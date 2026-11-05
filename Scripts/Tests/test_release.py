"""release: the manifests decide what is released; engine step and project pointers against local git repositories."""

import contextlib
import io
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from Scripts.import_engine import import_engine
from Scripts.release import (LAUNCHER_MANIFEST, REPOSITORY, ReleaseFailed, check_clean, commit_launcher,
                             commit_project_pointers, contract_problem, engine_release_body, engine_release_tags, make_plan,
                             manifest_problems, prepare_worktree, release_engine, release_files, release_notes,
                             remove_release_trees, unpackaged_engine_tags)
from Scripts.release import capture as real_capture

GIT_ENV = {"GIT_AUTHOR_NAME": "Test", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "Test",
           "GIT_COMMITTER_EMAIL": "t@t"}


def git(folder: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(folder), "-c", "protocol.file.allow=always", *args], check=True,
                          capture_output=True, text=True).stdout.strip()


def note(version: str, change: str = "x") -> dict:
    return {"version": version, "date": "2026-01-01", "changes": [change], "breaking": []}


def launcher(version: str = "1.1.0", engine: str = "0.3.1") -> dict:
    return {"version": version, "engine": engine, "toolsContracts": [1, 2],
            "notes": [note(version, "launcher change")]}


def engine_manifest(*versions: str) -> dict:
    return {"name": "MyEngine", "version": versions[0], "toolsContract": 1,
            "notes": [note(version, f"engine change {version}") for version in versions]}


class RulesTests(unittest.TestCase):
    def test_the_real_manifest_is_valid(self):
        data = json.loads((REPOSITORY / LAUNCHER_MANIFEST).read_text(encoding="utf-8"))
        self.assertEqual(manifest_problems(data), [])

    def test_a_version_without_its_note_is_rejected(self):
        data = launcher("1.2.0")
        data["version"] = "1.3.0"
        self.assertTrue(manifest_problems(data))

    def test_the_shipped_engine_must_be_an_exact_version(self):
        self.assertTrue(manifest_problems(launcher(engine="0.3")))

    def test_the_tools_contracts_are_listed(self):
        for contracts in (None, [], [0], ["2"], [True]):
            data = launcher()
            data["toolsContracts"] = contracts
            self.assertTrue(manifest_problems(data), contracts)

    def test_an_engine_needs_a_launcher_that_drives_its_contract(self):
        self.assertIsNone(contract_problem("0.4.0", 2, "launcher v1.1.0", launcher()))
        self.assertIn("raise the launcher", contract_problem("0.4.0", 3, "launcher v1.1.0", launcher()))
        older = launcher()
        del older["toolsContracts"]
        self.assertIn("does not list", contract_problem("0.4.0", 2, "launcher v1.0.0", older))
        self.assertIn("no launcher", contract_problem("0.4.0", 2, "no launcher", None))

    def test_a_note_needs_a_date_and_changes(self):
        data = launcher()
        data["notes"][0]["date"] = "soon"
        data["notes"][0]["changes"] = []
        self.assertEqual(len(manifest_problems(data)), 2)

    def test_notes_are_newest_first(self):
        data = launcher("1.0.0")
        data["notes"].append(note("1.1.0"))
        self.assertTrue(manifest_problems(data))

    def test_only_what_is_unreleased_is_planned(self):
        self.assertEqual(make_plan("0.4.0", None, "1.0.0", "published").engine, "0.4.0")
        self.assertIsNone(make_plan("0.4.0", "abc", "1.0.0", "published").engine)
        self.assertIsNone(make_plan("0.4.0", "abc", "1.0.0", "published").launcher)
        self.assertEqual(make_plan("0.4.0", "abc", "1.1.0", "none").launcher, "1.1.0")

    def test_an_interrupted_upload_is_released_again(self):
        self.assertEqual(make_plan("0.4.0", "abc", "1.1.0", "draft").launcher, "1.1.0")


class NotesTests(unittest.TestCase):
    def test_notes_are_only_the_changes_of_the_newest_note(self):
        data = launcher("1.1.0")
        data["notes"][0]["changes"] = ["First", "Second"]
        data["notes"].append(note("1.0.0", "older change"))
        self.assertEqual(release_notes(data), "- First\n- Second\n")


class TempFolderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="Release-", ignore_cleanup_errors=True)
        self.root = Path(self.temp.name)
        self.saved_env = {key: os.environ.get(key) for key in GIT_ENV}
        os.environ.update(GIT_ENV)

    def tearDown(self):
        for key, value in self.saved_env.items():
            if value is None:
                del os.environ[key]
            else:
                os.environ[key] = value
        self.temp.cleanup()

    def repository(self, name: str) -> Path:
        """A clone of a new bare origin, with one pushed commit."""
        origin = self.root / f"{name}.git"
        origin.mkdir()
        git(origin, "init", "--bare", "-b", "main")
        folder = self.root / name
        folder.mkdir()
        git(folder, "init", "-b", "main")
        git(folder, "remote", "add", "origin", str(origin))
        (folder / "EngineManifest.json").write_text("{}\n", newline="\n")
        git(folder, "add", "-A")
        git(folder, "commit", "-m", "first")
        git(folder, "push", "-u", "origin", "main")
        return folder


class ReleaseFilesTests(TempFolderTests):
    def write_release(self, *names: str) -> None:
        for name in names:
            (self.root / name).write_text("x")

    def test_older_packages_stay_out(self):
        self.write_release("P-Setup.exe", "P-1.1.0-full.nupkg", "P-1.1.0-delta.nupkg", "P-1.0.0-full.nupkg",
                           "P-win-Portable.zip", "releases.win.json", "RELEASES", "assets.win.json")
        names = {path.name for path in release_files(self.root, "P", "1.1.0")}
        self.assertEqual(names, {"P-Setup.exe", "P-1.1.0-full.nupkg", "P-1.1.0-delta.nupkg", "releases.win.json",
                                 "RELEASES", "assets.win.json"})

    def test_a_first_release_has_no_delta(self):
        self.write_release("P-Setup.exe", "P-1.0.0-full.nupkg", "releases.win.json", "RELEASES",
                           "assets.win.json")
        self.assertEqual(len(release_files(self.root, "P", "1.0.0")), 5)

    def test_a_missing_file_stops(self):
        self.write_release("P-Setup.exe")
        with self.assertRaises(ReleaseFailed):
            release_files(self.root, "P", "1.0.0")


