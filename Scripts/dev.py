"""Builds and tests the launcher in development (Docs/Development.md).

Usage, from the launcher repository:
    python Scripts/dev.py build [--release]   configure when needed, then build (build/<Config>/)
    python Scripts/dev.py test                Debug build, then the core's C++ tests and the scripts' tests
    python Scripts/dev.py clean               delete build/

The MSVC environment comes from the engine submodule's helper (Engine/Tools/msvc.py), so the launcher builds with
the same compiler setup as the engine's projects.
"""

import argparse
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
BUILD = REPOSITORY / "build"
sys.path.insert(0, str(REPOSITORY / "Engine"))

from Tools.errors import ToolError  # noqa: E402
from Tools.msvc import msvc_env, tool  # noqa: E402


def run(command: list[str], env: dict[str, str]) -> None:
    print(f"> {subprocess.list2cmdline(command)}", flush=True)
    if subprocess.run(command, cwd=REPOSITORY, env=env).returncode != 0:
        raise SystemExit(1)


def build(release: bool) -> dict[str, str]:
    env = msvc_env(BUILD / ".msvc-env.json")
    if not (BUILD / "build.ninja").exists():
        run([tool("cmake"), "--preset", "launcher"], env)
    run([tool("cmake"), "--build", "--preset", "release" if release else "debug"], env)
    return env


def test() -> None:
    env = build(release=False)
    run([tool("ctest"), "--preset", "debug"], env)
    suite = unittest.defaultTestLoader.discover(str(REPOSITORY / "Scripts" / "Tests"), top_level_dir=str(REPOSITORY))
    if not unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful():
        raise SystemExit(1)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Build and test the launcher")
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("build")
    command.add_argument("--release", action="store_true")
    commands.add_parser("test")
    commands.add_parser("clean")
    args = parser.parse_args(argv)
    try:
        match args.command:
            case "build":
                build(args.release)
            case "test":
                test()
            case "clean":
                shutil.rmtree(BUILD, ignore_errors=True)
    except ToolError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
