# Core API

`LauncherCore` (`Core/`) is the headless logic shared by the UI and CLI. This document describes its public service
boundaries; repository structure and command forwarding are in [Architecture](Architecture.md).

## Common contracts

- Public APIs return `Result<T>` or `Status`; exceptions do not cross the API boundary. Error messages are English,
  single-sentence, and have no trailing period.
- Text is UTF-8 `std::string`; paths are `std::filesystem::path`. Use `PathToUtf8` and `Utf8ToPath` at boundaries.
- Long operations accept `OutputLine` and a cancellation flag. Core functions do not create threads.
- Product, executable, and version text come from `LAUNCHER_PRODUCT_NAME`, `LAUNCHER_EXECUTABLE_NAME`, and
  `LAUNCHER_VERSION`; never hardcode the product name.

## Startup and settings

Startup resolves `LauncherLayout`, loads `LauncherSettings`, and scans `EngineLibrary`. Invalid settings are reported
rather than silently replaced; on first launch the UI uses defaults and opens System check. Runtime locations are
described in [Architecture](Architecture.md#runtime-layout).

| Service | Contract |
| --- | --- |
| `LoadLauncherSettings` / `SaveLauncherSettings` | Read `Settings.json`; save atomically after changes |
| `AddProject` / `RemoveProject` | Change only the project list, never project files |
| `MarkProjectOpened` | Add the project if needed and update its last-opened time |
| `RecentProjects` | Most recently opened first; never-opened projects last |
| `ProjectsFolderOf` | Resolve the configured or default new-project location |

`LauncherSettings::mode` is `User` or `Developer`. Project types and the authority of each mode are documented in
[Engine versions](Engines.md#project-types-and-modes).

## Background jobs

`Job<T>` runs one operation on its own thread and exposes it through the untyped `BackgroundJob` interface. The UI reads
state, current step, new output lines, error, and outcome each frame; it may request cancellation at any time. Destroying
a job cancels it and joins its thread. A canceled operation reports `Canceled`, an uncaught exception reports `Failed`,
and successful jobs retain their `Result<T>` or `Status` outcome.

Progress is expressed as named steps and live output, not a synthetic percentage.

## Project services

| Area | Main API | Contract |
| --- | --- | --- |
| Inspection | `InspectProjectRows`, `InspectProjectDetails` | Fast row data is read synchronously; integrity, git, and build details run in a job |
| Filtering | `VisibleRows`, `ActionsFor` | Apply search, sort, mode, project type, and engine availability |
| Discovery | `FindProjects` | Find `Project.json` up to `MaxScanDepth`, skipping projects, hidden folders, and build output |
| Launch | `OpenProject`, `RunProject` | Invoke the project's engine tools; the launcher contains no build logic |
| Creation | `ProjectNameProblem`, `CheckNewProjectFolder`, `CreateProject` | Validate the name (the engine `new` rule) and destination while typing, create through engine `new`, and remove partial output on failure |
| Engine change | `PlanEngineUpdate`, `ApplyEngineUpdate` | Plan losses before confirmation; copy beside the old engine and swap only when ready |
| Cleanup | `CleanProjectBuild`, `RemoveDownloadedEngine` | Remove only the requested build output or cached engine |

Opening an engine-copy project checks its engine files, builds the editor, and starts it detached. Modified files produce
a warning but do not block opening. Running stays attached until the game exits so cancellation can stop the build or
game. Both pass `ENGINE_LAUNCHER` for the editor's return-to-launcher action.

Stable projects receive a copied engine and never run git. Developer projects initialize an engine submodule and an
initial project commit. Engine updates are rejected for git-managed engines; repair means applying the same stable
version again.

## Developer git operations

`Developer/DeveloperGit` is available only in developer mode for git-managed engines:

| API | Contract |
| --- | --- |
| `ReadEngineGitStatus` | Report branch, tracking state, changes, ahead/behind, and pointer changes |
| `UpdateEngineFromRemote` | Require a clean engine, fast-forward it, and commit the new pointer in the project |
| `PublishEngineChanges` | Commit, rebase, and push the engine; commit but do not push the project pointer |

## System services

- `RunSystemCheck` verifies the bundled tools, MSVC, available engines, and developer-mode git configuration.
  `InstallMsvc` uses winget and then reruns the check.
- `IsOnUserPath`, `AddToUserPath`, and `RemoveFromUserPath` manage the user PATH and broadcast `WM_SETTINGCHANGE`.
- `OpenFolder` opens Explorer; the UI-owned `PickFolder` provides the Windows folder dialog.
- `InspectAudioTools`, `InstallAudioTools`, and `RemoveAudioTools` manage optional pinned Python wheels under the data
  folder. `PythonScriptCommand` runs scripts with bundled Python and those packages on `sys.path`. Package layout and
  pinning are in [Toolchain](Toolchain.md).

## Launcher updates

`UpdateController` owns the update state machine: check, download, cancel, restart, dismiss, and per-frame progress. The
core depends on `UpdateBackend`; only the UI adapter links Velopack, and development builds provide no backend.

`ReleaseSource` reads the latest release's feed and each package from its version's release through github.com
download addresses, never the rate-limited API, with cancellation and the system proxy. `LAUNCHER_UPDATE_RELEASES`
points it at a local folder laid out the same way (`Scripts/update_test.py`). A failure reaches the user in the source's
own words, not Velopack's generic callback error. Automatic checks stay silent when offline; a found update opens the
update panel once per start when nothing else is open. `CommandActivity` prevents restart while `.com` or a foreground launcher
job is using the install folder. Settings may disable automatic checks or hide one skipped version until a newer release
appears.

## CLI

`RunCommandLine` handles launcher commands and forwards everything else unchanged to the selected project's engine. The
command surface and project-resolution rules are in [Architecture](Architecture.md#cli).
