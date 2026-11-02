# Launcher screens (`Apps/Ui`)

The `<engine>.exe` window. Business logic lives in `LauncherCore` ([CoreApi](CoreApi.md)); sequencing (jobs, navigation,
saves, notices) lives in one application model (`Model/LauncherModel`); screens only draw. Decisions: engine
`Docs/Design/README.md` → D15 "UI preparation" (Godot project manager-like layout, English only, error language).

```
Apps/Ui/
  InstallerHooks                   Velopack's install / uninstall hooks (PATH); runs first in Main.cpp
  Update/VelopackUpdates           The update backend (LauncherVelopack); the presentation stays free of Velopack
  Main.cpp, LauncherEntryPoint.h   wWinMain; resolves layout, runs the window (raylib and <windows.h> separate)
  LauncherApplication              engine Game: ToolUiContext + model; Tick → DrawLauncher every frame
  Model/LauncherModel              all state and actions (below)
  Dialogs/FolderDialog             Windows folder picker (opened by model actions)
  Screens/LauncherScreens.h        list of screen functions
  Screens/*.cpp                    one drawing function per region (visual work here)
```

## Regions

| Function | File | Region |
| --- | --- | --- |
| `DrawLauncher` | `LauncherFrame.cpp` | Whole window: top bar, notice, tab; then panel and confirmation popup |
| `DrawTopBar` | `LauncherFrame.cpp` | Pixel product name, tabs (Projects / Engines), launcher update when one is offered, "Developer", system / job state (opens System check), Settings |
| `DrawNotice` | `LauncherFrame.cpp` | `CurrentNotice()`; `SettingsError()` + "Start with defaults" |
| `DrawProjectsTab` | `ProjectsTab.cpp` | No projects: first-run screen (New project, Add existing, and a setup list: build tools with Install or Review, engine, projects folder with Change). Otherwise toolbar (filter, sort, Add existing, New project), list with tiles, details of the selected project (`ProjectPreview.cpp`: the editor's last frame; the last build's result; Clean build; the model always keeps a visible row selected) |
| `DrawEnginesTab` | `EnginesTab.cpp` | Same layout as Projects: version list (status, projects using it, size on disk), details of the selected version (Download or Show in folder, facts, notes with breaking changes highlighted, Remove from this computer); "dev (main)" in developer mode |
| `DrawNewProjectPanel` | `NewProjectPanel.cpp` | Name (Enter creates), location (Browse), engine selection, live validation once a name is typed, Create |
| `DrawSettingsPanel` | `SettingsPanel.cpp` | PATH, open install folder, developer mode, and licenses |
| `DrawSystemCheckPanel` | `SystemCheckPanel.cpp` | Items (status, details), MSVC installation, recheck |
| `DrawEngineUpdatePanel` | `EngineUpdatePanel.cpp` | Update / Repair / Replace confirmation: notes, may break, files to overwrite / delete; the trial's result with its steps (`DrawEngineTrialSteps`) |
| `DrawJobPanel` | `JobPanel.cpp` | Foreground job: title, step (n / total), spinner, live output, Cancel; error + output + Back. An engine trial shows its steps with their times instead of the step line |
| `DrawUpdatePanel` | `UpdatePanel.cpp` | Launcher update: version, size, notes; Update / Later / Skip, download progress + Cancel / Hide, Restart now, waiting for commands, failure + Try again |
| `DrawConfirmation` | `Confirmation.cpp` | Remove project? / Remove engine? / developer mode warning / engine commit message |

## Model

Screens receive `LauncherModel&`: read getters, call actions. Actions are queued and applied at the next frame's start;
they can be called during drawing (even inside row loops). Forms (`NewProject()`, `Filter()`, `ConfirmationInput()`) are
text edited directly by screens.

