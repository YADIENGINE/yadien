"""Updates a really installed launcher from one version to the next, from a local folder (Docs/Packaging.md).

Usage, from the launcher repository:
    python Scripts/dev.py update-test [--keep]

Packs this build twice under a separate id, so the machine's real installation, shortcuts and settings stay out of it,
installs the first version under <build>/UpdateTest/ and updates it to the second through the launcher's real update
backend (Tests/UpdateProbe.cpp), its release files served from a local folder laid out like GitHub's download
addresses through LAUNCHER_UPDATE_RELEASES. Then it checks
what users rely on: the new version is in place, a delta was used, notes arrived, data survived. Uninstalling removes
everything again. `--keep` stops after the first install and prints how to open that launcher, to see the panel.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import time
import winreg
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from engine_location import REPOSITORY  # noqa: E402
from package import launcher_version, pack, setup_name, stage  # noqa: E402

PROBE = "LauncherUpdateProbe.exe"
SOURCE_VARIABLE = "LAUNCHER_UPDATE_RELEASES"  # Apps/Ui/Update/VelopackUpdates.cpp
PACK_ID_SUFFIX = "UpdateTest"
VERSION_FILE = "sq.version"  # Velopack's manifest in current/
MARKER_FILE = "UpdateTestMarker.txt"  # Only in the second version's package
SENTINEL_FILE = "UpdateTestSentinel.txt"  # In the data folder: must survive the update
APPLY_TIMEOUT_SECONDS = 180
UNINSTALL_TIMEOUT_SECONDS = 60
POLL_SECONDS = 0.5
FEED_FILE = "releases.win.json"
SAMPLE_NOTES = """Early preview. For testing. Windows only, unsigned (SmartScreen asks to confirm); 0.x versions may break.

Launcher {version}, contains engine 0.3.1.

- The launcher now updates itself: a button in the top bar shows when a new version is out
- Settings has a switch for the check at startup

Engine 0.3.1:

- Breaking: Editor previews are editor scenes now. Migrate: rename the worker target in the project's CMakeLists.txt
- Console scrolling keeps its place while new output arrives
- A much longer line so the panel's wrapping can be judged: new standalone graphics are registered by default and show up in the Assets panel without restarting the editor
"""


class UpdateTestFailed(Exception):
    pass


def next_patch(version: str) -> str:
    major, minor, patch = version.split(".")
    return f"{major}.{minor}.{int(patch) + 1}"


def file_url(path: Path) -> str:
    return "file:///" + path.resolve().as_posix()


def fake_releases(releases: Path, server: Path, feed: str = FEED_FILE) -> Path:
    """GitHub's download addresses as folders: the latest feed, and every package the feed names in its version's
    release (Core/Update/ReleaseSource.h)."""
    latest = server / "latest" / "download"
    latest.mkdir(parents=True)
    shutil.copy2(releases / feed, latest / feed)
    for asset in json.loads((releases / feed).read_text(encoding="utf-8"))["Assets"]:
        package = releases / asset["FileName"]
        if package.is_file():
            folder = server / "download" / f"v{asset['Version']}"
            folder.mkdir(parents=True, exist_ok=True)
            shutil.copy2(package, folder / package.name)
    return server


def installed_version(current: Path) -> str | None:
    """The version Velopack reads; None while current/ is being replaced."""
    try:
        match = re.search(r"<version>([^<]+)</version>", (current / VERSION_FILE).read_text(encoding="utf-8"))
    except OSError:
        return None
    return match.group(1) if match else None


def parse_update_line(output: str) -> tuple[str, int, int] | None:
    """The probe's "update <version> <bytes> <notes length>" line."""
    match = re.search(r"^update (\S+) (\d+) (\d+)$", output, re.MULTILINE)
    return (match.group(1), int(match.group(2)), int(match.group(3))) if match else None


def user_path() -> str:
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as key:
        return winreg.QueryValueEx(key, "Path")[0]


