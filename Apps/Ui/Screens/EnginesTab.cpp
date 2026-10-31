#include "Screens/LauncherScreens.h"
#include "Model/LauncherModel.h"
#include "ToolUi/ToolWidgets.h"

// Reads: Engines() (Engines(): newest first; Problems()), ProjectsUsing(version), DevelopmentEngineAvailable().
void DrawEnginesTab(LauncherModel& model) {
    (void)model;
    ToolEmptyState("Engines", "Not designed yet.");
}
