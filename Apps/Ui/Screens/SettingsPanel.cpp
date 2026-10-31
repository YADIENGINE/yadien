#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float FooterGap = 10.0f;
} // namespace

void DrawSettingsPanel(LauncherModel& model) {
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + FooterGap * ToolUiScale();
    if (ImGui::BeginChild("##settings-body", {0, -footerHeight})) {
        LauncherMutedText(model.SettingsError().empty()
                              ? "Changes are saved automatically."
                              : "Start with defaults from the notification to enable saving.");
        if (ToolSectionHeader("Installation")) {
            const auto onPath = model.OnPath();
            bool enabled = onPath.value_or(false);
            ImGui::BeginDisabled(!onPath.has_value());
            if (ImGui::Checkbox("Add launcher to PATH", &enabled)) model.SetOnPath(enabled);
            ImGui::EndDisabled();
            if (!onPath) LauncherMutedText("PATH status is unavailable.");
            LauncherMutedText("Open a new terminal after changing PATH.");
            if (ToolSingleLineButton(ICON_FA_FOLDER_OPEN " Open installation folder")) model.OpenInstallFolder();
        }
        if (ToolSectionHeader("Developer mode", false)) {
            LauncherMutedText("Use git-managed engines and publish engine changes.");
            if (model.Mode() == LauncherMode::Developer) {
                ToolStatusBadge(ICON_FA_CODE " Enabled", ToolToneColor(ToolTone::Accent));
                if (ToolButton("Leave developer mode")) model.LeaveDeveloperMode();
            } else {
                if (ToolButton(ICON_FA_CODE " Enter developer mode")) model.AskDeveloperMode();
            }
        }
        if (ToolSectionHeader("Open source licenses", false)) {
            LauncherMutedText("The launcher, its engines and its bundled tools are open source. The notices file "
                              "lists every license.");
            ImGui::BeginDisabled(!model.LicenseNoticesAvailable());
            if (ToolSingleLineButton(ICON_FA_FILE_LINES " Open third-party notices")) model.OpenLicenseNotices();
            ImGui::EndDisabled();
            if (!model.LicenseNoticesAvailable()) LauncherMutedText("Installed launchers include the notices file.");
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (ToolButton("Done", {}, true)) model.ClosePanel();
}
