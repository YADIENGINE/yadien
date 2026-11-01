"""Publishes what the manifests ask for: engine tag, launcher installer, project pointers (Docs/Packaging.md).

Usage, from the launcher repository:
    python Scripts/dev.py release [--dry-run]

The manifests are the desired state. A version without its pushed tag (engine) or its published GitHub release
(launcher) is released; everything else is left alone, so a run that stopped halfway continues when run again:

    EngineManifest.json    version + notes
    LauncherManifest.json  version + notes + engines (the exact engine versions the installer carries)

Steps: check -> engine (tests, commit, push, tag) -> launcher (import engines, pin, commit) -> release notes -> tests
and installer, built in a worktree at the pinned engine commit -> you try the installer and confirm -> push, tag,
GitHub release -> the enclosing project's submodule pointers, committed but not pushed.
The same notes go into the package (`vpk pack --releaseNotes`, read back from releases.win.json) and the GitHub release.
Only the standard library; the engine's manifest rules come from the engine checkout dev.py puts on sys.path.
"""

import json
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from engine_location import MANIFEST_FILE, REPOSITORY  # noqa: E402
from import_engine import FOLDER_PREFIX, ImportFailed, import_engine, installed_version, parse_version  # noqa: E402
from package import LAUNCHER_MANIFEST, PackageFailed, setup_name  # noqa: E402

BRANCH = "main"
ENGINE_POINTER = "Engine"
ENGINES_FOLDER = "Engines"
RELEASE_ENGINE = "ReleaseEngine"  # build/<this>/: worktree of the engine commit the release is built against
RELEASE_BUILD = "ReleaseBuild"  # build/<this>/: its own build tree, so development builds keep theirs
STAMP_FILE = "Release.json"  # build/Package/<this>: what the installer in Releases/ was built from
NOTES_FILE = "ReleaseNotes.md"  # build/Package/<this>: the notes of the package and of the GitHub release
PREVIEW_NOTE = "Early preview. For testing. Windows only, unsigned (SmartScreen asks to confirm); 0.x versions may break."
UPDATE_FILES = ("releases.win.json", "RELEASES", "assets.win.json")


class ReleaseFailed(Exception):
    pass


def run(command: list[str], cwd: Path) -> None:
    print(f"> {subprocess.list2cmdline(command)}", flush=True)
    if subprocess.run(command, cwd=cwd).returncode != 0:
        raise ReleaseFailed(f"failed: {subprocess.list2cmdline(command)}")


def capture(command: list[str], cwd: Path) -> str:
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        raise ReleaseFailed(f"{subprocess.list2cmdline(command)}: {result.stderr.strip()}")
    return result.stdout.strip()


def git(folder: Path, *args: str) -> str:
    return capture(["git", "-C", str(folder), *args], folder)


def git_succeeds(folder: Path, *args: str) -> bool:
    return subprocess.run(["git", "-C", str(folder), *args], capture_output=True).returncode == 0


def read_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ReleaseFailed(f"{path}: {error}") from error


