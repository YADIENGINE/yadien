"""Builds and tests the launcher in development (Docs/Development.md).

Usage, from the launcher repository:
    python Scripts/dev.py build [--release]   configure when needed, then build (build/<Config>/)
    python Scripts/dev.py test                Debug build, then the core's C++ tests and the scripts' tests
    python Scripts/dev.py clean               delete build/
    python Scripts/dev.py pin-engine          point the launcher's Engine submodule at the engine checkout's HEAD
    python Scripts/dev.py package             Release build, then the installer (Scripts/package.py, Docs/Packaging.md)

`--engine PATH` (before the command) picks the engine checkout; by default the launcher's own Engine/ submodule,
or the enclosing project's Engine/ when the launcher sits inside a project (Scripts/engine_location.py). The MSVC
environment comes from that engine's helper (Engine/Tools/msvc.py), so the launcher builds with the same compiler
setup as the engine's projects.
"""

import argparse
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


def cached(variable: str) -> str | None:
    cache = BUILD / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(f"{variable}:"):
            return line.split("=", 1)[1]
    return None


def configured_engine() -> Path | None:
    engine = cached(ENGINE_CACHE_VARIABLE)
    return Path(engine).resolve() if engine else None


def build(engine: Path, release: bool) -> dict[str, str]:
    from Tools.msvc import msvc_env, tool  # The chosen engine's helper (main puts it on sys.path)

    env = msvc_env(BUILD / ".msvc-env.json")
    # The preset turns the window on; a build tree configured before it existed is configured again.
    if not (BUILD / "build.ninja").exists() or configured_engine() != engine or cached("LAUNCHER_BUILD_UI") != "ON":
        run([tool("cmake"), "--preset", "launcher", f"-D{ENGINE_CACHE_VARIABLE}={engine.as_posix()}"], env)
    run([tool("cmake"), "--build", "--preset", "release" if release else "debug"], env)
    return env


def test(engine: Path) -> None:
    env = build(engine, release=False)
    from Tools.msvc import tool

    run([tool("ctest"), "--preset", "debug"], env)
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
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("build")
    command.add_argument("--release", action="store_true")
    commands.add_parser("test")
    commands.add_parser("clean")
    commands.add_parser("pin-engine")
    commands.add_parser("package")
    args = parser.parse_args(argv)

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
                build(engine, args.release)
            case "test":
                test(engine)
            case "clean":
                shutil.rmtree(BUILD, ignore_errors=True)
            case "pin-engine":
                pin_engine(engine)
            case "package":
                from package import PackageFailed, package

                build(engine, release=True)
                try:
                    setup = package(engine, BUILD / "Release", BUILD / "Package")
                except PackageFailed as error:
                    print(f"error: {error}", file=sys.stderr)
                    return 1
                print(f"Installer: {setup}")
    except ToolError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
