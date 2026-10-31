"""fetch_toolchain: only archives whose SHA-256 matches the lock are extracted; Toolchain.json tells the launcher."""

import hashlib
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from Scripts.fetch_toolchain import FetchFailed, fetch, load_lock, pin


class FetchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="FetchToolchain-")
        self.root = Path(self.temp.name)
        self.archive = self.root / "tool.zip"
        with zipfile.ZipFile(self.archive, "w") as bundle:
            bundle.writestr("tool-1.0/bin/tool.exe", "binary")
        self.cache = self.root / "ToolCache"

    def tearDown(self):
        self.temp.cleanup()

    def lock(self, sha256: str) -> dict:
        return {"format": 1, "tools": {"tool": {"version": "1.0", "url": self.archive.as_uri(), "sha256": sha256,
                                                "root": "tool-1.0", "executable": "bin/tool.exe"}}}

    def test_verified_archive_is_extracted_and_recorded(self):
        fetch(self.lock(hashlib.sha256(self.archive.read_bytes()).hexdigest()), self.cache)
        self.assertEqual((self.cache / "tool" / "bin" / "tool.exe").read_text(), "binary")
        installed = json.loads((self.cache / "Toolchain.json").read_text(encoding="utf-8"))
        self.assertEqual(installed["tools"]["tool"], {"version": "1.0", "executable": "tool/bin/tool.exe"})

    def test_a_wrong_hash_stops_before_extracting(self):
        with self.assertRaises(FetchFailed):
            fetch(self.lock("0" * 64), self.cache)
        self.assertFalse((self.cache / "tool").exists())

    def test_libraries_are_fetched_but_not_listed_as_tools(self):
        lock = self.lock(hashlib.sha256(self.archive.read_bytes()).hexdigest())
        library = {key: value for key, value in lock["tools"]["tool"].items() if key != "executable"}
        lock["libraries"] = {"lib": {**library, "check": "bin/tool.exe"}}
        fetch(lock, self.cache)
        self.assertTrue((self.cache / "lib" / "bin" / "tool.exe").is_file())
        installed = json.loads((self.cache / "Toolchain.json").read_text(encoding="utf-8"))
        self.assertEqual(list(installed["tools"]), ["tool"])

    def test_audio_packages_pass_through_to_the_launcher(self):
        lock = self.lock(hashlib.sha256(self.archive.read_bytes()).hexdigest())
        lock["audio"] = {"numpy": {"version": "2.0", "url": "https://example.invalid/numpy.whl", "sha256": "ab" * 32}}
        fetch(lock, self.cache)
        installed = json.loads((self.cache / "Toolchain.json").read_text(encoding="utf-8"))
        self.assertEqual(installed["audio"], lock["audio"])
        self.assertFalse((self.cache / "numpy").exists())

    def test_pin_records_missing_hashes(self):
        lock_path = self.root / "Toolchain.lock.json"
        lock_path.write_text(json.dumps(self.lock("")), encoding="utf-8")
        pin(load_lock(lock_path), lock_path, self.cache)
        pinned = load_lock(lock_path)["tools"]["tool"]["sha256"]
        self.assertEqual(pinned, hashlib.sha256(self.archive.read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
