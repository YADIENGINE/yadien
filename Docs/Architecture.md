# Launcher architecture

The launcher is a separate product from the engine (engine `Docs/Design/README.md` D15, D16). It installs engine
versions, manages projects, and forwards commands to each project's engine. It knows no games, and its product name
always comes from the engine manifest.

## Repository

```
Core/                LauncherCore: headless services shared by UI and CLI
  Jobs/              Background work, progress, output, and cancellation
  Platform/          Results, text, files, processes, downloads, and Windows integration
  Engines/           Installed and online engine versions
  Projects/          Project inspection, creation, launch, and engine changes
  Settings/          Layout, settings, and the project list
  Developer/         Developer-mode git operations
  System/            Toolchain, system checks, PATH, and command activity
  Update/            Launcher updates
  Cli/               Command parsing and forwarding
Apps/Cli/             `<engine>.com`
Apps/Ui/              `<engine>.exe`, its model, and drawing-only screens
Tests/                Catch2 tests and test support
Scripts/              Development, packaging, release, and toolchain scripts
Packaging/            Installer notices and licenses
```

See [Core API](CoreApi.md), [UI screens](UiScreens.md), [engine versions](Engines.md), and
[packaging](Packaging.md) for subsystem details.

## Layers

- `LauncherCore` owns all business logic. UI and CLI are thin entry points over the same services.
- The core has no raylib or ImGui dependency. The UI uses the engine's shared `EngineToolUi` theme and widgets.
- Build, test, and editor logic belong to the project's `Engine/Tools`; the launcher only invokes that
  contract with its bundled Python.
- Core long-running functions accept output and cancellation callbacks. Only `Jobs/` creates threads.
- `LauncherModel` owns UI state, navigation, sequencing, and saves. Screen functions only draw and queue actions.

## CLI

`<engine>.com` is the console entry point and `<engine>.exe` is the windowed entry point. Because some callers select
`.exe` explicitly, the windowed executable forwards command arguments, standard handles, and the exit code to `.com`.

```
<engine> [--project ROOT] <engine command> [options]
<engine> launcher version | system-check | projects [--json]
<engine> launcher audio-tools [status | install | remove]
<engine> launcher python SCRIPT [ARGS]
```

Without `--project`, the launcher searches upward for `Project.json` (or `Engine/EngineManifest.json` for `new`). It
rejects unsupported `toolsContract` versions, warns about modified engine-copy files, shares Ctrl+C with the child, and
returns the child's exit code unchanged. The engine reserves no command named `launcher`.

## Runtime layout

Velopack installs executables, shipped engines, and tools under `%LOCALAPPDATA%\<Name>\current\`. Persistent settings,
downloaded engines, and optional audio tools live in `%LOCALAPPDATA%\<Name> Data\`, so updates can replace `current/`
without removing user data. Installer hooks maintain the user's PATH and remove the data folder on uninstall.

Development builds use `LauncherLayout.json` beside the executable to point at `build/Engines/`, `ToolCache/`, and
`build/<Config>/LauncherData/`; they never use installed-launcher settings.

## Code rules

The codebase is C++20 with `/W4`, RAII, no named namespaces, no raw `new`/`delete` or C arrays, and one class per
`.h`/`.cpp` pair. It uses `PascalCase`, `camelCase`, `_camelCase`, `#pragma once`, a single ordered include block, and
`[[nodiscard]] const` getters. Format with `.clang-format`, use LF, and keep code, comments, and documentation in English.
