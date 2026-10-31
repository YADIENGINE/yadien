"""Builds the installer from a Release build (design D15; Docs/Packaging.md). Called by `dev.py package`.

Steps: stage the files the launcher ships, write their license notices, run Velopack's `vpk pack`.

    build/Package/Stage/       what gets installed into current/:
        <engine>.exe, <engine>.com, the Velopack DLL
        Engines/               the stable engine copies (Engines/ of this repository)
        Tools/                 ToolCache's tools without the lock's "exclude" entries, + Toolchain.json
        ThirdPartyNotices.txt  Part 1: the engine (its own license + the components games get), Part 2: the launcher
                               and its tools (Packaging/Notices.json)
    build/Package/Releases/    vpk's output: <Name>-Setup-<version>.exe and the update files (nupkg, releases.win.json)

The package's version is the launcher's (LAUNCHER_VERSION in CMakeLists.txt); the Velopack id and title are the
product name from the engine manifest. Signing is not set up yet (design D19): vpk pack is where it would go.
Only the standard library; the engine's notice helpers come from the engine checkout dev.py puts on sys.path.
"""

import fnmatch
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # Scripts/, also when imported as Scripts.package
from engine_location import REPOSITORY  # noqa: E402

NOTICES_FILE = REPOSITORY / "Packaging" / "Notices.json"
OUTPUT_NOTICES = "ThirdPartyNotices.txt"  # Core/Settings/LauncherLayout.h LicenseNoticesFile
VELOPACK_DLL = "velopack_libc.dll"  # The name <engine>.exe imports (Apps/Ui/CMakeLists.txt copies it so)
STAMP_FILE = ".fetched"  # fetch_toolchain's marker inside a tool folder; not shipped


class PackageFailed(Exception):
    pass


def launcher_version(cmake_lists: Path = REPOSITORY / "CMakeLists.txt") -> str:
    match = re.search(r'set\(LAUNCHER_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\)', cmake_lists.read_text(encoding="utf-8"))
    if match is None:
        raise PackageFailed(f"{cmake_lists}: LAUNCHER_VERSION not found")
    return match.group(1)


def excluded(relative: Path, patterns: list[str]) -> bool:
    """True when the path or one of its parent folders matches an "exclude" entry (paths relative to the tool)."""
    text = relative.as_posix()
    return any(text == pattern or text.startswith(pattern.rstrip("/") + "/") or fnmatch.fnmatch(text, pattern)
               for pattern in patterns)


def copy_tool(source: Path, destination: Path, patterns: list[str]) -> None:
    for file in sorted(source.rglob("*")):
        relative = file.relative_to(source)
        if file.is_dir() or file.name == STAMP_FILE or excluded(relative, patterns):
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)


def launcher_notices(notices: dict, lock: dict, roots: dict[str, Path]) -> str:
    """Part 2 of ThirdPartyNotices.txt: one section per component, versions of tools / libraries from the lock."""
    versions = {name: entry["version"] for section in ("tools", "libraries")
                for name, entry in lock.get(section, {}).items()}
    sections = [notices["intro"]]
    for component in notices["components"]:
        path = Path(component["text"].format(**{key: value.as_posix() for key, value in roots.items()}))
        if not path.is_file():
            raise PackageFailed(f"{component['name']}: license text {path} not found")
        title = component["name"]
        if "lock" in component:
            if component["lock"] not in versions:
                raise PackageFailed(f"{component['name']}: '{component['lock']}' is not in Toolchain.lock.json")
            title += f" {versions[component['lock']]}"
        title += f" ({component['license']})"
        sections.append(f"{title}\n{'-' * len(title)}\n\n{path.read_text(encoding='utf-8').strip()}")
    return "\n\n".join(sections) + "\n"


def stage(build: Path, stage_dir: Path, executable: str, engines: Path, cache: Path, lock: dict) -> None:
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    stage_dir.mkdir(parents=True)
    for name in (f"{executable}.exe", f"{executable}.com", VELOPACK_DLL):
        if not (build / name).is_file():
            raise PackageFailed(f"{build / name} is missing; build the launcher in Release first")
        shutil.copy2(build / name, stage_dir / name)
    shutil.copytree(engines, stage_dir / "Engines")
    for name, tool in lock["tools"].items():
        if not (cache / name / tool["executable"]).is_file():
            raise PackageFailed(f"{name} is missing in {cache}; run: python Scripts/fetch_toolchain.py")
        copy_tool(cache / name, stage_dir / "Tools" / name, tool.get("exclude", []))
    shutil.copy2(cache / "Toolchain.json", stage_dir / "Tools" / "Toolchain.json")


def setup_name(product: str, version: str) -> str:
    return f"{product}-Setup-{version}.exe"


def pack(stage_dir: Path, releases: Path, product: str, executable: str, version: str, runner=subprocess.run,
         vpk: str | None = None) -> Path:
    vpk = vpk or shutil.which("vpk")
    if vpk is None:
        raise PackageFailed("vpk not found; install it: dotnet tool install -g vpk (Docs/Packaging.md)")
    releases.mkdir(parents=True, exist_ok=True)
    command = [vpk, "pack", "--packId", product, "--packVersion", version, "--packTitle", product, "--packAuthors",
               product, "--packDir", str(stage_dir), "--mainExe", f"{executable}.exe", "--outputDir", str(releases)]
    print(f"> {subprocess.list2cmdline(command)}", flush=True)
    if runner(command).returncode != 0:
        raise PackageFailed("vpk pack failed")
    setup = releases / setup_name(product, version)
    (releases / f"{product}-win-Setup.exe").replace(setup)
    return setup


def package(engine: Path, build: Path, output: Path, cache: Path = REPOSITORY / "ToolCache",
            engines: Path = REPOSITORY / "Engines", runner=subprocess.run) -> Path:
    """Stages a Release build and packs it; returns the setup executable."""
    from ThirdParty.notices import check_components, compose_notices, engine_notice, load_components
    from Tools.manifest import load_manifest

    manifest = load_manifest(engine)
    product, executable, version = manifest.name, manifest.name.lower(), launcher_version()
    lock = json.loads((REPOSITORY / "Toolchain.lock.json").read_text(encoding="utf-8"))
    components = load_components(engine / "ThirdParty")
    errors = check_components(components, engine / "ThirdParty")
    if errors:
        raise PackageFailed("engine notices are out of date:\n  " + "\n  ".join(errors))

    stage_dir = output / "Stage"
    stage(build, stage_dir, executable, engines, cache, lock)
    notices = json.loads(NOTICES_FILE.read_text(encoding="utf-8"))
    launcher_part = launcher_notices(notices, lock, {"repo": REPOSITORY, "engine": engine, "tools": cache})
    engine_text = engine_notice(manifest.name, manifest.version, (engine / "LICENSE").read_text(encoding="utf-8"))
    text = compose_notices(components, "Launcher", launcher_part, engine / "ThirdParty", engine_text=engine_text)
    (stage_dir / OUTPUT_NOTICES).write_text(text, encoding="utf-8", newline="\n")
    return pack(stage_dir, output / "Releases", product, executable, version, runner)
