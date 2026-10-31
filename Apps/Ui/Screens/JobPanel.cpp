#include "Screens/LauncherScreens.h"
#include <imgui.h>
#include "Model/LauncherModel.h"
#include "ToolUi/ToolWidgets.h"

// Reads: CurrentJob() (Title, State, StepText / StepIndex / StepCount, Error), JobFailureTitle(), JobOutput().
// Calls: CancelJob while running; DismissJob after a failure or cancel. Success closes the panel by itself.
void DrawJobPanel(LauncherModel& model) {
    const BackgroundJob* job = model.CurrentJob();
    if (!job) return;
    ToolEmptyState(job->Title().c_str(), "Not designed yet.");
    if (!job->Finished()) {
        if (ImGui::Button("Cancel")) model.CancelJob();
    } else if (ImGui::Button("Back")) {
        model.DismissJob();
    }
}
