"""Moves real projects between real engine versions with the launcher's own code (Docs/Engines.md -> Test it).

Usage, from the launcher repository:
    python Scripts/dev.py engine-test

A temporary folder plays GitHub: a release list, each tag's source archive and its EngineCopy.json, all made from the
engine checkout's tags and read through LAUNCHER_ENGINE_API. Tests/EngineProbe.cpp is the launcher's real download,
trial and switch. Nothing outside the folder changes. The steps it checks:

- two versions download and are checked against their records; an archive that does not match is refused;
- a project made with engine 0.2.0 fails its trial on 0.3.0 at the editor build (0.3 renamed the worker target), and
  stays as it was, build folder included;
- "change anyway" moves it and removes its build folder;
- a project made with engine 0.3.0 passes its trial on the same engine.

The two versions are commits that were once released; their tags are gone, so the test tags them in a temporary clone.
Those commits never change, so the expected results do not either. Only the standard library.
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from engine_location import REPOSITORY  # noqa: E402
from import_engine import COPY_FILE, engine_copy  # noqa: E402

PROBE = "LauncherEngineProbe.exe"
OLD_TAG = "v0.2.0"
NEW_TAG = "v0.3.0"
FIXTURES = {OLD_TAG: "ee424a472bf5f4f496e920d18cfd2fa70f778506", NEW_TAG: "b586e019b939401d30eb752122467283d18b8e1d"}
PROJECT_NAME = "Trial"
TOOLS = REPOSITORY / "ToolCache"
API_VARIABLE = "LAUNCHER_ENGINE_API"
SKIPPED_IN_SNAPSHOT = {"Engine"}


class EngineTestFailed(Exception):
    pass


def check(condition: bool, message: str) -> None:
    if not condition:
        raise EngineTestFailed(message)
    print(f"  ok: {message}", flush=True)


def file_uri(path: Path) -> str:
    return path.resolve().as_uri()


def write_release(engine: Path, tag: str, folder: Path) -> dict:
    """A tag's record and source archive, and the API's entry for them."""
    _, _, record = engine_copy(engine, tag, require_pushed=False)
    folder.mkdir(parents=True)
    record_file = folder / COPY_FILE
    record_file.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8", newline="\n")
    archive = folder / "source.tar.gz"
    subprocess.run(["git", "-C", str(engine), "archive", "--format=tar.gz", f"--prefix=owner-engine-{tag}/",
                    f"--output={archive}", tag], check=True)
    return {"tag_name": tag, "draft": False, "prerelease": True, "tarball_url": file_uri(archive),
            "assets": [{"name": COPY_FILE, "browser_download_url": file_uri(record_file)}]}


def write_feed(engine: Path, root: Path, tags: list[str]) -> Path:
    feed = root / "releases.json"
    entries = [write_release(engine, tag, root / "Releases" / tag) for tag in tags]
    feed.write_text(json.dumps(entries), encoding="utf-8", newline="\n")
    return feed


def probe(probe_exe: Path, data: Path, env: dict[str, str], *arguments: str) -> list[str]:
    """Its output lines; a failed run is the test's failure unless the line says otherwise."""
    command = [str(probe_exe), str(TOOLS), str(data), *arguments]
    print(f"> {subprocess.list2cmdline(arguments)}", flush=True)
    result = subprocess.run(command, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        raise EngineTestFailed(f"{subprocess.list2cmdline(arguments)} failed:\n{result.stdout[-3000:]}{result.stderr}")
    return result.stdout.splitlines()


def snapshot(project: Path) -> dict[str, str]:
    """Every file of the project with its hash; the engine is represented by its version."""
    files = {}
    for path in sorted(project.rglob("*")):
        relative = path.relative_to(project)
        if path.is_file() and relative.parts[0] not in SKIPPED_IN_SNAPSHOT and "__pycache__" not in relative.parts:
            files[relative.as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return files


def engine_version(project: Path) -> str:
    return json.loads((project / "Engine" / "EngineManifest.json").read_text(encoding="utf-8"))["version"]


def make_project(root: Path, cached_engine: Path) -> Path:
    root.mkdir(parents=True)
    shutil.copytree(cached_engine, root / "Engine")
    python = TOOLS / "python" / "python.exe"
    subprocess.run([str(python), "-X", "utf8", "Engine", "new", "--name", PROJECT_NAME], cwd=root, check=True,
                   capture_output=True)
    return root


def fixture_repository(engine: Path, folder: Path) -> Path:
    """A clone of the engine with the two versions tagged; the engine's own tags are not touched."""
    subprocess.run(["git", "clone", "--quiet", "--no-checkout", "--shared", str(engine), str(folder)], check=True)
    for tag, commit in FIXTURES.items():
        subprocess.run(["git", "-C", str(folder), "tag", "--force", tag, commit], check=True, capture_output=True)
    return folder


def run_engine_test(engine: Path, build: Path) -> int:
    probe_exe = build / PROBE
    with tempfile.TemporaryDirectory(prefix="engine-test-", ignore_cleanup_errors=True) as temporary:
        root = Path(temporary)
        engine = fixture_repository(engine, root / "Repository")
        feed = write_feed(engine, root / "Feed", [OLD_TAG, NEW_TAG])
        env = {**os.environ, API_VARIABLE: file_uri(feed)}
        data = root / "Data"
        cache = data / "Engines"

        print("Download", flush=True)
        listed = probe(probe_exe, data, env, "list")
        check(listed[:2] == [NEW_TAG[1:], OLD_TAG[1:]], "the list holds both versions, newest first")
        probe(probe_exe, data, env, "download", OLD_TAG[1:])
        probe(probe_exe, data, env, "download", NEW_TAG[1:])
        check((cache / f"Engine{OLD_TAG[1:]}" / COPY_FILE).is_file(), "the download is in the cache with its record")

        print("Refusing an archive that does not match its record", flush=True)
        bad = root / "BadFeed"
        entries = json.loads(feed.read_text(encoding="utf-8"))
        entries[0]["assets"], entries[1]["assets"] = entries[1]["assets"], entries[0]["assets"]
        bad.mkdir()
        (bad / "releases.json").write_text(json.dumps(entries), encoding="utf-8", newline="\n")
        bad_data = root / "BadData"
        refused = probe(probe_exe, bad_data, {**env, API_VARIABLE: file_uri(bad / "releases.json")}, "try-download",
                        OLD_TAG[1:])
        check(refused[-1].startswith("refused"), "a release with the wrong record is refused")
        check(not any((bad_data / "Engines").glob("Engine*")), "nothing is left in the cache")

        print(f"A project on {OLD_TAG} tries {NEW_TAG}", flush=True)
        old = make_project(root / "Old", cache / f"Engine{OLD_TAG[1:]}")
        build_marker = old / "build" / "Marker.txt"
        build_marker.parent.mkdir()
        build_marker.write_text("an existing build\n", encoding="utf-8")
        before = snapshot(old)
        result = probe(probe_exe, data, env, "trial", str(old), NEW_TAG[1:])
        check(result[-1] == "failed Editor build", "it fails at the editor build, not the game build")
        check(snapshot(old) == before and engine_version(old) == OLD_TAG[1:], "the project is exactly as it was")
        check(not (data / "EngineTrial").exists(), "the temporary copy is gone")

        print("Changing anyway", flush=True)
        probe(probe_exe, data, env, "switch", str(old), NEW_TAG[1:])
        check(engine_version(old) == NEW_TAG[1:], "the project is on the new engine")
        check(not build_marker.exists(), "its old build folder is gone")
        check((old / "Game").is_dir() and (old / "Project.json").is_file(), "its own files are still there")

        print(f"A project on {NEW_TAG} passes", flush=True)
        new = make_project(root / "New", cache / f"Engine{NEW_TAG[1:]}")
        result = probe(probe_exe, data, env, "trial", str(new), NEW_TAG[1:])
        check(result[-1] == "passed", "game, editor and tests pass")
    print("Engine test passed.", flush=True)
    return 0
