# Developing the launcher

Requirements: VS 2022 Build Tools (C++ workload), CMake 3.24+, Python 3.10+, git. Clone: `git clone
--recurse-submodules` (or `git submodule update --init` afterwards).

```powershell
python Scripts/fetch_toolchain.py     # ToolCache/: Python, CMake, Ninja bundled with the launcher (once)
python Scripts/dev.py build           # Debug: build/Debug/<engine>.com + <engine>.exe (window)
python Scripts/dev.py build --release
python Scripts/dev.py test            # build Debug + C++ tests (ctest) + script tests
python Scripts/dev.py clean
```

`dev.py` gets the MSVC environment from the engine submodule's helper. Manually, in Developer PowerShell: `cmake
--preset launcher`, `cmake --build --preset debug`, `ctest --preset debug`.

Development builds use the repo's `Engines/` and `ToolCache/` folders and keep settings in
`build/<Config>/LauncherData/` (`LauncherLayout.json`). Try: `build\Debug\<engine>.com launcher system-check`, and
`...\<engine>.com build` in a project folder.

## Developing inside a game

The launcher repo can also be a `Launcher/` submodule in a game's repo; engine, launcher, and game are developed in one
place, with the game's `Engine/` as the single engine copy:

```
MyGame/
  Engine/      game's engine submodule (single engine copy)
  Launcher/    this repo; its Engine/ submodule is not initialized (git submodule update --init Launcher, not recursive)
```

- When `Scripts/` commands detect the launcher inside a project (`Project.json` + `Engine/` in the parent folder), they
  use that `Engine/` (`Scripts/engine_location.py`); `--engine PATH` always takes precedence. It is passed to CMake as
  `LAUNCHER_ENGINE_DIR`.
- The launcher's own Engine pointer is maintained manually: after pushing the engine change, run `python
  Launcher/Scripts/dev.py pin-engine` (sets the pointer to the engine's HEAD; rejects unpushed commits), then commit +
  push in the launcher repo, then update the `Launcher` pointer in the game's repo.
- The order is always engine → launcher → game. Each repo points only to pushed commits.

Tests: C++ `Tests/` (Catch2; `[git]` tests use local git repositories and skip if git is absent); scripts: `python -m
unittest discover -s Scripts/Tests -t .`.

## Optional UI screenshots

In the existing build tree, `cmake --build build --config Debug --target LauncherUiSmoke` (in the MSVC environment),
then `build/Debug/LauncherUiSmoke.exe build/UiReview`. Normal build/test does not run this target; desktop/GPU required.
A hidden engine window uses production screens, bundled fonts, and the rlImGui renderer; it does not touch user settings
or projects. It produces PNGs for temporary empty/populated lists, search, inspector, forms, and 75/100/125/150/200%
scales; horizontal overflow and persistent toolbars requiring internal scrolling count as errors. Folder-selection
validation, automatic discovery, default-folder saving, and the new-project form's use of that folder are checked with
temporary data. Also inspect images visually.