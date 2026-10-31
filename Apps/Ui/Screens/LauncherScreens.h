#pragma once

class LauncherModel;

// The launcher's screens: one draw function per region (engine Docs/Design/README.md D15 → Düzen ve gezinme).
// Each reads the model's getters and calls its actions; none keeps state or does work of its own. Layout, sizes,
// colors and text are the screens' job; the tool theme and widgets come from the engine (ToolUi/).

// The whole window, every frame: top bar, notice, settings error, the current tab, status bar, then the open panel
// and the confirmation popup over them.
void DrawLauncher(LauncherModel& model);

// Frame regions (Screens/LauncherFrame.cpp).
void DrawTopBar(LauncherModel& model);    // Product name, tabs, system state dot, Settings
void DrawNotice(LauncherModel& model);    // CurrentNotice() + dismiss; SettingsError() + "Start with defaults"
void DrawStatusBar(LauncherModel& model); // Launcher version, "Developer mode"

// Tabs.
void DrawProjectsTab(LauncherModel& model); // Toolbar, list, action panel, empty state
void DrawEnginesTab(LauncherModel& model);  // Stable engines, their notes, usage; "dev (main)" in developer mode

// Panels (over the tab).
void DrawNewProjectPanel(LauncherModel& model);
void DrawSettingsPanel(LauncherModel& model);
void DrawSystemCheckPanel(LauncherModel& model);
void DrawEngineUpdatePanel(LauncherModel& model);
void DrawJobPanel(LauncherModel& model);

// Confirmation popup (Pending() != Confirmation::None).
void DrawConfirmation(LauncherModel& model);
