"""Builds and tests the launcher in development (Docs/Development.md).

Usage, from the launcher repository:
    python Scripts/dev.py build [--release]   configure when needed, then build (build/<Config>/)
    python Scripts/dev.py test                Debug build, then the core's C++ tests and the scripts' tests
    python Scripts/dev.py clean               delete build/
    python Scripts/dev.py pin-engine          point the launcher's Engine submodule at the engine checkout's HEAD
    python Scripts/dev.py package             Release build, then the installer (Scripts/package.py, Docs/Packaging.md)
    python Scripts/dev.py release [--dry-run] publish what the manifests ask for (Scripts/release.py, Docs/Packaging.md)
    python Scripts/dev.py update-test [--keep] update an installed launcher to a next version from local files
    python Scripts/dev.py engine-test         move a real project between engine versions from local files
    python Scripts/dev.py engine-releases [--dry-run] publish the record of every engine tag that has none

`--engine PATH` (before the command) picks the engine checkout; by default the launcher's own Engine/ submodule,
or the enclosing project's Engine/ when the launcher sits inside a project (Scripts/engine_location.py). The MSVC
environment comes from that engine's helper (Engine/Tools/msvc.py), so the launcher builds with the same compiler
setup as the engine's projects. `--build-dir PATH` (before the command) picks the build tree; by default build/.
The installer always goes to build/Package/.
"""

import argparse
import json
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

from engine_location import MANIFEST_FILE, REPOSITORY, default_engine, own_engine

BUILD = REPOSITORY / "build"
ENGINE_CACHE_VARIABLE = "LAUNCHER_ENGINE_DIR"


def run(command: list[str], env: dict[str, str] | None = None, cwd: Path = REPOSITORY) -> None:
    print(f"> {subprocess.list2cmdline(command)}", flush=True)
    if subprocess.run(command, cwd=cwd, env=env).returncode != 0:
        raise SystemExit(1)


def output(command: list[str], cwd: Path) -> str:
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"error: {subprocess.list2cmdline(command)}: {result.stderr.strip()}")
    return result.stdout.strip()


def cached(tree: Path, variable: str) -> str | None:
    cache = tree / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(f"{variable}:"):
            return line.split("=", 1)[1]
    return None


def configured_engine(tree: Path) -> Path | None:
    engine = cached(tree, ENGINE_CACHE_VARIABLE)
    return Path(engine).resolve() if engine else None


def import_shipped_engine(engine: Path, tree: Path) -> None:
    """<tree>/Engines/ holds exactly the engine LauncherManifest.json ships; a development launcher reads it too. Without
    the tag (an engine not released yet) a build goes on and the launcher finds no engine; packaging stops."""
    from import_engine import FOLDER_PREFIX, ImportFailed, import_engine
    from package import LAUNCHER_MANIFEST

    version = json.loads((REPOSITORY / LAUNCHER_MANIFEST).read_text(encoding="utf-8"))["engine"]
    engines = tree / "Engines"
    wanted = engines / f"{FOLDER_PREFIX}{version}"
    if engines.is_dir():
        for folder in engines.iterdir():
            if folder != wanted:
                shutil.rmtree(folder)
    if wanted.is_dir():
        return
    try:
        import_engine(engine, engines, f"v{version}")
    except ImportFailed as error:
        print(f"warning: engine {version} is not in {engines}: {error}", file=sys.stderr, flush=True)


def build(engine: Path, tree: Path, release: bool) -> dict[str, str]:
    from Tools.msvc import msvc_env, tool  # The chosen engine's helper (main puts it on sys.path)

    import_shipped_engine(engine, tree)
    env = msvc_env(tree / ".msvc-env.json")
    # The preset turns the window on; a build tree configured before it existed is configured again.
    if (not (tree / "build.ninja").exists() or configured_engine(tree) != engine
            or cached(tree, "LAUNCHER_BUILD_UI") != "ON"):
        run([tool("cmake"), "--preset", "launcher", "-B", str(tree), f"-D{ENGINE_CACHE_VARIABLE}={engine.as_posix()}"],
            env)
    run([tool("cmake"), "--build", str(tree), "--config", "Release" if release else "Debug"], env)
    return env