class GitTests(TempFolderTests):
    def test_the_engine_release_commits_only_the_manifest_and_its_note_and_pushes_the_tag(self):
        engine = self.repository("engine")
        (engine / "EngineManifest.json").write_text('{"version": "0.4.0"}\n', newline="\n")
        (engine / "ReleaseNotes").mkdir()
        (engine / "ReleaseNotes" / "0.4.0.json").write_text("{}\n", newline="\n")
        (engine / "Other.txt").write_text("someone's work")
        ran = []
        release_engine(engine, "0.4.0", lambda: ran.append(True))
        self.assertEqual(ran, [True])
        self.assertEqual(git(engine, "log", "-1", "--format=%s"), "Release engine 0.4.0")
        self.assertEqual(sorted(git(engine, "show", "--name-only", "--format=", "HEAD").splitlines()),
                         ["EngineManifest.json", "ReleaseNotes/0.4.0.json"])
        self.assertEqual(git(engine, "cat-file", "-t", "v0.4.0"), "tag")
        self.assertIn("refs/tags/v0.4.0", git(engine, "ls-remote", "--tags", "origin"))
        self.assertEqual(git(engine, "rev-parse", "HEAD"), git(engine, "rev-parse", "origin/main"))

    def test_failing_engine_tests_leave_no_commit_or_tag(self):
        engine = self.repository("engine")
        (engine / "EngineManifest.json").write_text('{"version": "0.4.0"}\n', newline="\n")

        def failing():
            raise ReleaseFailed("tests failed")

        with self.assertRaises(ReleaseFailed):
            release_engine(engine, "0.4.0", failing)
        self.assertEqual(git(engine, "log", "-1", "--format=%s"), "first")
        self.assertEqual(git(engine, "tag", "--list"), "")

    def test_a_resumed_engine_release_reuses_its_commit_and_tag(self):
        engine = self.repository("engine")
        (engine / "EngineManifest.json").write_text('{"version": "0.4.0"}\n', newline="\n")
        release_engine(engine, "0.4.0", lambda: None)
        head = git(engine, "rev-parse", "HEAD")
        release_engine(engine, "0.4.0", lambda: None)
        self.assertEqual(git(engine, "rev-parse", "HEAD"), head)

    def test_changes_outside_the_allowed_paths_stop(self):
        engine = self.repository("engine")
        (engine / "Other.txt").write_text("someone's work")
        with self.assertRaisesRegex(ReleaseFailed, "Other.txt"):
            check_clean(engine, "The engine", ("EngineManifest.json",))
        check_clean(engine, "The engine", ("EngineManifest.json", "Other.txt"))

    def test_project_pointers_commit_leaves_other_work_alone(self):
        project = self.repository("project")
        self.repository("child")
        git(project, "-c", "protocol.file.allow=always", "submodule", "add", str(self.root / "child.git"), "Child")
        git(project, "commit", "-m", "add child")
        submodule = project / "Child"
        (submodule / "New.txt").write_text("x")
        git(submodule, "add", "-A")
        git(submodule, "commit", "-m", "child change")
        (project / "EngineManifest.json").write_text('{"other": "work"}\n', newline="\n")
        (project / "Staged.txt").write_text("staged")
        git(project, "add", "Staged.txt")
        commit_project_pointers(project, {"Child": "1.0.0"})
        self.assertEqual(git(project, "log", "-1", "--format=%s"), "Update Child (1.0.0)")
        self.assertEqual(git(project, "show", "--name-only", "--format=", "HEAD"), "Child")
        status = git(project, "status", "--porcelain")
        self.assertIn("EngineManifest.json", status)
        self.assertIn("A  Staged.txt", status)

    def engine_release(self, engine: Path, version: str) -> str:
        data = engine_manifest(version)
        (engine / "EngineManifest.json").write_text(json.dumps(data), newline="\n")
        git(engine, "add", "-A")
        git(engine, "commit", "-m", version)
        git(engine, "tag", "-a", f"v{version}", "-m", version)
        git(engine, "push", "origin", "main", f"v{version}")
        return git(engine, "rev-parse", "HEAD")

    def launcher_repository(self, carried: str) -> Path:
        """A launcher that ships `carried`."""
        repository = self.repository("launcher")
        (repository / LAUNCHER_MANIFEST).write_text(json.dumps(launcher("1.0.0", carried)), newline="\n")
        git(repository, "add", "-A")
        git(repository, "commit", "-m", "start")
        return repository

    def test_the_launcher_step_commits_the_version(self):
        repository = self.launcher_repository("0.3.0")
        data = launcher("1.1.0", "0.4.0")
        (repository / LAUNCHER_MANIFEST).write_text(json.dumps(data), newline="\n")
        commit_launcher(repository, data)
        self.assertEqual(git(repository, "log", "-1", "--format=%s"), "Release launcher 1.1.0")
        self.assertEqual(git(repository, "status", "--porcelain"), "")

    def test_the_launcher_step_repeated_changes_nothing(self):
        repository = self.launcher_repository("0.3.0")
        head = git(repository, "rev-parse", "HEAD")
        data = json.loads((repository / LAUNCHER_MANIFEST).read_text())
        commit_launcher(repository, data)
        self.assertEqual(git(repository, "rev-parse", "HEAD"), head)

    def test_tags_are_listed_oldest_first_without_other_names(self):
        engine = self.repository("engine")
        for version in ("0.10.0", "0.9.0"):
            self.engine_release(engine, version)
        git(engine, "tag", "vnext")
        self.assertEqual(engine_release_tags(engine), ["v0.9.0", "v0.10.0"])

    def test_a_record_release_says_what_changed(self):
        data = engine_manifest("0.3.0")
        data["notes"][0]["breaking"] = [{"change": "A renamed.", "migrate": "Use B."}]
        self.assertEqual(engine_release_body(data), "- Breaking: A renamed. Migrate: Use B.\n- engine change 0.3.0\n")

    def test_unchanged_pointers_make_no_commit(self):
        project = self.repository("project")
        head = git(project, "rev-parse", "HEAD")
        commit_project_pointers(project, {"EngineManifest.json": "1.0.0"})
        self.assertEqual(git(project, "rev-parse", "HEAD"), head)


