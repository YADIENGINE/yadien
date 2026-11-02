# Developing the launcher

Requirements: VS 2022 Build Tools (C++ workload), CMake 3.24+, Python 3.10+, git.

The launcher's window is drawn with the engine's `ToolUi`, so building it needs an engine checkout. `dev.py` chooses it
(`Scripts/engine_location.py`): in a plain clone, `ToolCache/Engine`, cloned from `engineRepository` at the tag of the
version `engine` names in `LauncherManifest.json`. Using a change that is not released yet: `dev.py --engine PATH` with
your own checkout, or see Developing inside a game.

```powershell
python Scripts/fetch_toolchain.py     # ToolCache/: bundled Python, CMake, Ninja + the Velopack library (once)
python Scripts/dev.py build           # Debug: build/Debug/<engine>.com + <engine>.exe (window)
python Scripts/dev.py build --release
python Scripts/dev.py test            # build Debug + C++ tests (ctest) + script tests
python Scripts/dev.py clean
python Scripts/dev.py package         # Release build + installer (Packaging.md)
python Scripts/dev.py release         # publish what the manifests ask for (Packaging.md -> Publish)
python Scripts/dev.py engine-test     # move real projects between engine versions (Engines.md -> Test it)
```

`dev.py` gets the MSVC environment from the engine checkout's helper. Manually, in Developer PowerShell: `cmake
--preset launcher -DLAUNCHER_ENGINE_DIR=<engine checkout>`, `cmake --build --preset debug`, `ctest --preset debug`.

Development builds use `build/Engines/` (`dev.py` imports the engine `LauncherManifest.json` ships from its tag) and the repo's `ToolCache/` and keep settings in
`build/<Config>/LauncherData/` (`LauncherLayout.json`). Try: `build\Debug\<engine>.com launcher system-check`, and
`...\<engine>.com build` in a project folder.

## Developing inside a game

The launcher repo can also be a `Launcher/` submodule in a game's repo; engine, launcher, and game are developed in one
place, with the game's `Engine/` as the single engine copy:

```
MyGame/
  Engine/      game's engine submodule (single engine copy)
  Launcher/    this repo
```

- When `Scripts/` commands detect the launcher inside a project (`Project.json` + `Engine/` in the parent folder), they
  use that `Engine/` (`Scripts/engine_location.py`); `--engine PATH` always takes precedence. It is passed to CMake as
  `LAUNCHER_ENGINE_DIR`.
- A release builds against the tag of the engine `LauncherManifest.json` ships, not this checkout: a launcher that needs
  an engine change ships an engine version holding it (Packaging.md -> Publish).
- The order is always engine → launcher → game. The game's repo points only to pushed commits.

Tests: C++ `Tests/` (Catch2; `[git]` tests use local git repositories and skip if git is absent); scripts: `python -m
unittest discover -s Scripts/Tests -t .`.

## Optional UI screenshots

In the existing build tree, `cmake --build build --config Debug --target LauncherUiSmoke` (in the MSVC environment),
then `build/Debug/LauncherUiSmoke.exe build/UiReview`. Normal build/test does not run this target; desktop/GPU required.
A hidden engine window uses production screens, embedded fonts, and the rlImGui renderer; it does not touch user settings
or projects. It produces PNGs for the first-run screen, temporary populated lists, search, details, forms, and 75/100/125/150/200%
scales; horizontal overflow and persistent toolbars requiring internal scrolling count as errors. Folder-selection
validation, automatic discovery, default-folder saving, and the new-project form's use of that folder are checked with
temporary data. Also inspect images visually.