def probe(install: Path, mode: str, source: str | None) -> subprocess.CompletedProcess:
    env = {name: value for name, value in os.environ.items() if name != SOURCE_VARIABLE}
    if source is not None:
        env[SOURCE_VARIABLE] = source
    command = [str(install / "current" / PROBE), mode]
    print(f"> {SOURCE_VARIABLE}={source} {subprocess.list2cmdline(command)}", flush=True)
    result = subprocess.run(command, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
    print(result.stdout, end="", flush=True)
    return result


def wait_until(condition, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(POLL_SECONDS)
    return False


def uninstall(install: Path) -> None:
    updater = install / "Update.exe"
    if updater.is_file():
        print("> uninstalling the test installation", flush=True)
        subprocess.run([str(updater), "--silent", "--uninstall"])
        wait_until(lambda: not (install / "current").exists(), UNINSTALL_TIMEOUT_SECONDS)
    shutil.rmtree(install, ignore_errors=True)
    shutil.rmtree(data_folder(install), ignore_errors=True)


def data_folder(install: Path) -> Path:
    return install.with_name(install.name + " Data")  # Core/Settings/LauncherLayout.cpp


def check(condition: bool, message: str) -> None:
    if not condition:
        raise UpdateTestFailed(message)
    print(f"  ok: {message}", flush=True)


def build_packages(output: Path, build: Path, brand: Path, product: str, executable: str, first: str, second: str,
                   runner=subprocess.run) -> tuple[Path, Path]:
    """Returns the first version's installer and the folder of update files."""
    lock = json.loads((REPOSITORY / "Toolchain.lock.json").read_text(encoding="utf-8"))
    stage_dir = output / "Stage"
    releases = output / "Releases"
    stage(build, stage_dir, executable, build.parent / "Engines", REPOSITORY / "ToolCache", lock)
    shutil.copy2(build / PROBE, stage_dir / PROBE)
    pack_id = product + PACK_ID_SUFFIX
    no_shortcuts = ["--shortcuts", ""]  # The test must not touch the desktop or the Start menu
    setup = pack(stage_dir, releases, brand, pack_id, executable, first, runner, extra=no_shortcuts)
    setup = setup.replace(output / setup.name)  # Every version's installer has the same name; the next would replace it
    (stage_dir / MARKER_FILE).write_text(second, encoding="utf-8")
    notes = output / "Notes.md"
    notes.write_text(SAMPLE_NOTES.format(version=second), encoding="utf-8", newline="\n")
    pack(stage_dir, releases, brand, pack_id, executable, second, runner, notes=notes, extra=no_shortcuts)
    return setup, releases


def run_update_test(engine: Path, tree: Path, keep: bool) -> int:
    from Tools.manifest import load_manifest

    product = load_manifest(engine).name
    executable = product.lower()
    first = launcher_version()
    second = next_patch(first)
    output = tree / "UpdateTest"
    install = output / "Install"
    path_before = user_path()
    uninstall(install)
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    try:
        setup, releases = build_packages(output, tree / "Release", tree / "Brand", product, executable, first, second)
        server = fake_releases(releases, output / "Server")

        print(f"> installing {setup.name} into {install}", flush=True)
        if subprocess.run([str(setup), "--silent", "--installto", str(install)]).returncode != 0:
            raise UpdateTestFailed("the first version did not install")
        current = install / "current"
        check(installed_version(current) == first, f"version {first} is installed")
        data_folder(install).mkdir(parents=True, exist_ok=True)
        (data_folder(install) / SENTINEL_FILE).write_text("keep", encoding="utf-8")

        if keep:
            launcher = current / f"{executable}.exe"
            print("\nKept. Open the launcher and go through the update yourself:")
            print(f'  $env:{SOURCE_VARIABLE} = "{file_url(server)}"; & "{launcher}"')
            print(f"Remove it with: {install / 'Update.exe'} --silent --uninstall")
            return 0

        live = probe(install, "check", None)  # The real GitHub releases: informative, the network may be down
        print(f"  live check against GitHub: {live.stdout.strip() or live.stderr.strip()}", flush=True)
        offline = probe(install, "check", file_url(output / "Missing"))
        check(offline.returncode == 1 and offline.stdout.startswith("failed:") and FEED_FILE in offline.stdout,
              "an unreachable server is an error that names what could not be read")

        checked = probe(install, "check", file_url(server))
        update = parse_update_line(checked.stdout)
        check(update is not None and update[0] == second, f"version {second} is found")
        full = releases / f"{product}{PACK_ID_SUFFIX}-{second}-full.nupkg"
        check(update[1] < full.stat().st_size, "the delta package is offered, not the full one")
        check(update[2] > 0, "the release notes arrive with the check")

        applied = probe(install, "update", file_url(server))
        check(applied.returncode == 0 and "applying" in applied.stdout, "the download finishes and the updater starts")
        check(wait_until(lambda: installed_version(current) == second and (current / MARKER_FILE).is_file(),
                         APPLY_TIMEOUT_SECONDS), f"version {second} replaced the first")
        check((data_folder(install) / SENTINEL_FILE).is_file(), "the data folder survives the update")
        command = subprocess.run([str(current / f"{executable}.com"), "launcher", "version"], capture_output=True)
        check(command.returncode == 0, "the updated launcher runs")
        after = probe(install, "check", file_url(server))
        check(after.stdout.strip() == "up-to-date", "the updated launcher finds nothing newer")
    except UpdateTestFailed as error:
        print(f"error: {error}", file=sys.stderr)
        uninstall(install)
        return 1
    uninstall(install)
    if user_path() != path_before:
        print(f"error: PATH changed by the test; before: {path_before}\nafter: {user_path()}", file=sys.stderr)
        return 1
    print("Update test passed", flush=True)
    return 0
