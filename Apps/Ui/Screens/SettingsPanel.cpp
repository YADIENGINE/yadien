#include "Screens/LauncherScreens.h"
#include "Model/LauncherModel.h"
#include "ToolUi/ToolWidgets.h"

// Reads: Mode(), UiScalePercent(), OnPath(), ProjectsFolder(), DeveloperModeAvailable().
// Calls: SetUiScalePercent, SetOnPath, OpenInstallFolder, BrowseProjectsFolder, AskDeveloperMode, LeaveDeveloperMode.
void DrawSettingsPanel(LauncherModel& model) {
    (void)model;
    ToolEmptyState("Settings", "Not designed yet.");
}
