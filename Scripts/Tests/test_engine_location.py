"""engine_location: a standalone clone builds against the tag of the engine it ships."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from Scripts.engine_location import MANIFEST_FILE, fetched_engine


def git(folder: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(folder), "-c", "user.name=Test", "-c", "user.email=t@t", *args],
                          check=True, capture_output=True, text=True).stdout.strip()


class FetchedEngineTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.source = self.root / "engine"
        self.source.mkdir()
        git(self.source, "init", "-q", "-b", "main")
        self.clone = self.root / "ToolCache" / "Engine"

    def release(self, version: str) -> None:
        (self.source / MANIFEST_FILE).write_text(version)
        git(self.source, "add", "-A")
        git(self.source, "commit", "-q", "-m", version)
        git(self.source, "tag", f"v{version}")

    def test_the_clone_holds_the_requested_tag(self):
        self.release("1.0.0")
        self.release("2.0.0")
        engine = fetched_engine(str(self.source), "1.0.0", self.clone)
        self.assertEqual((engine / MANIFEST_FILE).read_text(), "1.0.0")

    def test_a_later_version_is_fetched_into_the_same_clone(self):
        self.release("1.0.0")
        fetched_engine(str(self.source), "1.0.0", self.clone)
        self.release("2.0.0")
        engine = fetched_engine(str(self.source), "2.0.0", self.clone)
        self.assertEqual((engine / MANIFEST_FILE).read_text(), "2.0.0")

    def test_a_version_without_a_tag_stops(self):
        self.release("1.0.0")
        with self.assertRaisesRegex(SystemExit, "v9.0.0 not found"):
            fetched_engine(str(self.source), "9.0.0", self.clone)


if __name__ == "__main__":
    unittest.main()
