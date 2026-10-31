"""Downloads the tools the launcher ships (Python, CMake, Ninja) and the libraries it links (Velopack) into
ToolCache/ (Docs/Toolchain.md).

Usage, from the launcher repository:
    python Scripts/fetch_toolchain.py          download what is missing, verify every archive's SHA-256
    python Scripts/fetch_toolchain.py --pin    record the SHA-256 of entries whose "sha256" is empty

Toolchain.lock.json is the only source: per tool a fixed version, its download address, the archive's SHA-256, the
archive folder to strip ("root") and the executable inside the tool folder. "libraries" entries are the same with a
"check" file instead of an executable: they are needed to build the launcher, not shipped as tools. A mismatching hash
stops the script; nothing unverified is extracted. Binaries never enter git.

ToolCache/<tool>/ receives each tool; ToolCache/Toolchain.json tells the launcher which versions it got and where the
executables are (the launcher verifies them against it). The embeddable Python stays as published, `._pth` file
included: it runs isolated, so another Python's registry entries or PYTHONPATH never leak in (Docs/Toolchain.md).
"""

import argparse
import hashlib
import json
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
LOCK_FILE = REPOSITORY / "Toolchain.lock.json"
CACHE = REPOSITORY / "ToolCache"
INSTALLED_FILE = "Toolchain.json"  # The launcher reads this one (Core/System/Toolchain)
STAMP_FILE = ".fetched"  # Inside a tool folder: the archive hash it came from
FORMAT = 1


class FetchFailed(Exception):
    pass


SECTIONS = {"tools": "executable", "libraries": "check"}  # Lock section → the file that must exist after extracting


def load_lock(path: Path) -> dict:
    lock = json.loads(path.read_text(encoding="utf-8"))
    if lock.get("format") != FORMAT:
        raise FetchFailed(f"{path}: expected \"format\": {FORMAT}")
    for section, required in SECTIONS.items():
        for name, entry in lock.get(section, {}).items():
            for key in ("version", "url", "sha256", required):
                if not isinstance(entry.get(key), str):
                    raise FetchFailed(f"{path}: {section}.{name}.{key} is required")
    return lock


def entries(lock: dict):
    """(section, name, entry) for every tool and library in the lock."""
    for section in SECTIONS:
        for name, entry in lock.get(section, {}).items():
            yield section, name, entry


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, destination: Path) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + ".part")
    print(f"  downloading {url}", flush=True)
    try:
        with urllib.request.urlopen(url) as response, partial.open("wb") as stream:
            shutil.copyfileobj(response, stream)
    except OSError as error:
        partial.unlink(missing_ok=True)
        raise FetchFailed(f"{url}: {error}") from error
    partial.replace(destination)
    return destination


def archive_path(cache: Path, name: str, tool: dict) -> Path:
    return cache / ".downloads" / f"{name}-{tool['version']}.zip"


def verified_archive(cache: Path, name: str, tool: dict) -> Path:
    """The tool's archive, downloaded when missing; a hash that differs from the lock stops everything."""
    archive = archive_path(cache, name, tool)
    if not archive.is_file() or sha256(archive) != tool["sha256"]:
        download(tool["url"], archive)
    actual = sha256(archive)
    if actual != tool["sha256"]:
        archive.unlink()
        raise FetchFailed(f"{name} {tool['version']}: SHA-256 {actual} does not match the lock ({tool['sha256']})")
    return archive


def extract(archive: Path, destination: Path, root: str | None) -> None:
    staging = destination.with_name(destination.name + ".extracting")
    if staging.exists():
        shutil.rmtree(staging)
    with zipfile.ZipFile(archive) as bundle:
        for member in bundle.infolist():
            parts = Path(member.filename).parts
            if root is not None:
                if not parts or parts[0] != root:
                    raise FetchFailed(f"{archive.name}: {member.filename} is outside the expected folder {root}")
                parts = parts[1:]
            if not parts or member.is_dir():
                continue
            if Path(member.filename).is_absolute() or ".." in parts:
                raise FetchFailed(f"{archive.name}: unsafe entry {member.filename}")
            target = staging.joinpath(*parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with bundle.open(member) as source, target.open("wb") as stream:
                shutil.copyfileobj(source, stream)
    if destination.exists():
        shutil.rmtree(destination)
    staging.rename(destination)


def fetch(lock: dict, cache: Path) -> None:
    installed = {}
    for section, name, tool in entries(lock):
        if not tool["sha256"]:
            raise FetchFailed(f"{section}.{name}.sha256 is empty; run with --pin first")
        folder = cache / name
        stamp = folder / STAMP_FILE
        if not (stamp.is_file() and stamp.read_text(encoding="utf-8").strip() == tool["sha256"]):
            print(f"{name} {tool['version']}", flush=True)
            extract(verified_archive(cache, name, tool), folder, tool.get("root"))
            stamp.write_text(tool["sha256"] + "\n", encoding="utf-8")
        required = tool[SECTIONS[section]]
        if not (folder / required).is_file():
            raise FetchFailed(f"{name}: {required} not found in {folder}")
        if section == "tools":
            installed[name] = {"version": tool["version"], "executable": f"{name}/{required}"}
    (cache / INSTALLED_FILE).write_text(json.dumps({"format": FORMAT, "tools": installed}, indent=2) + "\n",
                                        encoding="utf-8", newline="\n")


def pin(lock: dict, lock_path: Path, cache: Path) -> None:
    """Trust on first download: records the hash of an entry without one. Compare it with the publisher's
    checksum where one is published before committing the lock."""
    for _, name, tool in entries(lock):
        if tool["sha256"]:
            continue
        archive = download(tool["url"], archive_path(cache, name, tool))
        tool["sha256"] = sha256(archive)
        print(f"{name} {tool['version']}: sha256 {tool['sha256']}")
    lock_path.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8", newline="\n")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Download the launcher's tools and libraries into ToolCache/")
    parser.add_argument("--pin", action="store_true", help="record missing SHA-256 values in the lock")
    parser.add_argument("--lock", type=Path, default=LOCK_FILE)
    parser.add_argument("--cache", type=Path, default=CACHE)
    args = parser.parse_args(argv)
    try:
        lock = load_lock(args.lock)
        if args.pin:
            pin(lock, args.lock, args.cache)
        else:
            fetch(lock, args.cache)
            print(f"Tools ready in {args.cache}")
    except FetchFailed as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
