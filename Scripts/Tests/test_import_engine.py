"""import_engine: a pushed tag becomes Engines/Engine<series>/ with its record; development files stay out."""

import hashlib
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from Scripts.import_engine import COPY_FILE, ImportFailed, import_engine

GIT_ENV = os.environ | {"GIT_AUTHOR_NAME": "Test", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "Test",
                        "GIT_COMMITTER_EMAIL": "t@t"}


def git(folder: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(folder), *args], check=True, capture_output=True, text=True,
                          env=GIT_ENV).stdout.strip()


def manifest(version: str) -> str:
    return json.dumps({"name": "TestEngine", "version": version, "toolsContract": 1,
                       "notes": [{"version": version, "date": "2026-01-01", "changes": ["x"], "breaking": []}]})


class ImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ImportEngine-")
        root = Path(self.temp.name)
        self.origin, self.engine, self.engines = root / "origin.git", root / "engine", root / "Engines"
        self.origin.mkdir()
        git(self.origin, "init", "--bare", "-b", "main")
        self.engine.mkdir()
        git(self.engine, "init", "-b", "main")
        git(self.engine, "remote", "add", "origin", str(self.origin))
        (self.engine / ".gitattributes").write_text("* text=auto eol=lf\n/CLAUDE.md export-ignore\n"
                                                    "/Docs/Design export-ignore\n", newline="\n")
        (self.engine / "CLAUDE.md").write_text("engine rules\n")
        (self.engine / "Docs" / "Design").mkdir(parents=True)
        (self.engine / "Docs" / "Design" / "Plan.md").write_text("plans\n")
        (self.engine / "Core").mkdir()
        (self.engine / "Core" / "A.h").write_text("#pragma once\n", newline="\n")
        self.release("0.2.0")

    def tearDown(self):
        self.temp.cleanup()

    def release(self, version: str, push: bool = True) -> None:
        (self.engine / "EngineManifest.json").write_text(manifest(version), newline="\n")
        git(self.engine, "add", "-A")
        git(self.engine, "commit", "-m", version)
        git(self.engine, "tag", "-a", f"v{version}", "-m", version)
        if push:
            git(self.engine, "push", "origin", "main", f"v{version}")

    def test_copy_holds_the_tag_without_development_files(self):
        folder = import_engine(self.engine, self.engines, "v0.2.0")
        self.assertEqual(folder.name, "Engine0.2")
        self.assertTrue((folder / "Core" / "A.h").is_file())
        self.assertFalse((folder / "CLAUDE.md").exists())
        self.assertFalse((folder / "Docs").exists())
        record = json.loads((folder / COPY_FILE).read_text(encoding="utf-8"))
        self.assertEqual(record["source"], {"tag": "v0.2.0", "commit": git(self.engine, "rev-parse", "v0.2.0^{commit}")})
        for path, digest in record["files"].items():
            self.assertEqual(digest, "sha256:" + hashlib.sha256((folder / path).read_bytes()).hexdigest())
        self.assertNotIn(COPY_FILE, record["files"])

    def test_uncommitted_work_never_enters_the_copy(self):
        (self.engine / "Core" / "A.h").write_text("work in progress\n")
        folder = import_engine(self.engine, self.engines, "v0.2.0")
        self.assertEqual((folder / "Core" / "A.h").read_text(), "#pragma once\n")

    def test_a_patch_replaces_its_series_but_never_goes_back(self):
        import_engine(self.engine, self.engines, "v0.2.0")
        self.release("0.2.1")
        folder = import_engine(self.engine, self.engines, "v0.2.1")
        self.assertIn("0.2.1", (folder / "EngineManifest.json").read_text())
        with self.assertRaises(ImportFailed):
            import_engine(self.engine, self.engines, "v0.2.0")

    def test_unpushed_and_mismatched_tags_are_refused(self):
        self.release("0.3.0", push=False)
        with self.assertRaises(ImportFailed):
            import_engine(self.engine, self.engines, "v0.3.0")
        git(self.engine, "tag", "-a", "v9.0.0", "-m", "wrong", "v0.2.0")
        git(self.engine, "push", "origin", "v9.0.0")
        with self.assertRaises(ImportFailed):
            import_engine(self.engine, self.engines, "v9.0.0")
        with self.assertRaises(ImportFailed):
            import_engine(self.engine, self.engines, "release-1")


if __name__ == "__main__":
    unittest.main()
