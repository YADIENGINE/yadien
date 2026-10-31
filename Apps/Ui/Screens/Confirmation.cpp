#include "Screens/LauncherScreens.h"
#include <imgui.h>
#include "Model/LauncherModel.h"
#include "ToolUi/ToolWidgets.h"

// Reads: Pending(), PendingProject(), ConfirmationInput() (password, commitMessage), PasswordCheckRunning(),
//        PasswordRejected().
// Calls: Confirm, CancelConfirmation.
void DrawConfirmation(LauncherModel& model) {
    constexpr const char* Id = "Confirm##launcher";
    if (model.Pending() == Confirmation::None) return;
    if (!ImGui::IsPopupOpen(Id)) ImGui::OpenPopup(Id);
    if (ImGui::BeginPopupModal(Id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ToolEmptyState("Confirmation", "Not designed yet.");
        if (ImGui::Button("Cancel")) {
            model.CancelConfirmation();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
