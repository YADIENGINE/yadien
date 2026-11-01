"""update_test: the pure parts; the installing run itself is `dev.py update-test`."""

import json
import tempfile
import unittest
from pathlib import Path

from Scripts.update_test import fake_release_list, file_url, installed_version, next_patch, parse_update_line


class UpdateTestHelpers(unittest.TestCase):
    def test_next_patch(self):
        self.assertEqual(next_patch("0.2.9"), "0.2.10")

    def test_release_list_holds_the_feed_and_every_package(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ("App-1-full.nupkg", "App-2-delta.nupkg", "releases.win.json", "App-Setup.exe"):
                (root / name).write_text("x", encoding="utf-8")
            releases = json.loads(fake_release_list(root))
        self.assertEqual(len(releases), 1)
        self.assertTrue(releases[0]["prerelease"])
        names = [asset["name"] for asset in releases[0]["assets"]]
        self.assertEqual(sorted(names), ["App-1-full.nupkg", "App-2-delta.nupkg", "releases.win.json"])
        self.assertTrue(all(asset["browser_download_url"].startswith("file:///") for asset in releases[0]["assets"]))

    def test_file_url_uses_forward_slashes(self):
        self.assertNotIn("\\", file_url(Path(tempfile.gettempdir()) / "a.json"))

    def test_installed_version_reads_the_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.assertIsNone(installed_version(root))
            (root / "sq.version").write_text("<package><version>1.2.3</version></package>", encoding="utf-8")
            self.assertEqual(installed_version(root), "1.2.3")

    def test_parse_update_line(self):
        self.assertEqual(parse_update_line("update 1.2.3 100 7\ndownloaded 100\n"), ("1.2.3", 100, 7))
        self.assertIsNone(parse_update_line("up-to-date\n"))


if __name__ == "__main__":
    unittest.main()
