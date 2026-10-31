"""package: what goes into the installer, the launcher's license notices and the vpk call; no compiler, no vpk."""

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from Scripts.package import (NOTICES_FILE, VELOPACK_DLL, PackageFailed, excluded, launcher_notices,
                             launcher_version, pack, setup_name, stage)

LOCK = {"format": 1,
        "tools": {"tool": {"version": "2.0", "executable": "bin/tool.exe", "exclude": ["doc/html", "bin/gui.exe"]}},
        "libraries": {"lib": {"version": "3.1", "check": "include/lib.h"}}}


class PackageTests(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory(prefix="Package-")
        self.root = Path(self._temp.name)

    def tearDown(self):
        self._temp.cleanup()

    def write(self, relative: str, text: str = "x") -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def test_the_package_version_is_the_launchers(self):
        self.assertRegex(launcher_version(), r"^\d+\.\d+\.\d+$")

    def test_exclude_entries_match_files_and_folders(self):
        patterns = LOCK["tools"]["tool"]["exclude"]
        self.assertTrue(excluded(Path("doc/html/index.html"), patterns))
        self.assertTrue(excluded(Path("bin/gui.exe"), patterns))
        self.assertFalse(excluded(Path("doc/Copyright.txt"), patterns))
        self.assertFalse(excluded(Path("bin/tool.exe"), patterns))

    def test_stage_ships_binaries_engines_and_trimmed_tools(self):
        for name in ("Release/myengine.exe", "Release/myengine.com", f"Release/{VELOPACK_DLL}",
                     "Release/LauncherLayout.json", "Engines/Engine0.1/EngineManifest.json",
                     "Cache/tool/bin/tool.exe", "Cache/tool/bin/gui.exe", "Cache/tool/doc/html/a.html",
                     "Cache/tool/doc/Copyright.txt", "Cache/tool/.fetched", "Cache/Toolchain.json",
                     "Cache/lib/include/lib.h"):
            self.write(name)
        target = self.root / "Stage"
        stage(self.root / "Release", target, "myengine", self.root / "Engines", self.root / "Cache", LOCK)
        shipped = {path.relative_to(target).as_posix() for path in target.rglob("*") if path.is_file()}
        self.assertEqual(shipped, {"myengine.exe", "myengine.com", VELOPACK_DLL,
                                   "Engines/Engine0.1/EngineManifest.json", "Tools/tool/bin/tool.exe",
                                   "Tools/tool/doc/Copyright.txt", "Tools/Toolchain.json"})

    def test_a_missing_build_stops_staging(self):
        with self.assertRaises(PackageFailed):
            stage(self.root / "Release", self.root / "Stage", "myengine", self.root / "Engines", self.root, LOCK)

    def test_notices_name_every_component_with_its_locked_version(self):
        notices = {"intro": "Intro.", "components": [
            {"name": "Tool", "lock": "tool", "license": "MIT", "text": "{tools}/tool/LICENSE"},
            {"name": "Lib", "lock": "lib", "license": "zlib", "text": "{repo}/lib.txt"},
            {"name": "Own", "license": "MIT", "text": "{repo}/LICENSE"}]}
        self.write("Cache/tool/LICENSE", "tool license text")
        self.write("lib.txt", "lib license text")
        self.write("LICENSE", "own license text")
        text = launcher_notices(notices, LOCK, {"repo": self.root, "tools": self.root / "Cache"})
        for expected in ("Tool 2.0 (MIT)", "Lib 3.1 (zlib)", "Own (MIT)", "tool license text", "lib license text"):
            self.assertIn(expected, text)

    def test_a_missing_license_text_stops(self):
        notices = {"intro": "", "components": [{"name": "Gone", "license": "MIT", "text": "{repo}/missing.txt"}]}
        with self.assertRaises(PackageFailed):
            launcher_notices(notices, LOCK, {"repo": self.root})

    def test_the_real_notice_list_is_well_formed(self):
        notices = json.loads(NOTICES_FILE.read_text(encoding="utf-8"))
        lock = json.loads((NOTICES_FILE.parents[1] / "Toolchain.lock.json").read_text(encoding="utf-8"))
        locked = set(lock["tools"]) | set(lock.get("libraries", {}))
        for component in notices["components"]:
            self.assertTrue({"name", "license", "text"} <= component.keys(), component)
            if "lock" in component:
                self.assertIn(component["lock"], locked)

    def test_pack_calls_vpk_and_names_the_setup(self):
        calls = []

        def runner(command):
            calls.append(command)
            self.write("Releases/MyEngine-win-Setup.exe")
            return SimpleNamespace(returncode=0)

        self.write("Brand/Icon.ico")
        self.write("Brand/Splash.png")
        setup = pack(self.root / "Stage", self.root / "Releases", self.root / "Brand", "MyEngine", "myengine", "1.2.3",
                     runner, vpk="vpk")
        self.assertEqual(setup.name, setup_name("MyEngine", "1.2.3"))
        self.assertTrue(setup.is_file())
        command = calls[0]
        self.assertEqual(command[command.index("--packVersion") + 1], "1.2.3")
        self.assertEqual(command[command.index("--mainExe") + 1], "myengine.exe")
        self.assertEqual(Path(command[command.index("--icon") + 1]).name, "Icon.ico")
        self.assertEqual(Path(command[command.index("--splashImage") + 1]).name, "Splash.png")


if __name__ == "__main__":
    unittest.main()