def test(engine: Path, tree: Path) -> None:
    env = build(engine, tree, release=False)
    from Tools.msvc import tool

    run([tool("ctest"), "--test-dir", str(tree), "-C", "Debug", "--output-on-failure"], env)
    suite = unittest.defaultTestLoader.discover(str(REPOSITORY / "Scripts" / "Tests"), top_level_dir=str(REPOSITORY))
    if not unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful():
        raise SystemExit(1)


def pin_engine(engine: Path) -> None:
    """Records the engine checkout's HEAD as the launcher's Engine submodule commit (staged, not committed). The
    commit must be on the engine's origin: the launcher repository may only point at pushed engine commits."""
    commit = output(["git", "rev-parse", "HEAD"], engine)
    output(["git", "fetch", "origin"], engine)
    if not output(["git", "branch", "-r", "--contains", commit], engine):
        raise SystemExit(f"error: engine commit {commit[:10]} is not pushed; push the engine first")
    if engine == own_engine().resolve():
        run(["git", "add", "Engine"])
    else:
        run(["git", "update-index", "--cacheinfo", f"160000,{commit},Engine"])
    print(f"Engine pointer set to {commit[:10]}; commit it in the launcher repository "
          f"(git -C {REPOSITORY} commit -m \"Update Engine\")")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Build and test the launcher")
    parser.add_argument("--engine", type=Path, help="engine checkout (default: see Scripts/engine_location.py)")
    parser.add_argument("--build-dir", type=Path, default=BUILD, help="build tree (default: build/)")
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("build")
    command.add_argument("--release", action="store_true")
    commands.add_parser("test")
    commands.add_parser("clean")
    commands.add_parser("pin-engine")
    command = commands.add_parser("package")
    command.add_argument("--notes", type=Path, help="Markdown release notes to put in the package")
    command.add_argument("--yes", action="store_true", help="overwrite this version in build/Package/Releases/")
    command = commands.add_parser("release")
    command.add_argument("--dry-run", action="store_true", help="show the plan and run the checks; change nothing")
    command = commands.add_parser("update-test")
    command.add_argument("--keep", action="store_true", help="stop after installing the first version, to try the panel")
    commands.add_parser("engine-test")
    command = commands.add_parser("engine-releases")
    command.add_argument("--dry-run", action="store_true", help="list what would be published")
    args = parser.parse_args(argv)

    tree = args.build_dir.resolve()
    engine = (args.engine or default_engine()).resolve()
    if not (engine / MANIFEST_FILE).is_file():
        print(f"error: no engine at {engine}; run: git submodule update --init Engine", file=sys.stderr)
        return 2
    print(f"Engine: {engine}", flush=True)
    sys.path.insert(0, str(engine))
    from Tools.errors import ToolError

    try:
        match args.command:
            case "build":
                build(engine, tree, args.release)
            case "test":
                test(engine, tree)
            case "clean":
                shutil.rmtree(tree, ignore_errors=True)
            case "pin-engine":
                pin_engine(engine)
            case "package":
                from package import PackageFailed, package

                build(engine, tree, release=True)
                try:
                    setup = package(engine, tree / "Release", tree / "Brand", BUILD / "Package", notes=args.notes,
                                    overwrite=args.yes)
                except PackageFailed as error:
                    print(f"error: {error}", file=sys.stderr)
                    return 1
                print(f"Installer: {setup}")
            case "release":
                from release import run_release

                return run_release(engine, args.dry_run)
            case "engine-releases":
                from release import run_engine_releases

                return run_engine_releases(engine, args.dry_run)
            case "engine-test":
                from Tools.msvc import tool
                from engine_test import EngineTestFailed, run_engine_test

                env = build(engine, tree, release=False)
                run([tool("cmake"), "--build", str(tree), "--config", "Debug", "--target", "LauncherEngineProbe"], env)
                try:
                    return run_engine_test(engine, tree / "Debug")
                except EngineTestFailed as error:
                    print(f"error: {error}", file=sys.stderr)
                    return 1
            case "update-test":
                from Tools.msvc import tool
                from package import PackageFailed
                from update_test import run_update_test

                env = build(engine, tree, release=True)
                run([tool("cmake"), "--build", str(tree), "--config", "Release", "--target", "LauncherUpdateProbe"], env)
                try:
                    return run_update_test(engine, tree, args.keep)
                except PackageFailed as error:
                    print(f"error: {error}", file=sys.stderr)
                    return 1
    except ToolError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
