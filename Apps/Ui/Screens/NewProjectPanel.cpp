#include "Screens/LauncherScreens.h"
#include "Model/LauncherModel.h"
#include "ToolUi/ToolWidgets.h"

// Reads / edits: NewProject() (name, location, engine), NewProjectEngines(), NewProjectProblem(), NewProjectFolder().
// Calls: BrowseNewProjectLocation, CreateProject.
void DrawNewProjectPanel(LauncherModel& model) {
    (void)model;
    ToolEmptyState("New project", "Not designed yet.");
}
