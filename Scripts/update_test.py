"""Updates a really installed launcher from one version to the next, from a local folder (Docs/Packaging.md).

Usage, from the launcher repository:
    python Scripts/dev.py update-test [--keep]

Packs this build twice under a separate id, so the machine's real installation, shortcuts and settings stay out of it,
installs the first version under <build>/UpdateTest/ and updates it to the second through the launcher's real update
backend (Tests/UpdateProbe.cpp), its release list served from local files through LAUNCHER_UPDATE_API. Then it checks
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
API_VARIABLE = "LAUNCHER_UPDATE_API"  # Apps/Ui/Update/VelopackUpdates.cpp
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


def fake_release_list(releases: Path, feed: str = FEED_FILE) -> str:
    """A GitHub releases answer with one pre-release holding the folder's update files."""
    names = [feed, *sorted(path.name for path in releases.glob("*.nupkg"))]
    assets = [{"name": name, "browser_download_url": file_url(releases / name)} for name in names]
    return json.dumps([{"draft": False, "prerelease": True, "published_at": "2026-01-01T00:00:00Z", "assets": assets}])


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


def probe(install: Path, mode: str, api: str | None) -> subprocess.CompletedProcess:
    env = {name: value for name, value in os.environ.items() if name != API_VARIABLE}
    if api is not None:
        env[API_VARIABLE] = api
    command = [str(install / "current" / PROBE), mode]
    print(f"> {API_VARIABLE}={api} {subprocess.list2cmdline(command)}", flush=True)
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
    stage(build, stage_dir, executable, REPOSITORY / "Engines", REPOSITORY / "ToolCache", lock)
    shutil.copy2(build / PROBE, stage_dir / PROBE)
    pack_id = product + PACK_ID_SUFFIX
    no_shortcuts = ["--shortcuts", ""]  # The test must not touch the desktop or the Start menu
    setup = pack(stage_dir, releases, brand, pack_id, executable, first, runner, extra=no_shortcuts)
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
        api = output / "api.json"
        api.write_text(fake_release_list(releases), encoding="utf-8")

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
            print(f'  $env:{API_VARIABLE} = "{file_url(api)}"; & "{launcher}"')
            print(f"Remove it with: {install / 'Update.exe'} --silent --uninstall")
            return 0

        live = probe(install, "check", None)  # The real GitHub API: informative, the network may be down
        print(f"  live check against GitHub: {live.stdout.strip() or live.stderr.strip()}", flush=True)
        offline = probe(install, "check", file_url(output / "missing.json"))
        check(offline.returncode == 1 and offline.stdout.startswith("failed:"), "an unreachable server is an error")

        checked = probe(install, "check", file_url(api))
        update = parse_update_line(checked.stdout)
        check(update is not None and update[0] == second, f"version {second} is found")
        full = releases / f"{product}{PACK_ID_SUFFIX}-{second}-full.nupkg"
        check(update[1] < full.stat().st_size, "the delta package is offered, not the full one")
        check(update[2] > 0, "the release notes arrive with the check")

        applied = probe(install, "update", file_url(api))
        check(applied.returncode == 0 and "applying" in applied.stdout, "the download finishes and the updater starts")
        check(wait_until(lambda: installed_version(current) == second and (current / MARKER_FILE).is_file(),
                         APPLY_TIMEOUT_SECONDS), f"version {second} replaced the first")
        check((data_folder(install) / SENTINEL_FILE).is_file(), "the data folder survives the update")
        command = subprocess.run([str(current / f"{executable}.com"), "launcher", "version"], capture_output=True)
        check(command.returncode == 0, "the updated launcher runs")
        after = probe(install, "check", file_url(api))
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