def manifest_problems(data: dict) -> list[str]:
    """The launcher manifest's rules: `version` is the newest note, notes newest first with a date and changes, and
    `engines` holds one exact version per series."""
    problems = []
    if parse_version(str(data.get("version"))) is None:
        problems.append(f"version '{data.get('version')}' is not MAJOR.MINOR.PATCH")
    engines = data.get("engines")
    if not isinstance(engines, list) or not engines:
        problems.append("engines: at least one engine version is required")
    else:
        parsed = [parse_version(str(engine)) for engine in engines]
        if None in parsed:
            problems.append(f"engines: {engines} must be MAJOR.MINOR.PATCH versions")
        elif len({version[:2] for version in parsed}) != len(parsed):
            problems.append(f"engines: {engines} holds two versions of one series; a series carries its newest patch")
    notes = data.get("notes")
    if not isinstance(notes, list) or not notes:
        return problems + ["notes: at least the current version's note is required"]
    previous = None
    for index, note in enumerate(notes):
        version = parse_version(str(note.get("version"))) if isinstance(note, dict) else None
        if version is None:
            problems.append(f"notes[{index}]: version is not MAJOR.MINOR.PATCH")
            continue
        if previous is not None and version >= previous:
            problems.append(f"notes[{index}]: versions must be newest first without repeats")
        previous = version
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", str(note.get("date"))):
            problems.append(f"notes[{index}]: date must be YYYY-MM-DD")
        changes = note.get("changes")
        if not isinstance(changes, list) or not changes or not all(isinstance(c, str) and c.strip() for c in changes):
            problems.append(f"notes[{index}]: changes must be a non-empty list of texts")
    if isinstance(notes[0], dict) and notes[0].get("version") != data.get("version"):
        problems.append(f"notes[0] is {notes[0].get('version')}; the newest note must be version {data.get('version')}")
    return problems


def series_folder(version: str) -> str:
    major, minor, _ = parse_version(version)
    return f"{FOLDER_PREFIX}{major}.{minor}"


def newest(versions: list[str]) -> str:
    return max(versions, key=parse_version)


def carried_at_tag(repository: Path, tag: str) -> list[str]:
    """The engine versions a published launcher carried, read from its tag's Engines/ folder."""
    versions = []
    for name in git(repository, "ls-tree", "--name-only", f"{tag}:{ENGINES_FOLDER}").splitlines():
        path = f"{tag}:{ENGINES_FOLDER}/{name}/{MANIFEST_FILE}"
        if git_succeeds(repository, "cat-file", "-e", path):
            versions.append(json.loads(git(repository, "show", path))["version"])
    return versions


def new_engine_notes(engine_manifest: dict, previous: list[str]) -> list[dict]:
    """The notes users of the previous launcher have not seen: newer than the newest engine it carried below this
    version. Nothing when it carried this version; only the newest note when it carried nothing older."""
    current = parse_version(engine_manifest["version"])
    carried = [parse_version(version) for version in previous]
    if current in carried:
        return []
    older = [version for version in carried if version < current]
    if not older:
        return engine_manifest["notes"][:1]
    return [note for note in engine_manifest["notes"] if parse_version(note["version"]) > max(older)]


def release_notes(launcher: dict, engine_manifests: list[dict], previous: list[str]) -> str:
    version = launcher["version"]
    carried = ", ".join(sorted(launcher["engines"], key=parse_version, reverse=True))
    lines = []
    if parse_version(version)[0] == 0:
        lines += [PREVIEW_NOTE, ""]
    lines += [f"Launcher {version}, contains engine {carried}.", ""]
    lines += [f"- {change}" for change in launcher["notes"][0]["changes"]]
    for manifest in sorted(engine_manifests, key=lambda item: parse_version(item["version"]), reverse=True):
        for note in new_engine_notes(manifest, previous):
            lines += ["", f"Engine {note['version']}:", ""]
            lines += [f"- Breaking: {item['change']} Migrate: {item['migrate']}" for item in note.get("breaking", [])]
            lines += [f"- {change}" for change in note["changes"]]
    return "\n".join(lines) + "\n"


def release_files(releases: Path, product: str, version: str) -> list[Path]:
    """What a GitHub release holds: this version's installer and packages + the update feed; never older packages."""
    names = [setup_name(product, version), f"{product}-{version}-full.nupkg", *UPDATE_FILES]
    missing = [name for name in names if not (releases / name).is_file()]
    if missing:
        raise ReleaseFailed(f"{releases} is missing {', '.join(missing)}")
    files = [releases / name for name in names]
    delta = releases / f"{product}-{version}-delta.nupkg"
    return files + [delta] if delta.is_file() else files


