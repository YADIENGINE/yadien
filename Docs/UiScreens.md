# Launcher screens (`Apps/Ui`)

`<engine>.exe` is a thin presentation layer over `LauncherCore`. `LauncherModel` owns state, navigation, jobs, saves,
and notices; screen functions only read the model and queue actions. The window uses the engine's shared `EngineToolUi`.

## Ownership

| Function | File | Responsibility |
| --- | --- | --- |
| `DrawLauncher` / `DrawTopBar` / `DrawNotice` | `LauncherFrame.cpp` | Window frame, tabs, global state, notices, panels, and confirmations |
| `DrawProjectsTab` | `ProjectsTab.cpp` | Project search, sorting, list, selection, details, and first-run actions |
| `DrawEnginesTab` | `EnginesTab.cpp` | Engine versions, local state, usage, release notes, download, and removal |
| `DrawNewProjectPanel` | `NewProjectPanel.cpp` | Name, location, engine selection, validation, and creation |
| `DrawSettingsPanel` | `SettingsPanel.cpp` | PATH, install folder, developer mode, licenses, and optional audio tools |
| `DrawSystemCheckPanel` | `SystemCheckPanel.cpp` | Tool status, MSVC installation, and recheck |
| `DrawEngineUpdatePanel` | `EngineUpdatePanel.cpp` | Review, trial, migration report, repair, replace, and apply states |
| `DrawJobPanel` | `JobPanel.cpp` | Foreground progress, output, cancellation, and failure |
| `DrawUpdatePanel` | `UpdatePanel.cpp` | Launcher update offer, download, restart wait, skip, and retry states |
| `DrawConfirmation` | `Confirmation.cpp` | Destructive and developer-mode confirmations |

`ProjectPreview.cpp` is the only screen helper that directly loads a resource: the selected project's last-frame texture
belongs to the window. Platform folder selection is isolated in `Dialogs/FolderDialog`.

## Model contract

Actions requested during drawing are queued and applied at the start of the next frame, so they are safe inside row
loops. Screens may edit model-owned form strings directly, but they must not access files, processes, git, settings, or
core services. Add missing state or behavior to `LauncherModel` instead of reproducing it in a screen.

The model also enforces global behavior: foreground jobs disable conflicting actions, successful panels close at the
appropriate transition, Open exits the launcher after starting the editor, and errors pass from core to notices without
screen-side rewriting.

## UI rules

- Use `ToolUi/ToolTheme.h`, `ToolUi/ToolWidgets.h`, Font Awesome icons, and `ToolUiScale()` for dimensions. Shared theme
  or widget changes belong in the engine.
- Keep screen text in English and obtain the product name from `ProductName()`.
- Projects and Engines retain their toolbar and column headings when empty; empty searches offer a clear action.
- The project list always keeps a visible row selected when possible. Its inspector contains project actions; without a
  selection it shows project-folder discovery. There is no separate Launcher tab or scan row.
- Changing Projects Folder validates and saves the folder, updates the new-project form, and scans it without removing
  already registered projects.
- With no modal or panel open, Projects supports Up/Down selection, Enter to open, Ctrl+F to search, Ctrl+N for a new
  project, and a context menu for common row actions.
- System state in the top bar shows only actionable warnings; a healthy system is represented by its status mark.
- The layout follows monitor DPI. There is no persisted UI-scale setting.

Business rules and service contracts are documented in [Core API](CoreApi.md); engine distribution and migration
behavior are documented in [Engine versions](Engines.md).
