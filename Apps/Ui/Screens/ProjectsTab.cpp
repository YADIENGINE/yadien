#include "Screens/LauncherScreens.h"
#include "Model/LauncherModel.h"
#include "ToolUi/ToolWidgets.h"

// Reads: Rows(), VisibleRows(), Selected(), ActionsFor(row), DetailsFor(row), DetailsRunning(), Filter(), Sort().
// Calls: Select, SetSort, Create, Import, Scan, Open, Run, ShowInFolder, AskRemove, RemoveMissing,
//        ChangeEngine(row, Update / Repair / Replace), UpdateEngineFromRemote, AskPublishEngine.
void DrawProjectsTab(LauncherModel& model) {
    (void)model;
    ToolEmptyState("Projects", "Not designed yet.");
}
