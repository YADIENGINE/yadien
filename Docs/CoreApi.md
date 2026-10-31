# Core API

All launcher work lives in `LauncherCore` (headless, `Core/`); UI only calls it and implements no business logic. Design
decisions are in the engine repo's `Docs/Design/README.md` (D4, D6, D8, D14, D15, D16); this document describes their
code counterparts. Architecture: [Architecture](Architecture.md).

## Status (2026-10-31)

- Core (repo, import, tool lock, CLI, settings, project operations, modes) and UI preparation are complete: background
  jobs (`Jobs/`), "Open" / "Run" flows, project row model and details, Scan, PATH, Explorer, folder picker, `Apps/Ui`
  skeleton (window, theme, model, empty screen functions).
- Only visual work remains: fill `Apps/Ui/Screens/` functions ([UiScreens](UiScreens.md)). Decisions: engine
  `Docs/Design/README.md` → D15 "UI preparation".
- The window uses the engine's shared tool UI: `EngineToolUi` (`ToolUi/`, engine `Docs/ToolUi.md`).

## General rules

- Error channel is `Result<T>` / `Status` (`Platform/Result.h`): `Ok()`, `Value()`, `Error()`. Messages are English, one
  sentence, no trailing period; UI can show them as-is. Exceptions do not escape public API.
- Text is UTF-8 `std::string`; paths are `std::filesystem::path` (`PathToUtf8` / `Utf8ToPath`, `Platform/Text.h`).
- Process output is `OutputLine` (`std::function<void(const std::string&)>`), line by line, UTF-8. The callback runs on
  the calling thread; jobs supply `JobContext::Lines()`, and lines accumulate in `BackgroundJob`.
- Long tasks do not run on the UI thread: `Job<T>` (below) starts its own thread; core functions do not start threads.
- Name: product name `LAUNCHER_PRODUCT_NAME`, executable name `LAUNCHER_EXECUTABLE_NAME`, launcher version
  `LAUNCHER_VERSION` (CMake definitions; name from engine manifest). Do not hardcode the name in UI text.

## Startup

```cpp
Result<LauncherLayout> layout = ResolveLauncherLayout(ExecutablePath().parent_path()); // Settings/LauncherLayout.h
Result<LauncherSettings> settings = LoadLauncherSettings(layout.Value().SettingsFile());
EngineLibrary engines = EngineLibrary::Scan(layout.Value().enginesFolder);           // Engines/EngineLibrary.h
```

- `LauncherLayout`: `enginesFolder`, `toolsFolder`, `dataFolder`, `SettingsFile()`. In installed launchers, all are
  beside the executable; in development builds, `LauncherLayout.json` beside the executable (written by CMake) points to
  the repo's `Engines/`, `ToolCache/`, and a data folder inside the build.
- Invalid `Settings.json` is an error (not silently reset): UI tells the user and offers "Start with defaults".
- On first launch (D15), System check opens automatically: with no `Settings.json`, `LoadLauncherSettings` returns
  defaults; UI detects this with `std::filesystem::exists(SettingsFile())`.

## Settings and project list (`Settings/`)

| API | Job |
| --- | --- |
| `LauncherSettings { mode, projects, projectsFolder, projectSort }` | `Settings.json` (`format: 1`) |
| `SaveLauncherSettings(file, settings)` | atomic write; UI calls after every change |
| `AddProject(settings, root)` | "Add existing": folder must have `Project.json` and not be listed |
| `RemoveProject(settings, root)` | "Remove from list": does not touch disk |
| `MarkProjectOpened(settings, root, CurrentTimeIso())` | on "Open"; adds if not listed |
| `RecentProjects(settings)` | screen order: most recently opened first, never opened last |
| `ProjectsFolderOf(settings)` / `DefaultProjectsFolder()` | suggested location for new projects (`Belgeler\<Ad> Projects`) |

`LauncherMode::User` / `Developer`. Developer mode has no password (engine D19): after a warning confirmation, UI writes
`settings.mode = LauncherMode::Developer`.

## Background jobs (`Jobs/`)

```cpp
auto job = std::make_unique<Job<SystemReport>>("System check", 1, [layout, mode](JobContext& context) -> Result<SystemReport> {
    context.Step("Checking tools");                 // step text + index (from 1)
    return RunSystemCheck(layout, mode);            // context.Lines(): output, context.CancelFlag(): cancellation
});
// UI thread every frame: job->State() (Running / Succeeded / Failed / Canceled), StepText(), StepIndex(),
// StepCount(), LinesFrom(next) (new lines; last BackgroundJob::MaxLines retained), Error(), Cancel()
// On completion: *job->Outcome() → Result<T> (Status for Job<void>)
```