| Region | Reads | Actions |
| --- | --- | --- |
| General | `ProductName()`, `LauncherVersion()`, `Mode()`, `Tab()`, `Panel()`, `CurrentNotice()`, `SettingsError()`, `System()` | `SelectTab`, `OpenPanel(NewProject / Settings / SystemCheck)`, `ClosePanel`, `DismissNotice`, `StartWithDefaultSettings` |
| Projects | `Rows()`, `VisibleRows()`, `Selected()`, `ActionsFor(row)`, `DetailsFor(row)`, `DetailsRunning()`, `Filter()`, `Sort()`, `ProjectsFolder()` | `Select`, `SetSort`, `Create`, `Import`, `BrowseProjectsFolder`, `SetProjectsFolder`, `Open`, `Run`, `ShowInFolder`, `AskRemove`, `RemoveMissing`, `ChangeEngine(row, Update / Repair / Replace)`, `UpdateEngineFromRemote`, `AskPublishEngine` |
| Engines | `Engines()` (`Engines()`: newest first, `Problems()`), `ProjectsUsing(version)`, `DevelopmentEngineAvailable()`, `ShownEngine()`, `EngineSize(version)`, `EngineRemovalProblem(version)` | `SelectEngine`, `DownloadEngineVersion`, `ShowEngineFolder`, `AskRemoveEngine` |
| New project | `NewProject()` (`name`, `location`, `engine`), `NewProjectEngines()`, `NewProjectProblem()`, `NewProjectFolder()` | `BrowseNewProjectLocation`, `CreateProject` |
| Engine update | `PendingChange()`, `ChangeProject()`, `PlanRunning()`, `Plan()`, `PlanError()` | `ConfirmEngineChange`, `ClosePanel` |
| System check | `System()`, `Report()`, `SystemCheckRunning()` | `RunSystemCheck`, `InstallMsvc` |
| Settings | `OnPath()`, `Mode()`, `LicenseNoticesAvailable()`, `AudioTools()`, `AudioPackages()` | `SetOnPath`, `OpenInstallFolder`, `OpenLicenseNotices`, `InstallAudioTools` (job), `RemoveAudioTools`, `AskDeveloperMode`, `LeaveDeveloperMode` |
| Launcher update | `UpdatesSupported()`, `UpdateState()`, `Update()`, `UpdateNotes()`, `UpdateProgress()`, `UpdateError()`, `UpdateOffered()`, `ChecksForUpdates()` | `SetChecksForUpdates`, `CheckForUpdates`, `OpenUpdatePanel`, `DownloadUpdate`, `CancelUpdateDownload`, `SkipUpdate`, `RestartToUpdate`, `CancelUpdateRestart` |
| Engine trial | `TrialRunning()`, `TrialSteps()` (`TrialStepView`: name, status, seconds) | — |
| Confirmation | `Pending()` (`RemoveProject`, `RemoveEngine`, `CleanBuild`, `DeveloperMode`, `PublishEngine`), `PendingProject()`, `PendingEngine()`, `ConfirmationInput()` | `Confirm`, `CancelConfirmation`, `AskCleanBuild` |
| Job | `CurrentJob()` (`Title`, `State`, `StepText`, `StepIndex`, `StepCount`, `Error`), `JobFailureTitle()`, `JobOutput()` | `CancelJob`, `DismissJob` |

- Row metadata comes from `ProjectRow`: version `Version()`, a quiet arrow for `update` (the details panel offers it, with
  `updateMayBreak`), the last-opened time, type
  `info.kind` (Git: "managed by git", Unregistered: "unknown source"), `contractSupported == false`: "update the
  launcher", `folderMissing`, `info.problem`; details from `DetailsFor`: "N engine files modified" if `integrity` is not
  clean (+ Repair), `git` (branch, change count, ahead / behind, detached), `error`.
- Row actions are disabled while a foreground job runs (`ActionsFor` disables all except Remove); the panel closes
  automatically on success (Open closes the launcher).
- Notice: the title is model text, not screen text; the message beneath is the core's sentence, unchanged.

## Rules

- Screens implement no business logic: no files / processes / git / saves, no core function calls; only the model. If
  the model lacks something, add it to the model rather than imitate it in the screen. The one exception is
  `ProjectPreview.cpp`, which loads the frame's texture: a texture belongs to the window.
- Appearance uses the engine's shared tool UI: `ToolUi/ToolTheme.h` (colors `ToolToneColor`, fonts `ToolFont`, scale
  `ToolUiScale()`), `ToolUi/ToolWidgets.h`; icons are Font Awesome (`<extras/IconsFontAwesome6.h>`, `ICON_FA_*`). Theme
  / widget changes belong in the engine (shared with the editor); small launcher-specific helpers in `Screens/`.
- Text is English, in screen functions; do not spell out the product name (`ProductName()`).
- Dimensions multiply by `ToolUiScale()`; constants are named `constexpr` in the file's anonymous namespace.
- Toolbar and column-headed list remain visible when empty. The empty-state message is inside the list; empty searches
  offer Clear search. Row content is 48 logical px, with name / location and engine / status columns. Projects and
  Project details headings are separate `PanelHeader` strips; table headings are semibold.
- Projects Folder to the right of Import Project opens the folder picker. The chosen folder is validated, saved as the
  default projects folder, applied to the new-project form's location, and scanned automatically. Existing registered
  projects remain. The button tooltip shows the current path. Progress / cancellation is in the job panel; results
  appear in a notice.
- Keyboard on Projects, when nothing floats above it: Up / Down move the selection, Enter opens it, Ctrl+F searches,
  Ctrl+N starts a new project. A control reached with Tab keeps its own Enter. Right-clicking a row selects it and offers
  Open, Run, Show in folder, Copy path, Remove from list.
- The top bar's state says only what needs reading: which tool is missing or warning, with an arrow to System check. A
  healthy system is just the green mark, and the first-run screen spells out the tools itself.
- The right inspector is 316 logical px; with a visible selection, it shows project information / actions. Without
  selection, it shows the current projects folder, the same select / scan action, and a short discovery explanation; the
  toolbar beside it already has New project and Add existing. No separate Launcher tab or scale setting. In narrow views, access it from the Project details
  dropdown panel. There is no Launcher tab or separate scan row. Missing-project cleanup appears only when missing
  projects exist.
- The window follows monitor DPI scale; no user-specific UI scale setting is saved. Settings contains only installation
  / PATH, developer mode, and licenses; change the default folder through Projects Folder.