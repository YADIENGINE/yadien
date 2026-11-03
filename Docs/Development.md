# Developing the launcher

Requirements: Visual Studio 2022 Build Tools with the C++ workload, CMake 3.24+, Python 3.10+, and git.

The window uses the engine's `ToolUi`, so every build needs an engine checkout. `Scripts/engine_location.py` selects it:
`--engine PATH` when supplied, the parent project's `Engine/` when developing inside a game, or `ToolCache/Engine` at
the version in `LauncherManifest.json` for a standalone clone.

```powershell
python Scripts/fetch_toolchain.py     # Download pinned tools and libraries once
python Scripts/dev.py build           # Debug build
python Scripts/dev.py build --release
python Scripts/dev.py test            # Debug build, C++ tests, and script tests
python Scripts/dev.py clean
```

`dev.py` obtains the MSVC environment from the selected engine. From Developer PowerShell, the equivalent manual commands
are `cmake --preset launcher -DLAUNCHER_ENGINE_DIR=<engine>`, `cmake --build --preset debug`, and
`ctest --preset debug`. Development output and settings locations are in [Architecture](Architecture.md#runtime-layout).

Packaging, update testing, and releases are documented in [Packaging](Packaging.md). Engine catalog and migration
validation are documented in [Engine versions](Engines.md#validation).

## Developing inside a game

The launcher may be the `Launcher/` submodule of a game repository beside that game's `Engine/` submodule. Scripts use
the game's engine unless `--engine PATH` overrides it. Changes flow engine → launcher → game, and the game points only to
pushed engine and launcher commits.

A release always builds against the engine tag selected by `LauncherManifest.json`, not the development checkout. A
launcher change that needs an unreleased engine change must therefore ship that engine version first.

## Tests and UI snapshots

C++ tests are under `Tests/`; `[git]` tests use temporary local repositories and skip when git is unavailable. Script
tests can run without a compiler:

```powershell
python -m unittest discover -s Scripts/Tests -t .
```

`LauncherUiSmoke` is an optional desktop/GPU target:

```powershell
cmake --build build --config Debug --target LauncherUiSmoke
build/Debug/LauncherUiSmoke.exe build/UiReview
```

It renders production screens at supported scales and checks layout assertions; inspect the generated PNGs as well.
Normal builds and tests do not run this target.
