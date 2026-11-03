"""Publishes what the manifests ask for: engine tag, launcher installer, project pointers (Docs/Packaging.md).

Usage, from the launcher repository:
    python Scripts/dev.py release [--dry-run]

The manifests are the desired state. A version without its pushed tag (engine) or its published GitHub release
(launcher) is released; everything else is left alone, so a run that stopped halfway continues when run again:

    EngineManifest.json    version + notes
    LauncherManifest.json  version + notes + engine (the exact engine version the installer ships)

Every engine tag also gets a GitHub release holding its EngineCopy.json: that is what the launcher lists and checks
its downloads against (Docs/Engines.md).

Steps: check -> engine (tests, commit, push, tag, record) -> launcher (commit) -> release notes -> tests
and installer, built in a worktree at the tag of the engine it ships -> you try the installer and confirm -> push, tag,
GitHub release -> the enclosing project's submodule pointers, committed but not pushed.
The same notes go into the package (`vpk pack --releaseNotes`, read back from releases.win.json) and the GitHub release.
Only the standard library; the engine's manifest rules come from the engine checkout dev.py puts on sys.path.
"""

import json
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from engine_location import MANIFEST_FILE, REPOSITORY  # noqa: E402
from import_engine import COPY_FILE, NOTES_FOLDER, ImportFailed, engine_copy, parse_version  # noqa: E402
from package import LAUNCHER_MANIFEST, PREBUILT_ASSET, PackageFailed, setup_name  # noqa: E402

BRANCH = "main"
RELEASE_ENGINE = "ReleaseEngine"  # build/<this>/: worktree of the engine commit the release is built against
RELEASE_BUILD = "ReleaseBuild"  # build/<this>/: its own build tree, so development builds keep theirs
RELEASE_PACKAGE = "ReleasePackage"  # build/<this>/: worktree of the tag a compiled engine is made from; removed after
PREBUILT_COMMAND = "Tools/commands/prebuilt.py"  # An engine tag that can compile itself into a package
STAGE_FOLDER = "Stage"  # build/Package/<this>/: what the package step installs into current/
STAMP_FILE = "Release.json"  # build/Package/<this>: what the installer in Releases/ was built from
NOTES_FILE = "ReleaseNotes.md"  # build/Package/<this>: the notes of the package and of the GitHub release
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
    `engine` is one exact version."""
    problems = []
    if parse_version(str(data.get("version"))) is None:
        problems.append(f"version '{data.get('version')}' is not MAJOR.MINOR.PATCH")
    if parse_version(str(data.get("engine"))) is None:
        problems.append(f"engine '{data.get('engine')}' is not MAJOR.MINOR.PATCH")
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


def release_notes(launcher: dict) -> str:
    """Exactly the changes written in the manifest's newest note; nothing is added to them."""
    return "\n".join(f"- {change}" for change in launcher["notes"][0]["changes"]) + "\n"


def release_files(releases: Path, product: str, version: str) -> list[Path]:
    """What a GitHub release holds: this version's installer and packages + the update feed; never older packages."""
    names = [setup_name(product), f"{product}-{version}-full.nupkg", *UPDATE_FILES]
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


def engine_note_file(version: str) -> str:
    return f"{NOTES_FOLDER}/{version}.json"


def check_engine(engine: Path, version: str) -> None:
    from Tools.manifest import load_notes, note_problems

    problems = note_problems(read_json(engine / MANIFEST_FILE), load_notes(engine))
    if problems:
        raise ReleaseFailed(f"{engine / MANIFEST_FILE}:\n  " + "\n  ".join(problems))
    check_branch(engine, "The engine")
    check_clean(engine, "The engine", (MANIFEST_FILE, engine_note_file(version)))
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
    git(engine, "add", MANIFEST_FILE)
    if (engine / engine_note_file(version)).is_file():
        git(engine, "add", engine_note_file(version))
    if not git_succeeds(engine, "diff", "--cached", "--quiet"):
        git(engine, "commit", "-m", message)
    run(["git", "push", "origin", BRANCH], engine)
    tag_and_push(engine, tag, message)
    print(f"Engine {version} released ({tag}).", flush=True)


def commit_launcher(repository: Path, launcher: dict) -> None:
    git(repository, "add", LAUNCHER_MANIFEST)
    if not git_succeeds(repository, "diff", "--cached", "--quiet"):
        git(repository, "commit", "-m", f"Release launcher {launcher['version']}")


def prepare_worktree(engine: Path, path: Path, commit: str) -> None:
    """The engine exactly as the tag says, whatever the shared checkout holds right now."""
    if (path / ".git").exists():
        git(path, "checkout", "--force", "--detach", commit)
        return
    git(engine, "worktree", "prune")
    path.parent.mkdir(parents=True, exist_ok=True)
    git(engine, "worktree", "add", "--force", "--detach", str(path), commit)


