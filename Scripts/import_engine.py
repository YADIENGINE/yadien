"""Copies a stable engine release into a folder of engine copies, Engine<version>/ (Docs/Engines.md).

Usage, from the launcher repository:
    python Scripts/import_engine.py v0.2.0 [--engines FOLDER]

The copy is taken from the tag with `git archive` in the engine checkout (Scripts/engine_location.py), so it is exactly the tagged content:
`.git`, uncommitted changes and the engine's `export-ignore` files (its development files) never enter it. The tag
must be pushed to the engine's origin, so every copy is traceable. `EngineCopy.json` is written beside the copy:
source tag and commit, and a SHA-256 for every file; the launcher checks downloads and projects' copies against it.

The launcher ships one such copy (dev.py puts it in <build>/Engines/); the others it downloads from the tag and the
same record, which `dev.py release` publishes.
"""

import argparse
import hashlib
import io
import json
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from engine_location import default_engine  # noqa: E402

REPOSITORY = Path(__file__).resolve().parents[1]
MANIFEST_FILE = "EngineManifest.json"
NOTES_FOLDER = "ReleaseNotes"
COPY_FILE = "EngineCopy.json"
COPY_FORMAT = 1
FOLDER_PREFIX = "Engine"


class ImportFailed(Exception):
    pass


def parse_version(text: str) -> tuple[int, int, int] | None:
    parts = text.split(".")
    if len(parts) != 3 or not all(part.isdigit() and (part == "0" or not part.startswith("0")) for part in parts):
        return None
    return int(parts[0]), int(parts[1]), int(parts[2])


def git(engine: Path, *args: str) -> bytes:
    result = subprocess.run(["git", "-C", str(engine), *args], capture_output=True)
    if result.returncode != 0:
        raise ImportFailed(f"git {' '.join(args)}: {result.stderr.decode(errors='replace').strip()}")
    return result.stdout


def tag_commit(engine: Path, tag: str) -> str:
    try:
        return git(engine, "rev-parse", "--verify", "--quiet", f"refs/tags/{tag}^{{commit}}").decode().strip()
    except ImportFailed as error:
        raise ImportFailed(f"Tag {tag} not found in {engine}; run: git -C {engine} fetch --tags") from error


def check_pushed(engine: Path, tag: str, commit: str) -> None:
    """The tag on origin points at the same commit (an annotated tag is listed with its peeled `^{}` line)."""
    listed = git(engine, "ls-remote", "--tags", "origin", f"refs/tags/{tag}", f"refs/tags/{tag}^{{}}").decode()
    remote = {ref: sha for sha, ref in (line.split("\t") for line in listed.splitlines() if "\t" in line)}
    pushed = remote.get(f"refs/tags/{tag}^{{}}", remote.get(f"refs/tags/{tag}"))
    if pushed is None:
        raise ImportFailed(f"Tag {tag} is not on the engine's origin; push it first: git -C {engine} push origin {tag}")
    if pushed != commit:
        raise ImportFailed(f"Tag {tag} points at {pushed} on origin but at {commit} locally")


def archive_files(engine: Path, tag: str) -> dict[str, bytes]:
    """Every file of the tag as the copy will hold it; links and paths leaving the folder are refused."""
    files = {}
    with tarfile.open(fileobj=io.BytesIO(git(engine, "archive", "--format=tar", tag))) as archive:
        for member in archive.getmembers():
            if member.isdir() or member.type == tarfile.XGLTYPE or member.type == tarfile.XHDTYPE:
                continue
            path = Path(member.name)
            if path.is_absolute() or ".." in path.parts or not member.isfile():
                raise ImportFailed(f"{tag}: unsupported archive entry {member.name}")
            if path.as_posix() == COPY_FILE:
                raise ImportFailed(f"{tag}: the engine must not contain {COPY_FILE}; it is written by this import")
            files[path.as_posix()] = archive.extractfile(member).read()
    return files


def read_notes(files: dict[str, bytes], manifest: dict) -> list:
    """Newest first: every ReleaseNotes/<version>.json; engines before that folder keep them in the manifest."""
    found = {}
    for path, content in files.items():
        folder, _, name = path.rpartition("/")
        version = parse_version(name.removesuffix(".json")) if name.endswith(".json") else None
        if folder == NOTES_FOLDER and version is not None:
            found[version] = {**json.loads(content.decode("utf-8")), "version": name.removesuffix(".json")}
    if not found:
        return manifest.get("notes")
    return [found[version] for version in sorted(found, reverse=True)]


