#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float FooterGap = 10.0f;

ToolTone CheckTone(CheckStatus status) {
    switch (status) {
    case CheckStatus::Ok     : return ToolTone::Success;
    case CheckStatus::Warning: return ToolTone::Warning;
    case CheckStatus::Missing: return ToolTone::Error;
    }
    return ToolTone::Information;
}

const char* CheckLabel(CheckStatus status) {
    switch (status) {
    case CheckStatus::Ok     : return ICON_FA_CHECK " Ready";
    case CheckStatus::Warning: return ICON_FA_TRIANGLE_EXCLAMATION " Warning";
    case CheckStatus::Missing: return ICON_FA_XMARK " Missing";
    }
    return "";
}
} // namespace

void DrawSystemCheckPanel(LauncherModel& model) {
    const bool checking = model.SystemCheckRunning();
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + FooterGap * ToolUiScale();
    if (ImGui::BeginChild("##system-report", {0, -footerHeight})) {
        if (checking)
            DrawLauncherActivity("Checking build tools...");
        else if (model.System() == SystemState::Ready)
            LauncherToneText(ICON_FA_CHECK " Ready to build projects", ToolTone::Success);
        else if (model.System() == SystemState::Warning)
            LauncherToneText("Ready with warnings", ToolTone::Warning);
        else if (model.System() == SystemState::NotReady)
            LauncherToneText("Setup needs attention before projects can build.", ToolTone::Error);
        LauncherMutedText("Tool versions and installation status on this computer.");
        if (const auto& report = model.Report()) {
            for (const auto& item : report->items) {
                ImGui::PushID(item.id.c_str());
                ImGui::Separator();
                ImGui::TextUnformatted(item.title.c_str());
                ContinueToolToolbar(CheckLabel(item.status));
                ToolStatusBadge(CheckLabel(item.status), ToolToneColor(CheckTone(item.status)));
                ImGui::PushFont(ToolFont(ToolFontRole::Technical), 0);
                ImGui::PushTextWrapPos(0);
                ImGui::TextUnformatted(item.detail.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopFont();
                if (item.fix == CheckFix::InstallMsvc) {
                    ImGui::BeginDisabled(checking || model.CurrentJob() != nullptr);
                    if (ToolButton(ICON_FA_DOWNLOAD " Install C++ Build Tools")) model.InstallMsvc();
                    ImGui::EndDisabled();
                }
                ImGui::PopID();
            }
        } else {
            ToolEmptyState("Waiting for system report", "The first check may take a moment.");
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (ToolButton("Back")) model.ClosePanel();
    ContinueToolToolbar(ICON_FA_ARROWS_ROTATE " Check again", true);
    ImGui::BeginDisabled(checking);
    if (ToolButton(ICON_FA_ARROWS_ROTATE " Check again", {}, true)) model.RunSystemCheck();
    ImGui::EndDisabled();
}
