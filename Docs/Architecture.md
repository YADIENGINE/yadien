# Launcher architecture

The launcher is a separate product from the engine (engine `Docs/Design/README.md` D15, D16): it carries stable engine
versions, creates and opens projects, and forwards engine commands to each project's own engine. It knows no games; it
does not hardcode the engine name (the name comes from the engine manifest).

## Repo

```
Engine/              submodule: engine repo, main (active development). The launcher builds against this.
Engines/             stable engine copies, Engine<MAJOR.MINOR>/ (Scripts/import_engine.py; never edited) — Engines.md
Core/                LauncherCore: headless core, include root (#include "Projects/NewProject.h")
  Jobs/              BackgroundJob / Job<T>: long task thread, step, output, cancellation (UI reads every frame)
  Platform/          Result/Status, UTF-8 ↔ UTF-16, files (atomic writes, tree copy), processes, BCrypt, known
                     folders, opening in Explorer
  Engines/           EngineVersion, EngineManifest (notes), EngineCopyRecord, EngineLibrary (Engines/ scan)
  Projects/          ProjectInfo (type), ProjectRow / ProjectDetails (list row, actions, slow checks),
                     ProjectLaunch (Open, Run), ProjectScan, CopyIntegrity (read-only), EngineUpdate, NewProject
  Settings/          LauncherLayout, LauncherSettings (Settings.json), ProjectList
  Developer/         DeveloperGit (submodule operations)
  System/            Toolchain (Toolchain.json), SystemCheck (tools, vswhere, winget, git), UserPath (PATH)
  Cli/               EngineCommand (forwarding), CommandLine (<engine>.com)
Apps/
  Cli/Main.cpp       <engine>.com (console)
  Ui/                <engine>.exe (window): model + screen functions, engine EngineToolUi — UiScreens.md
Tests/               Catch2 tests (LauncherCoreTests) + Support/
Scripts/             Developer scripts (stdlib Python): import_engine, fetch_toolchain, dev, package, brand
Packaging/           Installer inputs: Notices.json (licenses of the launcher and its tools), Licenses/ — Packaging.md
Toolchain.lock.json  Bundled tool and library versions / addresses / SHA-256 — Toolchain.md
ToolCache/           Downloaded tools and libraries (not tracked in git)
```

## Layers

- All work lives in `LauncherCore`; entry points are thin. UI and CLI call the same functions (D6: one logic layer).
- The core knows no raylib / ImGui; it uses only the engine's `ThirdParty/nlohmann_json` and Windows APIs (BCrypt,
  Shell). The UI target also links the engine's ImGui layer.
- The launcher does not implement build logic: build, test, and editor live in the project's `Engine/Tools` (D7). The
  launcher runs it with its own Python (`EngineCommand`).
- Long tasks stream output through `OutputLine` and take a cancellation flag; only `Jobs/` creates threads (each long UI
  task is a `Job<T>`; CLI calls the same functions directly).
- Window: `Apps/Ui` draws on the engine's `Game` using the engine's tool UI (`EngineToolUi`, theme and widgets shared
  with the editor). State lives in one model (`LauncherModel`); screens only draw ([UiScreens](UiScreens.md)).

## CLI

`<engine>.com` is the console program, `<engine>.exe` the windowed program; when typing `<engine>` in a terminal,
Windows selects `.com` first (D15).

```
<engine> [--project ROOT] <engine command> [options]   → <python> -X utf8 <ROOT>/Engine --project <ROOT> <...>
<engine> launcher version | system-check | projects [--json]
```

- Project: without `--project`, the first folder upward from the working folder containing `Project.json` (or
  `Engine/EngineManifest.json` before `new`).
- If the manifest's `toolsContract` is not in `SupportsToolsContract`, the command does not run (exit 2, "update the
  launcher").
- Every command in an engine copy project starts with a read-only check; modified files produce a stderr warning (does
  not stop the build).
- The process shares the console; Ctrl+C reaches the child, and the exit code is returned unchanged (0 / 1 / 2 / 130).
- The engine adds no command named `launcher` (engine `Docs/ToolsContract.md`).

## Runtime layout

Installed (Stage 4, Velopack): `%LOCALAPPDATA%\<Name>\` holds `Update.exe`, `packages/`, a small `<Name>.exe` that
the shortcuts start, `Settings.json`, and `current/`: `<engine>.exe` (+ the Velopack DLL), `<engine>.com`, `Engines/`,
`Tools/` (Toolchain.json + python, cmake, ninja). Every update replaces `current/`, so the settings stay outside it
(`LauncherLayout`). `<engine>.exe` starts with `RunInstallerHooks()` (`Apps/Ui/InstallerHooks`): after installing,
Velopack runs it to add `current/` to the user's PATH; before uninstalling, to remove it. The installer itself:
[Packaging](Packaging.md).

In development builds, `LauncherLayout.json` beside the executable points to the repo's `Engines/` and `ToolCache/`
folders and the `build/<Config>/LauncherData/` data folder; installed launcher settings are untouched.

## Code rules

Same as the engine: C++20, no `/W4` warnings, no named namespaces (`.cpp` helpers in anonymous namespaces), each class
in its own `.h` + `.cpp`, `#pragma once`, one include block (own header → third-party → std → project), no `new` /
`delete` or C arrays, RAII, single-line `enum class`, `PascalCase` / `camelCase` / `_camelCase`, getters `[[nodiscard]]
const`. Format `.clang-format`, LF line endings. Code text and comments are English; documentation is Turkish.