def read_manifest(files: dict[str, bytes], tag: str) -> dict:
    """The tag's manifest, with its notes in `notes` wherever the tag keeps them."""
    if MANIFEST_FILE not in files:
        raise ImportFailed(f"{tag} has no {MANIFEST_FILE}")
    manifest = json.loads(files[MANIFEST_FILE].decode("utf-8"))
    version = manifest.get("version", "")
    if f"v{version}" != tag:
        raise ImportFailed(f"Tag {tag} holds engine version {version}; tags are named v<version>")
    notes = read_notes(files, manifest)
    if not isinstance(notes, list) or not notes or notes[0].get("version") != version:
        raise ImportFailed(f"{tag}: the newest note must be version {version} ({NOTES_FOLDER}/{version}.json)")
    manifest["notes"] = notes
    if not isinstance(manifest.get("toolsContract"), int) or not manifest.get("name"):
        raise ImportFailed(f"{tag}: {MANIFEST_FILE} needs name and toolsContract")
    return manifest


def copy_record(tag: str, commit: str, files: dict[str, bytes]) -> dict:
    return {
        "format": COPY_FORMAT,
        "source": {"tag": tag, "commit": commit},
        "files": {path: "sha256:" + hashlib.sha256(files[path]).hexdigest() for path in sorted(files)},
    }


def installed_version(folder: Path) -> tuple[int, int, int] | None:
    try:
        manifest = json.loads((folder / MANIFEST_FILE).read_text(encoding="utf-8"))
        return parse_version(manifest["version"])
    except (OSError, ValueError, KeyError):
        return None


def write_copy(engines: Path, folder_name: str, files: dict[str, bytes], record: dict) -> Path:
    """Writes beside the destination first, then swaps, so a failed import leaves the old copy in place."""
    destination = engines / folder_name
    staging = engines / f".{folder_name}.importing"
    retired = engines / f".{folder_name}.old"
    for leftover in (staging, retired):
        if leftover.exists():
            shutil.rmtree(leftover)
    for path, content in files.items():
        target = staging / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
    (staging / COPY_FILE).write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8", newline="\n")
    if destination.exists():
        destination.rename(retired)
    staging.rename(destination)
    if retired.exists():
        shutil.rmtree(retired)
    return destination


def engine_copy(engine: Path, tag: str, require_pushed: bool = True) -> tuple[dict[str, bytes], dict, dict]:
    """The tag's files, its manifest and the record that describes them."""
    if not tag.startswith("v") or parse_version(tag[1:]) is None:
        raise ImportFailed(f"'{tag}' is not a release tag (v<MAJOR.MINOR.PATCH>)")
    commit = tag_commit(engine, tag)
    if require_pushed:
        check_pushed(engine, tag, commit)
    files = archive_files(engine, tag)
    return files, read_manifest(files, tag), copy_record(tag, commit, files)


def import_engine(engine: Path, engines: Path, tag: str, require_pushed: bool = True) -> Path:
    files, manifest, record = engine_copy(engine, tag, require_pushed)
    engines.mkdir(parents=True, exist_ok=True)
    return write_copy(engines, f"{FOLDER_PREFIX}{manifest['version']}", files, record)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Copy a stable engine tag into a folder of engine copies")
    parser.add_argument("tag", help="release tag, e.g. v0.2.0")
    parser.add_argument("--engine", type=Path, default=default_engine(),
                        help="engine repository (default: the Engine/ submodule, or the enclosing project's Engine/)")
    parser.add_argument("--engines", type=Path, default=REPOSITORY / "build" / "Engines", help="destination folder")
    args = parser.parse_args(argv)
    try:
        destination = import_engine(args.engine, args.engines, args.tag)
    except ImportFailed as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    record = json.loads((destination / COPY_FILE).read_text(encoding="utf-8"))
    print(f"Imported {args.tag} ({record['source']['commit'][:10]}) into {destination} "
          f"({len(record['files'])} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
