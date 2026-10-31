#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <algorithm>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr const char* PopupId = "##launcher-confirmation";
constexpr float PopupWidth = 450.0f;
constexpr float PopupHeight = 300.0f;
constexpr float PopupMargin = 20.0f;
constexpr float PopupPadding = 18.0f;
constexpr float FooterGap = 10.0f;
constexpr ImGuiWindowFlags PopupWindowFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

const char* ConfirmationTitle(Confirmation confirmation) {
    switch (confirmation) {
    case Confirmation::None             : return "";
    case Confirmation::RemoveProject    : return "Remove project?";
    case Confirmation::DeveloperPassword: return "Unlock developer mode";
    case Confirmation::PublishEngine    : return "Publish engine changes";
    }
    return "";
}

const char* ConfirmLabel(Confirmation confirmation) {
    switch (confirmation) {
    case Confirmation::None             : return "";
    case Confirmation::RemoveProject    : return "Remove";
    case Confirmation::DeveloperPassword: return "Unlock";
    case Confirmation::PublishEngine    : return "Publish";
    }
    return "";
}
} // namespace

void DrawConfirmation(LauncherModel& model) {
    const Confirmation pending = model.Pending();
    if (pending != Confirmation::None && !ImGui::IsPopupOpen(PopupId)) ImGui::OpenPopup(PopupId);
    if (!ImGui::IsPopupOpen(PopupId)) return;
    const float scale = ToolUiScale();
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        {viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f},
        ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({std::min(PopupWidth * scale, viewport->WorkSize.x - PopupMargin * scale * 2),
                              std::min(PopupHeight * scale, viewport->WorkSize.y - PopupMargin * scale * 2)},
                             ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {PopupPadding * scale, PopupPadding * scale});
    if (ImGui::BeginPopupModal(PopupId, nullptr, PopupWindowFlags)) {
        if (pending == Confirmation::None) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ImGui::PopStyleVar();
            return;
        }
        const bool focusInput = ImGui::IsWindowAppearing();
        const bool checking = model.PasswordCheckRunning();
        ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::SectionSize);
        ImGui::TextUnformatted(ConfirmationTitle(pending));
        ImGui::PopFont();
        ImGui::Separator();
        bool submitted = false;
        auto& input = model.ConfirmationInput();
        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + FooterGap * scale;
        if (ImGui::BeginChild("##confirmation-body", {0, -footerHeight})) {
            if (pending == Confirmation::RemoveProject || pending == Confirmation::PublishEngine) {
                if (const auto* project = model.PendingProject()) {
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextUnformatted(project->info.name.c_str());
                    ImGui::PopTextWrapPos();
                    LauncherPathText(project->entry.root);
                }
            }
            switch (pending) {
            case Confirmation::None: break;
            case Confirmation::RemoveProject:
                LauncherMutedText("Remove this project from the launcher list? Your project files stay on disk.");
                break;
            case Confirmation::DeveloperPassword:
                LauncherMutedText("Enter the developer password to enable git engine workflows.");
                ImGui::TextUnformatted("Password");
                ImGui::SetNextItemWidth(-1);
                ImGui::BeginDisabled(checking);
                if (focusInput) ImGui::SetKeyboardFocusHere();
                submitted = LauncherInputText("##password", input.password,
                                              ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::EndDisabled();
                if (checking)
                    DrawLauncherActivity("Verifying password...");
                else if (model.PasswordRejected())
                    LauncherToneText("Password not accepted. Try again.", ToolTone::Error);
                break;
            case Confirmation::PublishEngine:
                LauncherMutedText(
                    "Commits and pushes engine changes, then records the engine revision in the project.");
                ImGui::TextUnformatted("Commit message");
                ImGui::SetNextItemWidth(-1);
                if (focusInput) ImGui::SetKeyboardFocusHere();
                submitted =
                    LauncherInputText("##commit-message", input.commitMessage, ImGuiInputTextFlags_EnterReturnsTrue);
                break;
            }
        }
        ImGui::EndChild();
        const bool canConfirm = !checking && (pending == Confirmation::RemoveProject ||
                                              (pending == Confirmation::DeveloperPassword && !input.password.empty()) ||
                                              (pending == Confirmation::PublishEngine && !input.commitMessage.empty()));
        ImGui::Separator();
        ImGui::BeginDisabled(checking);
        const bool canceled = ToolButton("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);
        ImGui::EndDisabled();
        if (canceled && !checking) {
            model.CancelConfirmation();
            ImGui::CloseCurrentPopup();
        }
        ContinueToolToolbar(ConfirmLabel(pending), true);
        ImGui::BeginDisabled(!canConfirm);
        if (pending == Confirmation::RemoveProject)
            ImGui::PushStyleColor(ImGuiCol_Text, ToolToneColor(ToolTone::Error));
        const bool confirmed = ToolButton(ConfirmLabel(pending), {}, pending != Confirmation::RemoveProject);
        if (pending == Confirmation::RemoveProject) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (!canceled && canConfirm && (confirmed || submitted)) model.Confirm();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}
