#include "Screens/LauncherScreens.h"
#include "Model/LauncherModel.h"
#include "ToolUi/ToolWidgets.h"

// Reads: PendingChange(), ChangeProject(), PlanRunning(), Plan() (from, target, notes, mayBreak, overwritten,
//        deleted), PlanError().
// Calls: ConfirmEngineChange, ClosePanel.
void DrawEngineUpdatePanel(LauncherModel& model) {
    (void)model;
    ToolEmptyState("Engine update", "Not designed yet.");
}
