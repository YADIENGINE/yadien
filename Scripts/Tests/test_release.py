"""release: the manifests decide what is released; engine step and project pointers against local git repositories."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from Scripts.import_engine import import_engine
from Scripts.release import (LAUNCHER_MANIFEST, REPOSITORY, ReleaseFailed, carried_at_tag, check_clean, commit_launcher,
                             commit_project_pointers, engine_release_body, engine_release_tags, indexed_pointer,
                             make_plan, manifest_problems, new_engine_notes, release_engine, release_files,
                             release_notes, set_pointer)

GIT_ENV = {"GIT_AUTHOR_NAME": "Test", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "Test",
           "GIT_COMMITTER_EMAIL": "t@t"}


def git(folder: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(folder), "-c", "protocol.file.allow=always", *args], check=True,
                          capture_output=True, text=True).stdout.strip()


def note(version: str, change: str = "x") -> dict:
    return {"version": version, "date": "2026-01-01", "changes": [change], "breaking": []}


def launcher(version: str = "1.1.0", engine: str = "0.3.1") -> dict:
    return {"version": version, "engine": engine, "notes": [note(version, "launcher change")]}


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
    def test_a_new_patch_lists_only_notes_after_what_was_carried(self):
        notes = new_engine_notes(engine_manifest("0.4.0", "0.3.1", "0.3.0"), ["0.3.0"])
        self.assertEqual([item["version"] for item in notes], ["0.4.0", "0.3.1"])

    def test_an_engine_carried_before_adds_no_notes(self):
        self.assertEqual(new_engine_notes(engine_manifest("0.3.0"), ["0.3.0"]), [])

    def test_a_first_release_lists_only_the_newest_note(self):
        notes = new_engine_notes(engine_manifest("0.3.0", "0.2.0"), [])
        self.assertEqual([item["version"] for item in notes], ["0.3.0"])

    def test_notes_hold_launcher_changes_engines_and_breaking_changes(self):
        manifest = engine_manifest("0.3.1", "0.3.0")
        manifest["notes"][0]["breaking"] = [{"change": "Renamed A.", "migrate": "Use B."}]
        text = release_notes(launcher("0.5.0"), manifest, ["0.3.0"])
        self.assertIn("Early preview", text)
        self.assertIn("contains engine 0.3.1", text)
        self.assertIn("- launcher change", text)
        self.assertIn("- engine change 0.3.1", text)
        self.assertIn("- Breaking: Renamed A. Migrate: Use B.", text)
        self.assertNotIn("engine change 0.3.0", text)

    def test_a_stable_version_has_no_preview_line(self):
        self.assertNotIn("Early preview", release_notes(launcher("1.0.0"), engine_manifest("0.3.1"), ["0.3.1"]))


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
        self.write_release("P-Setup-1.1.0.exe", "P-1.1.0-full.nupkg", "P-1.1.0-delta.nupkg", "P-1.0.0-full.nupkg",
                           "P-win-Portable.zip", "releases.win.json", "RELEASES", "assets.win.json")
        names = {path.name for path in release_files(self.root, "P", "1.1.0")}
        self.assertEqual(names, {"P-Setup-1.1.0.exe", "P-1.1.0-full.nupkg", "P-1.1.0-delta.nupkg", "releases.win.json",
                                 "RELEASES", "assets.win.json"})

    def test_a_first_release_has_no_delta(self):
        self.write_release("P-Setup-1.0.0.exe", "P-1.0.0-full.nupkg", "releases.win.json", "RELEASES",
                           "assets.win.json")
        self.assertEqual(len(release_files(self.root, "P", "1.0.0")), 5)

    def test_a_missing_file_stops(self):
        self.write_release("P-Setup-1.0.0.exe")
        with self.assertRaises(ReleaseFailed):
            release_files(self.root, "P", "1.0.0")


class GitTests(TempFolderTests):
    def test_the_engine_release_commits_only_the_manifest_and_pushes_the_tag(self):
        engine = self.repository("engine")
        (engine / "EngineManifest.json").write_text('{"version": "0.4.0"}\n', newline="\n")
        ran = []
        release_engine(engine, "0.4.0", lambda: ran.append(True))
        self.assertEqual(ran, [True])
        self.assertEqual(git(engine, "log", "-1", "--format=%s"), "Release engine 0.4.0")
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

    def test_carried_engines_are_read_from_the_tag(self):
        repository = self.repository("launcher")
        copy = repository / "Engines" / "Engine0.3"
        copy.mkdir(parents=True)
        (copy / "EngineManifest.json").write_text('{"version": "0.3.1"}\n', newline="\n")
        git(repository, "add", "-A")
        git(repository, "commit", "-m", "copy")
        git(repository, "tag", "v1.0.0")
        (copy / "EngineManifest.json").write_text('{"version": "0.3.2"}\n', newline="\n")
        self.assertEqual(carried_at_tag(repository, "v1.0.0"), ["0.3.1"])

    def test_a_shipped_engine_is_read_from_the_tags_manifest(self):
        repository = self.repository("launcher")
        (repository / LAUNCHER_MANIFEST).write_text('{"engine": "0.4.0"}\n', newline="\n")
        git(repository, "add", "-A")
        git(repository, "commit", "-m", "manifest")
        git(repository, "tag", "v1.0.0")
        self.assertEqual(carried_at_tag(repository, "v1.0.0"), ["0.4.0"])

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

    def launcher_repository(self, engine: Path, carried: str) -> Path:
        """A launcher that ships `carried`, with its Engine pointer at that tag."""
        repository = self.repository("launcher")
        (repository / "Engine").mkdir()  # An uninitialized submodule's empty folder
        (repository / LAUNCHER_MANIFEST).write_text(json.dumps(launcher("1.0.0", carried)), newline="\n")
        git(repository, "add", "-A")
        set_pointer(repository, git(engine, "rev-parse", f"v{carried}^{{commit}}"))
        git(repository, "commit", "-m", "start")
        return repository

    def test_the_launcher_step_commits_the_version_and_moves_the_pointer(self):
        engine = self.repository("engine")
        self.engine_release(engine, "0.3.0")
        repository = self.launcher_repository(engine, "0.3.0")
        new_tag = self.engine_release(engine, "0.4.0")
        data = launcher("1.1.0", "0.4.0")
        (repository / LAUNCHER_MANIFEST).write_text(json.dumps(data), newline="\n")
        commit_launcher(engine, repository, data)
        self.assertEqual(git(repository, "log", "-1", "--format=%s"), "Release launcher 1.1.0")
        self.assertEqual(indexed_pointer(repository), new_tag)
        self.assertEqual(git(repository, "status", "--porcelain"), "")

    def test_the_launcher_step_repeated_changes_nothing(self):
        engine = self.repository("engine")
        self.engine_release(engine, "0.3.0")
        repository = self.launcher_repository(engine, "0.3.0")
        head = git(repository, "rev-parse", "HEAD")
        data = json.loads((repository / LAUNCHER_MANIFEST).read_text())
        commit_launcher(engine, repository, data)
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

    def test_a_pointer_past_the_carried_engine_stays(self):
        engine = self.repository("engine")
        self.engine_release(engine, "0.3.0")
        repository = self.launcher_repository(engine, "0.3.0")
        (engine / "Later.txt").write_text("x")
        git(engine, "add", "-A")
        git(engine, "commit", "-m", "later")
        git(engine, "push", "origin", "main")
        later = git(engine, "rev-parse", "HEAD")
        set_pointer(repository, later)
        data = launcher("1.1.0", "0.3.0")
        (repository / LAUNCHER_MANIFEST).write_text(json.dumps(data), newline="\n")
        commit_launcher(engine, repository, data)
        self.assertEqual(indexed_pointer(repository), later)

    def test_an_unpushed_pointer_stops(self):
        engine = self.repository("engine")
        self.engine_release(engine, "0.3.0")
        repository = self.launcher_repository(engine, "0.3.0")
        (engine / "Later.txt").write_text("x")
        git(engine, "add", "-A")
        git(engine, "commit", "-m", "not pushed")
        set_pointer(repository, git(engine, "rev-parse", "HEAD"))
        data = launcher("1.1.0", "0.3.0")
        with self.assertRaisesRegex(ReleaseFailed, "not pushed"):
            commit_launcher(engine, repository, data)

    def test_unchanged_pointers_make_no_commit(self):
        project = self.repository("project")
        head = git(project, "rev-parse", "HEAD")
        commit_project_pointers(project, {"EngineManifest.json": "1.0.0"})
        self.assertEqual(git(project, "rev-parse", "HEAD"), head)


if __name__ == "__main__":
    unittest.main()