def remove_release_trees(repository: Path, engine: Path) -> None:
    """After a publish the next release recreates these; build/Package/Releases stays for its delta. The release is
    already out, so a failure here only warns."""
    build = repository / "build"
    worktree = build / RELEASE_ENGINE
    try:
        if (worktree / ".git").exists():
            git(engine, "worktree", "remove", "--force", str(worktree))
        for folder in (build / RELEASE_BUILD, build / "Package" / STAGE_FOLDER):
            if folder.exists():
                shutil.rmtree(folder)
    except (ReleaseFailed, OSError) as error:
        print(f"warning: could not remove the release's temporary folders under {build}: {error}", flush=True)


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
    check_clean(repository, "The launcher", (LAUNCHER_MANIFEST,))
    latest = latest_tag_version(repository)
    tag = f"v{launcher['version']}"
    if latest is not None and parse_version(launcher["version"]) < latest:
        raise ReleaseFailed(f"Launcher version {launcher['version']} is older than the latest tag "
                            f"v{'.'.join(map(str, latest))}")
    version = launcher["engine"]
    if version != pending_engine and not tag_exists(engine, f"v{version}"):
        raise ReleaseFailed(f"{LAUNCHER_MANIFEST} ships engine {version}, which has no tag v{version}")
    if remote_tag_commit(repository, tag) not in (None, git(repository, "rev-parse", "HEAD")):
        raise ReleaseFailed(f"Tag {tag} is already on origin at another commit; raise the launcher's version")


