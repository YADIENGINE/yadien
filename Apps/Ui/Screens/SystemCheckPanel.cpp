#include "Screens/LauncherScreens.h"
#include "Model/LauncherModel.h"
#include "ToolUi/ToolWidgets.h"

// Reads: System(), Report() (items: title, status, detail, fix), SystemCheckRunning().
// Calls: RunSystemCheck, InstallMsvc (item.fix == CheckFix::InstallMsvc).
void DrawSystemCheckPanel(LauncherModel& model) {
    (void)model;
    ToolEmptyState("System check", "Not designed yet.");
}
