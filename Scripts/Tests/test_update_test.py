"""update_test: the pure parts; the installing run itself is `dev.py update-test`."""

import json
import tempfile
import unittest
from pathlib import Path

from Scripts.update_test import fake_releases, file_url, installed_version, next_patch, parse_update_line


class UpdateTestHelpers(unittest.TestCase):
    def test_next_patch(self):
        self.assertEqual(next_patch("0.2.9"), "0.2.10")

    def test_releases_are_laid_out_like_github_downloads(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            releases = root / "Releases"
            releases.mkdir()
            feed = {"Assets": [{"Version": "1.0.0", "FileName": "App-1.0.0-full.nupkg"},
                               {"Version": "1.0.1", "FileName": "App-1.0.1-delta.nupkg"},
                               {"Version": "0.9.0", "FileName": "App-0.9.0-full.nupkg"}]}
            (releases / "releases.win.json").write_text(json.dumps(feed), encoding="utf-8")
            for name in ("App-1.0.0-full.nupkg", "App-1.0.1-delta.nupkg", "App-Setup.exe"):
                (releases / name).write_text("x", encoding="utf-8")
            server = fake_releases(releases, root / "Server")
            files = sorted(path.relative_to(server).as_posix() for path in server.rglob("*") if path.is_file())
        self.assertEqual(files, ["download/v1.0.0/App-1.0.0-full.nupkg", "download/v1.0.1/App-1.0.1-delta.nupkg",
                                 "latest/download/releases.win.json"])

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