def build_installer(repository: Path, worktree: Path, previous_tag: str | None, product: str, version: str,
                    notes: str) -> Path:
    """Tests and installer from the committed launcher, the shipped engine and these notes; reused while all stay."""
    package_dir = repository / "build" / "Package"
    releases = package_dir / "Releases"
    setup = releases / setup_name(product)
    stamp ={"version": version, "launcher": git(repository, "rev-parse", "HEAD"),
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
    run([*dev, "update-test"], repository)  # A release whose update is broken strands its users on it
    run([*dev, "engine-test"], repository)  # Moving projects between engines goes through this launcher's code
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
    run(command, repository)  # A draft until every file is up, so nobody downloads half a release
    run(["gh", "release", "edit", tag, "--repo", url, "--draft=false"], repository)
    print(f"Published: {url}/releases/tag/{tag}", flush=True)


def engine_release_tags(engine: Path) -> list[str]:
    """Every engine version tag, oldest first."""
    tags = [tag for tag in git(engine, "tag", "--list", "v*").splitlines() if parse_version(tag[1:])]
    return sorted(tags, key=lambda tag: parse_version(tag[1:]))


def unpublished_engine_tags(engine: Path) -> list[str]:
    """Tags whose GitHub release (the record of their files) is missing or an interrupted draft."""
    listed = capture(["gh", "release", "list", "--repo", repository_url(engine), "--exclude-drafts", "--limit", "1000",
                      "--json", "tagName"], engine)
    published = {release["tagName"] for release in json.loads(listed or "[]")}
    return [tag for tag in engine_release_tags(engine) if tag not in published]


def engine_release_body(manifest: dict) -> str:
    note = manifest["notes"][0]
    lines = [f"- Breaking: {item['change']} Migrate: {item['migrate']}" for item in note.get("breaking", [])]
    return "\n".join(lines + [f"- {change}" for change in note["changes"]]) + "\n"


def publish_engine_release(engine: Path, tag: str) -> None:
    """A draft until the record is up, so nothing reads half a release; with immutable releases on, it never changes."""
    try:
        _, manifest, record = engine_copy(engine, tag)
    except ImportFailed as error:
        raise ReleaseFailed(str(error)) from error
    url = repository_url(engine)
    if release_state(engine, tag) == "draft":
        run(["gh", "release", "delete", tag, "--repo", url, "--yes"], engine)
    with tempfile.TemporaryDirectory() as folder:
        file = Path(folder) / COPY_FILE
        file.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8", newline="\n")
        body = Path(folder) / "Notes.md"
        body.write_text(engine_release_body(manifest), encoding="utf-8", newline="\n")
        command = ["gh", "release", "create", tag, str(file), "--repo", url, "--verify-tag", "--draft", "--title",
                   f"{manifest['name']} {manifest['version']}", "--notes-file", str(body)]
        if parse_version(manifest["version"])[0] == 0:
            command.append("--prerelease")
        run(command, engine)
    run(["gh", "release", "edit", tag, "--repo", url, "--draft=false"], engine)


def unpackaged_engine_tags(engine: Path) -> list[str]:
    """Published tags that can compile themselves (they have the engine's prebuilt command) and whose release lacks it."""
    missing = []
    for tag in engine_release_tags(engine):
        if not git_succeeds(engine, "cat-file", "-e", f"{tag}:{PREBUILT_COMMAND}") or release_state(engine, tag) is None:
            continue
        listed = capture(["gh", "release", "view", tag, "--repo", repository_url(engine), "--json", "assets"], engine)
        if PREBUILT_ASSET not in {asset["name"] for asset in json.loads(listed)["assets"]}:
            missing.append(tag)
    return missing


def publish_engine_package(engine: Path, tag: str) -> None:
    """Compiles the tag in its own worktree, so the package holds exactly what a project's copy of that tag holds. The
    launcher downloads it with GitHub's digest (Docs/Engines.md)."""
    worktree = REPOSITORY / "build" / RELEASE_PACKAGE
    prepare_worktree(engine, worktree, git(engine, "rev-parse", f"{tag}^{{commit}}"))
    try:
        with tempfile.TemporaryDirectory() as folder:
            package = Path(folder) / "Package"
            run([sys.executable, str(worktree), "prebuilt", "--output", str(package)], worktree)
            archive = shutil.make_archive(str(Path(folder) / Path(PREBUILT_ASSET).stem), "zip", package)
            run(["gh", "release", "upload", tag, archive, "--repo", repository_url(engine)], engine)
    finally:
        git(engine, "worktree", "remove", "--force", str(worktree))


def publish_engine_releases(engine: Path) -> None:
    for tag in unpublished_engine_tags(engine):
        publish_engine_release(engine, tag)
        print(f"Engine record published: {tag}", flush=True)
    for tag in unpackaged_engine_tags(engine):
        publish_engine_package(engine, tag)
        print(f"Compiled engine published: {tag}", flush=True)


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


def describe(plan: Plan, records: list[str]) -> str:
    lines = []
    if plan.engine:
        lines.append(f"Engine {plan.engine}: tests, commit the manifest, push, tag v{plan.engine}, publish its record")
    if records:
        lines.append(f"Engine records to publish: {', '.join(records)}")
    if plan.launcher:
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
    plan = make_plan(engine_version, remote_tag_commit(engine, f"v{engine_version}"), launcher["version"],
                     release_state(repository, f"v{launcher['version']}"))
    records = unpublished_engine_tags(engine)
    if not plan.engine and not plan.launcher and not records:
        print(f"Nothing to release: engine {engine_version} and launcher {launcher['version']} are published.\n"
              f"Raise a version in {MANIFEST_FILE} or {LAUNCHER_MANIFEST} first.")
        return 0

    print("Plan:\n" + describe(plan, records), flush=True)
    if plan.engine and engine_version != launcher["engine"]:
        print(f"Note: {LAUNCHER_MANIFEST} ships engine {launcher['engine']}; new projects are offered {engine_version} "
              "online.")
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
    publish_engine_releases(engine)
    if plan.launcher:
        previous_tag = latest_release_tag(repository)
        commit_launcher(repository, launcher)
        worktree = repository / "build" / RELEASE_ENGINE
        prepare_worktree(engine, worktree, git(engine, "rev-parse", f"v{launcher['engine']}^{{commit}}"))
        setup = build_installer(repository, worktree, previous_tag, product, plan.launcher, release_notes(launcher))
        if not confirmed(setup):
            print("Not published. The commits stay local; run the same command to continue.")
            return 0
        publish(repository, launcher, product, setup)
        remove_release_trees(repository, engine)
        pointers[repository.name] = plan.launcher
    project = enclosing_project(repository)
    if project is not None:
        commit_project_pointers(project, {path: version for path, version in pointers.items()
                                          if (project / path).resolve() in (engine.resolve(), repository.resolve())})
    return 0


def run_engine_releases(engine: Path, dry_run: bool) -> int:
    """Publishes the records a tag lacks without releasing anything else; the first run fills in the old tags."""
    try:
        tags = unpublished_engine_tags(engine)
        print("Engine records to publish: " + (", ".join(tags) or "none"), flush=True)
        print("Compiled engines to publish: " + (", ".join(unpackaged_engine_tags(engine)) or "none"), flush=True)
        for tag in tags:
            engine_copy(engine, tag)  # Every tag can be read before anything is published
        if not dry_run:
            publish_engine_releases(engine)
        return 0
    except (ReleaseFailed, ImportFailed) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


def run_release(engine: Path, dry_run: bool) -> int:
    try:
        return release(engine, dry_run=dry_run)
    except (ReleaseFailed, PackageFailed) as error:
        print(f"error: {error}\nFix it and run the same command again; finished steps are not repeated.",
              file=sys.stderr)
        return 1