- `BackgroundJob` is the untyped base (UI reads only this); `Job<T>` adds the result. Destroying a job cancels it and
  waits for its thread. If cancellation was requested when the body returns an error, state is `Canceled`; an exception
  thrown by the body is `Failed`.
- No percentage: step + "not frozen" indicator + live output (D15).

## Project row (`Projects/ProjectRow.h`, `ProjectDetails.h`)

```cpp
std::vector<ProjectRow> rows = InspectProjectRows(settings, engines); // fast, reads files only
std::vector<std::size_t> visible = VisibleRows(rows, filter, settings.projectSort);
ProjectActions actions = ActionsFor(rows[i], engines, settings.mode);  // right action panel buttons
std::vector<ProjectDetails> slow = InspectProjectDetails(rows, mode, cancel); // in a background job
```

| `ProjectRow` | Meaning |
| --- | --- |
| `entry` | `root`, `lastOpened` |
| `info` | `ProjectInfo`: `name`, `kind` (`Copy` / `Git` / `Unregistered` / `Missing`), `manifest`, `problem` |
| `Version()` | "0.1.2"; empty without an engine |
| `folderMissing` | folder deleted ("Remove missing") |
| `update`, `updateMayBreak` | newer stable version for an engine copy and whether it may break (`MayBreak`) |
| `contractSupported` | `false`: launcher cannot drive this engine, "update the launcher" |

- `ProjectActions { open, run, showInFolder, updateEngine, repairEngine, replaceEngine, engineGit, remove }`; target
  version `UpdateTarget` / `RepairTarget` / `ReplaceTarget`.
- `ProjectDetails { root, integrity, git, error }`: engine copy read-only check (`CopyIntegrity`: modified / missing /
  added files) and git engine state in developer mode (`EngineGitStatus`).
- `ProjectsUsing(rows, version)`: "N projects use this" in the Engines tab.
- `Git`: "engine managed by git" note; engine buttons disabled in user mode. `Unregistered`: "unknown source" warning +
  Replace engine.

## Open and Run (`Projects/ProjectLaunch.h`)

```cpp
Status OpenProject(info, toolchain, LauncherEngineRunner(toolchain, context.CancelFlag()), StartDetached, context);
Status RunProject(info, LauncherEngineRunner(toolchain, context.CancelFlag()), context);
```

- Open, `OpenProjectSteps` (3) steps: "Checking engine files" (read-only check for engine copies; changes produce a
  `warning:` line and opening continues) → "Building editor" (`editor --build-only`) → "Starting editor" (`editor`
  starts detached). On success, the caller runs `MarkProjectOpened` + saves, and the launcher closes.
- Run: engine `run` (builds, opens the game, finishes when the game closes); cancellation stops the build or closes the
  game.
- Engine commands get `ENGINE_LAUNCHER` (`WindowedLauncherExecutable()`): the editor's "Back to project list" (engine
  `Docs/ToolsContract.md` → Environment variables).

The launcher implements no build logic; everything lives in the project's `Engine/Tools` (D7).

## Scan, folders, PATH

| API | Job |
| --- | --- |
| `FindProjects(folder, cancel)` (`Projects/ProjectScan.h`) | "Scan": folders with `Project.json`, up to `MaxScanDepth` (4) levels; does not enter projects, hidden folders, or build folders |
| `OpenFolder(path)` (`Platform/Shell.h`) | "Show in folder", "Open install folder" |
| `LauncherFolder()`, `IsOnUserPath` / `AddToUserPath` / `RemoveFromUserPath` (`System/UserPath.h`) | Settings → PATH (HKCU, `WM_SETTINGCHANGE`) |
| `PickFolder(title, initial)` (`Apps/Ui/Dialogs/FolderDialog.h`) | Windows folder picker (UI side) |

## New project (`Projects/NewProject.h`)