class EnginePackageTests(TempFolderTests):
    def tagged_engine(self) -> Path:
        engine = self.repository("engine")
        git(engine, "tag", "v0.1.0")
        command = engine / "Tools" / "commands" / "prebuilt.py"
        command.parent.mkdir(parents=True)
        command.write_text("", newline="\n")
        git(engine, "add", "-A")
        git(engine, "commit", "-m", "command")
        for tag in ("v0.2.0", "v0.3.0"):
            git(engine, "tag", tag)
        return engine

    def missing(self, engine: Path, assets: dict[str, list[str]]) -> list[str]:
        def view(command: list[str], cwd: Path) -> str:
            if command[:3] == ["gh", "release", "view"]:
                return json.dumps({"assets": [{"name": name} for name in assets[command[3]]]})
            return real_capture(command, cwd)

        with (patch("Scripts.release.capture", view), patch("Scripts.release.repository_url", lambda _: "url"),
              patch("Scripts.release.release_state", lambda _, tag: "published" if tag in assets else None)):
            return unpackaged_engine_tags(engine)

    def test_only_a_published_tag_with_the_command_and_no_package_needs_one(self):
        engine = self.tagged_engine()
        self.assertEqual(self.missing(engine, {"v0.1.0": [], "v0.2.0": ["EngineCopy.json"]}), ["v0.2.0"])

    def test_a_release_holding_the_package_needs_none(self):
        engine = self.tagged_engine()
        self.assertEqual(self.missing(engine, {"v0.2.0": ["EngineCopy.json", "EnginePrebuilt.zip"]}), [])


class ReleaseTreesTests(TempFolderTests):
    def published_trees(self, engine: Path) -> Path:
        """A launcher folder holding what a finished release leaves under build/."""
        repository = self.root / "launcher"
        package = repository / "build" / "Package"
        for folder in (repository / "build" / "ReleaseBuild", package / "Stage", package / "Releases"):
            folder.mkdir(parents=True)
        (package / "Releases" / "MyEngine-1.0.0-full.nupkg").write_text("package")
        (package / "Release.json").write_text("{}")
        prepare_worktree(engine, repository / "build" / "ReleaseEngine", git(engine, "rev-parse", "HEAD"))
        return repository

    def test_a_published_release_removes_its_trees_and_keeps_its_packages(self):
        engine = self.repository("engine")
        repository = self.published_trees(engine)
        remove_release_trees(repository, engine)
        build = repository / "build"
        self.assertEqual(sorted(path.name for path in build.iterdir()), ["Package"])
        self.assertEqual(sorted(path.name for path in (build / "Package").iterdir()), ["Release.json", "Releases"])
        self.assertTrue((build / "Package" / "Releases" / "MyEngine-1.0.0-full.nupkg").is_file())
        self.assertNotIn("ReleaseEngine", git(engine, "worktree", "list"))

    def test_the_next_release_recreates_the_worktree(self):
        engine = self.repository("engine")
        repository = self.published_trees(engine)
        remove_release_trees(repository, engine)
        worktree = repository / "build" / "ReleaseEngine"
        prepare_worktree(engine, worktree, git(engine, "rev-parse", "HEAD"))
        self.assertEqual(git(worktree, "rev-parse", "HEAD"), git(engine, "rev-parse", "HEAD"))

    def test_nothing_to_remove_is_not_an_error(self):
        engine = self.repository("engine")
        remove_release_trees(self.root / "empty", engine)

    def test_a_failed_removal_only_warns(self):
        engine = self.repository("engine")
        repository = self.published_trees(engine)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            remove_release_trees(repository, self.root / "not-a-repository")
        self.assertIn("warning:", output.getvalue())


if __name__ == "__main__":
    unittest.main()