def dirty_paths(repository: Path) -> list[str]:
    status = subprocess.run(["git", "-C", str(repository), "status", "--porcelain=v1", "--no-renames"],
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    if status.returncode != 0:
        raise ReleaseFailed(f"git status in {repository}: {status.stderr.strip()}")
    return [line[3:].strip().strip('"') for line in status.stdout.splitlines()]


def unexpected_changes(paths: list[str], allowed: tuple[str, ...]) -> list[str]:
    return [path for path in paths if not any(path == item or path.startswith(item + "/") for item in allowed)]


def check_branch(repository: Path, label: str) -> None:
    """On the release branch, with nothing on origin that is missing here."""
    branch = git(repository, "rev-parse", "--abbrev-ref", "HEAD")
    if branch != BRANCH:
        raise ReleaseFailed(f"{label} is on '{branch}'; switch to {BRANCH}: git -C {repository} switch {BRANCH}")
    git(repository, "fetch", "--tags", "origin")
    if git(repository, "rev-list", "--count", f"HEAD..origin/{BRANCH}") != "0":
        raise ReleaseFailed(f"{label} is behind origin; first: git -C {repository} pull --rebase")


def check_clean(repository: Path, label: str, allowed: tuple[str, ...]) -> None:
    changes = unexpected_changes(dirty_paths(repository), allowed)
    if changes:
        raise ReleaseFailed(f"{label} has changes a release would not commit; commit or stash them first:\n  "
                            + "\n  ".join(changes))


def remote_tag_commit(repository: Path, tag: str) -> str | None:
    listed = git(repository, "ls-remote", "--tags", "origin", f"refs/tags/{tag}", f"refs/tags/{tag}^{{}}")
    remote = {ref: sha for sha, ref in (line.split("\t") for line in listed.splitlines() if "\t" in line)}
    return remote.get(f"refs/tags/{tag}^{{}}", remote.get(f"refs/tags/{tag}"))


def latest_tag_version(repository: Path) -> tuple[int, int, int] | None:
    versions = [parse_version(tag[1:]) for tag in git(repository, "tag", "--list", "v*").splitlines()]
    return max((version for version in versions if version), default=None)


def tag_exists(repository: Path, tag: str) -> bool:
    return git_succeeds(repository, "rev-parse", "--verify", "--quiet", f"refs/tags/{tag}^{{commit}}")


def tag_and_push(repository: Path, tag: str, message: str) -> None:
    if not tag_exists(repository, tag):
        git(repository, "tag", "-a", tag, "-m", message)
    run(["git", "push", "origin", tag], repository)


def engine_release_message(version: str) -> str:
    return f"Release engine {version}"


def check_engine(engine: Path, version: str) -> None:
    from Tools.manifest import note_problems

    problems = note_problems(read_json(engine / MANIFEST_FILE))
    if problems:
        raise ReleaseFailed(f"{engine / MANIFEST_FILE}:\n  " + "\n  ".join(problems))
    check_branch(engine, "The engine")
    check_clean(engine, "The engine", (MANIFEST_FILE,))
    latest = latest_tag_version(engine)
    if latest is not None and parse_version(version) <= latest:
        raise ReleaseFailed(f"Engine version {version} is not newer than the latest tag v{'.'.join(map(str, latest))}")
    unpushed = git(engine, "log", "--format=%s", f"origin/{BRANCH}..HEAD").splitlines()
    extra = [subject for subject in unpushed if subject != engine_release_message(version)]
    if extra:
        raise ReleaseFailed("The engine has unpushed commits; push them yourself so the tag holds only what you "
                            "reviewed:\n  " + "\n  ".join(extra))


def release_engine(engine: Path, version: str, run_tests) -> None:
    run_tests()
    tag = f"v{version}"
    message = engine_release_message(version)
    if MANIFEST_FILE in dirty_paths(engine):
        git(engine, "add", MANIFEST_FILE)
        git(engine, "commit", "-m", message)
    run(["git", "push", "origin", BRANCH], engine)
    tag_and_push(engine, tag, message)
    print(f"Engine {version} released ({tag}).", flush=True)


@dataclass
class EnginesChange:
    imports: list[str] = field(default_factory=list)
    drops: list[str] = field(default_factory=list)

    def __bool__(self) -> bool:
        return bool(self.imports or self.drops)


def engines_change(wanted: list[str], engines: Path) -> EnginesChange:
    """What makes Engines/ hold exactly the wanted versions."""
    folders = {path.name for path in engines.glob(f"{FOLDER_PREFIX}*") if path.is_dir()}
    wanted_folders = {series_folder(version): version for version in wanted}
    imports = []
    for folder, version in wanted_folders.items():
        current = installed_version(engines / folder)
        if current is None or current < parse_version(version):
            imports.append(version)
        elif current > parse_version(version):
            raise ReleaseFailed(f"{folder} already holds {'.'.join(map(str, current))}, newer than the manifest's "
                                f"{version}")
    return EnginesChange(sorted(imports, key=parse_version), sorted(folders - set(wanted_folders)))


def apply_engines(engine: Path, engines: Path, change: EnginesChange, repository: Path) -> None:
    for folder in change.drops:
        print(f"Dropping {folder}", flush=True)
        shutil.rmtree(engines / folder)
    for version in change.imports:
        try:
            destination = import_engine(engine, engines, f"v{version}")
        except ImportFailed as error:
            raise ReleaseFailed(str(error)) from error
        print(f"Imported engine {version} into {destination}", flush=True)
    git(repository, "add", "-A", "--", ENGINES_FOLDER)


def engines_message(change: EnginesChange) -> str:
    parts = [f"Import engine {version}" for version in change.imports]
    parts += [f"drop {folder}" for folder in change.drops]
    text = "; ".join(parts)
    return text[0].upper() + text[1:]


def indexed_pointer(repository: Path) -> str:
    return git(repository, "ls-files", "--stage", "--", ENGINE_POINTER).split()[1]


def set_pointer(repository: Path, commit: str) -> None:
    git(repository, "update-index", "--add", "--cacheinfo", f"160000,{commit},{ENGINE_POINTER}")


def pointer_target(engine: Path, repository: Path, carried: list[str]) -> str:
    """The commit the launcher builds against: its pointer, moved up to the newest carried engine when behind it. A
    pointer already past that tag stays; `dev.py pin-engine` is how it moves further."""
    pointer = indexed_pointer(repository)
    tag_commit = git(engine, "rev-parse", f"v{newest(carried)}^{{commit}}")
    if git_succeeds(engine, "merge-base", "--is-ancestor", tag_commit, pointer):
        return pointer
    return tag_commit


def commit_launcher(engine: Path, repository: Path, engines: Path, launcher: dict) -> None:
    """Up to two commits: the engine copies (large and mechanical) and the version itself."""
    change = engines_change(launcher["engines"], engines)
    set_pointer(repository, pointer_target(engine, repository, launcher["engines"]))
    if change:
        apply_engines(engine, engines, change, repository)
        git(repository, "commit", "-m", engines_message(change))
    git(repository, "add", LAUNCHER_MANIFEST)
    if not git_succeeds(repository, "diff", "--cached", "--quiet"):
        git(repository, "commit", "-m", f"Release launcher {launcher['version']}")
    pointer = indexed_pointer(repository)
    if not git(engine, "branch", "-r", "--contains", pointer):
        raise ReleaseFailed(f"The launcher's engine pointer {pointer[:10]} is not pushed; push the engine first")


def prepare_worktree(engine: Path, path: Path, commit: str) -> None:
    """The engine exactly as the pointer says, whatever the shared checkout holds right now."""
    if (path / ".git").exists():
        git(path, "checkout", "--force", "--detach", commit)
        return
    git(engine, "worktree", "prune")
    path.parent.mkdir(parents=True, exist_ok=True)
    git(engine, "worktree", "add", "--force", "--detach", str(path), commit)


def repository_url(repository: Path) -> str:
    url = git(repository, "remote", "get-url", "origin")
    return url.removesuffix(".git")


def latest_release_tag(repository: Path) -> str | None:
    """Newest published GitHub release of the launcher (never a draft)."""
    listed = capture(["gh", "release", "list", "--repo", repository_url(repository), "--exclude-drafts", "--limit", "1",
                      "--json", "tagName"], repository)
    releases = json.loads(listed or "[]")
    return releases[0]["tagName"] if releases else None


def release_state(repository: Path, tag: str) -> str:
    """'published', 'draft' or 'none': a draft is what an interrupted upload leaves behind."""
    result = subprocess.run(["gh", "release", "view", tag, "--repo", repository_url(repository), "--json", "isDraft"],
                            cwd=repository, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        if "not found" in result.stderr.lower():
            return "none"
        raise ReleaseFailed(f"gh release view {tag}: {result.stderr.strip()}")
    return "draft" if json.loads(result.stdout)["isDraft"] else "published"


def restore_previous_package(repository: Path, releases: Path, product: str, previous_tag: str | None) -> None:
    """A delta package needs the previous full package; `dev.py clean` may have deleted it."""
    if previous_tag is None or (releases / f"{product}-{previous_tag[1:]}-full.nupkg").is_file():
        return
    releases.mkdir(parents=True, exist_ok=True)
    run(["vpk", "download", "github", "--repoUrl", repository_url(repository), "--pre", "--outputDir", str(releases)],
        repository)


def check_launcher_tools(repository: Path) -> None:
    for tool, hint in (("gh", "https://cli.github.com, then: gh auth login"),
                       ("vpk", "dotnet tool install -g vpk (Docs/Packaging.md)")):
        if shutil.which(tool) is None:
            raise ReleaseFailed(f"{tool} not found; install it: {hint}")
    capture(["gh", "auth", "status"], repository)
    wanted = read_json(repository / "Toolchain.lock.json")["libraries"]["velopack"]["version"]
    if wanted not in capture(["vpk", "--help"], repository):
        raise ReleaseFailed(f"vpk is not version {wanted}; run: dotnet tool update -g vpk --version {wanted}")
    if not (repository / "ToolCache" / "Toolchain.json").is_file():
        raise ReleaseFailed("ToolCache is missing; run: python Scripts/fetch_toolchain.py")


def check_launcher(repository: Path, engine: Path, launcher: dict, pending_engine: str | None) -> None:
    check_launcher_tools(repository)
    check_branch(repository, "The launcher")
    check_clean(repository, "The launcher", (LAUNCHER_MANIFEST, ENGINE_POINTER, ENGINES_FOLDER))
    latest = latest_tag_version(repository)
    tag = f"v{launcher['version']}"
    if latest is not None and parse_version(launcher["version"]) < latest:
        raise ReleaseFailed(f"Launcher version {launcher['version']} is older than the latest tag "
                            f"v{'.'.join(map(str, latest))}")
    for version in launcher["engines"]:
        if version != pending_engine and not tag_exists(engine, f"v{version}"):
            raise ReleaseFailed(f"{LAUNCHER_MANIFEST} carries engine {version}, which has no tag v{version}")
    if remote_tag_commit(repository, tag) not in (None, git(repository, "rev-parse", "HEAD")):
        raise ReleaseFailed(f"Tag {tag} is already on origin at another commit; raise the launcher's version")


def build_installer(repository: Path, worktree: Path, previous_tag: str | None, product: str, version: str,
                    notes: str) -> Path:
    """Tests and installer from the committed launcher, the pinned engine and these notes; reused while all stay."""
    package_dir = repository / "build" / "Package"
    releases = package_dir / "Releases"
    setup = releases / setup_name(product, version)
    stamp = {"version": version, "launcher": git(repository, "rev-parse", "HEAD"),
             "engine": git(worktree, "rev-parse", "HEAD"), "notes": notes}
    stamp_path = package_dir / STAMP_FILE
    if setup.is_file() and stamp_path.is_file() and read_json(stamp_path) == stamp:
        print(f"Installer is up to date: {setup}", flush=True)
        return setup
    package_dir.mkdir(parents=True, exist_ok=True)
    notes_file = package_dir / NOTES_FILE
    notes_file.write_text(notes, encoding="utf-8", newline="\n")
    dev = [sys.executable, str(repository / "Scripts" / "dev.py"), "--engine", str(worktree), "--build-dir",
           str(repository / "build" / RELEASE_BUILD)]
    run([*dev, "test"], repository)
    restore_previous_package(repository, releases, product, previous_tag)
    run([*dev, "package", "--yes", "--notes", str(notes_file)], repository)
    stamp_path.write_text(json.dumps(stamp) + "\n", encoding="utf-8", newline="\n")
    return setup


def confirmed(setup: Path) -> bool:
    print(f"\nInstaller ready, nothing of the launcher is published yet:\n  {setup}\n"
          "Install and try it: the launcher, Settings, `<engine> launcher version` (Docs/Packaging.md -> Try it).\n"
          "Installing closes programs running from the install folder.\n", flush=True)
    try:
        return input("Publish this release? [y/N] ").strip().lower() in ("y", "yes")
    except EOFError:
        return False


def publish(repository: Path, launcher: dict, product: str, setup: Path) -> None:
    version = launcher["version"]
    tag = f"v{version}"
    url = repository_url(repository)
    head = git(repository, "rev-parse", "HEAD")
    if tag_exists(repository, tag) and git(repository, "rev-parse", f"{tag}^{{commit}}") != head:
        raise ReleaseFailed(f"Tag {tag} exists at another commit; delete it or raise the version")
    run(["git", "push", "origin", BRANCH], repository)
    tag_and_push(repository, tag, f"Release launcher {version}")
    if release_state(repository, tag) == "draft":
        run(["gh", "release", "delete", tag, "--repo", url, "--yes"], repository)
    notes_file = setup.parents[1] / NOTES_FILE
    command = ["gh", "release", "create", tag, *map(str, release_files(setup.parent, product, version)), "--repo", url,
               "--verify-tag", "--draft", "--title", f"{product} {version}", "--notes-file", str(notes_file)]
    if parse_version(version)[0] == 0:
        command.append("--prerelease")
    run(command, repository)  # A draft until every file is up, so nobody downloads half a release
    run(["gh", "release", "edit", tag, "--repo", url, "--draft=false"], repository)
    print(f"Published: {url}/releases/tag/{tag}", flush=True)


def enclosing_project(repository: Path) -> Path | None:
    parent = git(repository, "rev-parse", "--show-superproject-working-tree")
    return Path(parent).resolve() if parent else None


def commit_project_pointers(project: Path, pointers: dict[str, str]) -> None:
    """Commits only these submodule pointers, leaving everything else in the project as it is. Never pushes: the
    project's branch can hold other work."""
    changed = [path for path in pointers if not git_succeeds(project, "diff", "--quiet", "HEAD", "--", path)]
    if not changed:
        return
    message = "Update " + " and ".join(f"{path} ({pointers[path]})" for path in changed)
    git(project, "commit", "-m", message, "--only", "--", *changed)
    print(f"{project.name}: {message} (committed, not pushed)", flush=True)


@dataclass(frozen=True)
class Plan:
    engine: str | None  # Engine version to release
    launcher: str | None  # Launcher version to release


def make_plan(engine_version: str, engine_tag_commit: str | None, launcher_version: str, launcher_state: str) -> Plan:
    return Plan(engine_version if engine_tag_commit is None else None,
                launcher_version if launcher_state != "published" else None)


def describe(plan: Plan, change: EnginesChange) -> str:
    lines = []
    if plan.engine:
        lines.append(f"Engine {plan.engine}: tests, commit the manifest, push, tag v{plan.engine}")
    if plan.launcher:
        if change.imports:
            lines.append(f"Launcher: import engine {', '.join(change.imports)}")
        if change.drops:
            lines.append(f"Launcher: drop {', '.join(change.drops)}")
        lines.append(f"Launcher {plan.launcher}: commit, tests, installer, wait for your confirmation, push, "
                     f"tag v{plan.launcher}, GitHub release")
    lines.append("Project: commit the submodule pointers if there is an enclosing project (no push)")
    return "\n".join(f"  {line}" for line in lines)


def release(engine: Path, repository: Path = REPOSITORY, dry_run: bool = False) -> int:
    engine_data = read_json(engine / MANIFEST_FILE)
    launcher = read_json(repository / LAUNCHER_MANIFEST)
    problems = manifest_problems(launcher)
    if problems:
        raise ReleaseFailed(f"{LAUNCHER_MANIFEST}:\n  " + "\n  ".join(problems))
    product, engine_version = engine_data["name"], engine_data["version"]
    engines = repository / ENGINES_FOLDER
    plan = make_plan(engine_version, remote_tag_commit(engine, f"v{engine_version}"), launcher["version"],
                     release_state(repository, f"v{launcher['version']}"))
    change = engines_change(launcher["engines"], engines)
    if not plan.launcher and change:
        raise ReleaseFailed(f"{LAUNCHER_MANIFEST} changes the carried engines, but launcher {launcher['version']} is "
                            "already published; raise its version and add a note")
    if not plan.engine and not plan.launcher:
        print(f"Nothing to release: engine {engine_version} and launcher {launcher['version']} are published.\n"
              f"Raise a version in {MANIFEST_FILE} or {LAUNCHER_MANIFEST} first.")
        return 0

    print("Plan:\n" + describe(plan, change), flush=True)
    if plan.engine and engine_version not in launcher["engines"]:
        print(f"Note: {LAUNCHER_MANIFEST} does not carry engine {engine_version}; users will not get it yet.")
    if plan.engine:
        check_engine(engine, plan.engine)
    if plan.launcher:
        check_launcher(repository, engine, launcher, plan.engine)
    if dry_run:
        print("Dry run: the checks passed and nothing was changed.")
        return 0

    pointers = {}
    if plan.engine:
        release_engine(engine, plan.engine, lambda: run([sys.executable, ".", "test"], engine))
        pointers[engine.name] = plan.engine
    if plan.launcher:
        previous_tag = latest_release_tag(repository)
        previous = carried_at_tag(repository, previous_tag) if previous_tag else []
        commit_launcher(engine, repository, engines, launcher)
        worktree = repository / "build" / RELEASE_ENGINE
        prepare_worktree(engine, worktree, indexed_pointer(repository))
        manifests = [read_json(engines / series_folder(version) / MANIFEST_FILE) for version in launcher["engines"]]
        notes = release_notes(launcher, manifests, previous)
        setup = build_installer(repository, worktree, previous_tag, product, plan.launcher, notes)
        if not confirmed(setup):
            print("Not published. The commits stay local; run the same command to continue.")
            return 0
        publish(repository, launcher, product, setup)
        pointers[repository.name] = plan.launcher
    project = enclosing_project(repository)
    if project is not None:
        commit_project_pointers(project, {path: version for path, version in pointers.items()
                                          if (project / path).resolve() in (engine.resolve(), repository.resolve())})
    return 0


def run_release(engine: Path, dry_run: bool) -> int:
    try:
        return release(engine, dry_run=dry_run)
    except (ReleaseFailed, PackageFailed) as error:
        print(f"error: {error}\nFix it and run the same command again; finished steps are not repeated.",
              file=sys.stderr)
        return 1