```cpp
NewProjectRequest request{name, location, *engines.FindSeries("0.1")};           // user mode: stable engine copy
NewProjectRequest dev{name, location, DevelopmentEngine{engines.Newest()->manifest.repository, "main"}}; // developer
Status usable = CheckNewProjectFolder(request);                 // live validation: valid folder name, empty folder
Result<std::filesystem::path> root = CreateProject(request, LauncherEngineRunner(toolchain, &cancel), onLine);
if (root.Ok()) { (void)AddProject(settings, root.Value()); SaveLauncherSettings(...); }
```

- Naming rules belong to the engine (`new --name`, letters/digits/`_`); errors appear in output. Location + name =
  project folder.
- Engine copy: `Engines/<series>` → `<folder>/Engine`, then engine `new`. Does not touch git (D14).
- Developer ("dev (main)", listed only in developer mode): `git init` + submodule (`repository`, branch) + `new` +
  initial commit "Create <name>".
- On error, everything created is removed.

## Engine update (`Projects/EngineUpdate.h`)

```cpp
Result<EngineUpdatePlan> plan = PlanEngineUpdate(info, *engines.UpdateFor(info.manifest->version)); // worker thread
// Confirmation screen: plan.notes (all intervening versions, newest first; highlight breaking[i].change / .migrate),
// plan.mayBreak, plan.overwritten (edits to lose), plan.deleted (user files to delete)
Status applied = ApplyEngineUpdate(plan.Value());                                                    // after confirmation
```

- Always confirmed (D15). No downgrades; the same version is "repair". Rejected for `Git` type.
- Application copies beside the old engine, then swaps folders; on error, the old engine remains in place. Windows
  cannot move the folder while the project's editor / game is open; the error message explains this.

## Developer project git operations (`Developer/DeveloperGit.h`)

Shown only with `LauncherMode::Developer` and `kind == Git`.

| API | Button |
| --- | --- |
| `ReadEngineGitStatus(root)` → `EngineGitStatus { branch, trackedBranch, head, changedFiles, ahead, behind, pointerChanged, Detached() }` | row status (change count, detached warning) |
| `UpdateEngineFromRemote(root, onLine)` | "Update engine": engine must be clean; resolves detached state, fast-forwards, commits "Update Engine" in the project |
| `PublishEngineChanges(root, message, onLine)` | "Publish engine changes": commit + pull --rebase + push in engine, pointer commit in project (project is not pushed) |

## System check (`System/SystemCheck.h`)

```cpp
SystemReport report = RunSystemCheck(layout, settings.mode); // ~1 s, worker thread
for (const CheckItem& item : report.items) // id, title, status (Ok / Warning / Missing), detail, fix
if (item.fix == CheckFix::InstallMsvc) InstallMsvc(onLine, &cancel); // winget; long; then RunSystemCheck again
report.ReadyToBuild()                                                 // true if no Missing
```

Items: `python`, `cmake`, `ninja` (lock-file version), `msvc` (vswhere), `engines`, and in developer mode `git`
(installed, `user.name` / `user.email`).

## CLI (`Cli/CommandLine.h`)

`<engine>.com` calls `RunCommandLine`; UI has no role. `<engine> launcher version | system-check | projects [--json]`,
`audio-tools [status | install | remove]`, `python SCRIPT [ARGS]`; everything else goes unchanged to the project's
engine (D6). Details: [Architecture](Architecture.md#cli).

## Audio tools (`System/AudioTools.h`)

| API | Job |
| --- | --- |
| `Toolchain::audio` | the wheels this launcher offers (`Toolchain.json` `audio`; empty: the feature is hidden) |
| `InspectAudioTools(folder, packages)` | `NotOffered` / `NotInstalled` / `Outdated` (other versions) / `Installed` |
| `InstallAudioTools(folder, packages, onLine, cancel)` | download (`Platform/Download.h`), SHA-256, `tar.exe`, then replace `folder` whole; a failure keeps the old one |
| `RemoveAudioTools(folder)` | deletes the folder |
| `PythonScriptCommand(toolchain, folder, arguments)` | the bundled Python running SCRIPT with its folder and `folder/site` on `sys.path` |

The folder is `LauncherLayout::AudioToolsFolder()` (`<data>/AudioTools`). Toolchain.md has the file layout.

## Deliberately excluded in this session

- Installation, shortcuts, Apps registration, in-place updates: done with Velopack ([Packaging](Packaging.md)).
- Branch / tag / commit selection in developer projects: outside the plan; "dev (main)" starts from the branch's latest
  commit.
- `launcher new` CLI operation (project setup for AI / scripts): a thin layer over `CreateProject` if requested.
