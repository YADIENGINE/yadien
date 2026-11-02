#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <algorithm>
#include <string>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float RowGap = 10.0f;
constexpr float RowTextMinWidth = 180.0f;

// One setting: what it is on the left, its control on the right edge, a hairline below.
template <typename Control>
void SettingRow(const char* id, const char* title, const char* detail, float controlWidth, Control control) {
    const float scale = ToolUiScale();
    ImGui::PushID(id);
    ImGui::Dummy({0, 0});
    const auto start = ImGui::GetCursorPos();
    const float available = ImGui::GetContentRegionAvail().x;
    const bool fits = available - controlWidth >= RowTextMinWidth * scale;
    const float textWidth = fits ? available - controlWidth - RowGap * scale : available;
    ImGui::BeginGroup();
    ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (*detail) {
        ImGui::PushTextWrapPos(start.x + textWidth);
        ImGui::TextColored(ToolPalette::Muted, "%s", detail);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
    const float textHeight = ImGui::GetItemRectSize().y;
    if (fits)
        ImGui::SetCursorPos({start.x + available - controlWidth,
                             start.y + std::max(0.0f, (textHeight - ImGui::GetFrameHeight()) * 0.5f)});
    ImGui::BeginGroup();
    control();
    ImGui::EndGroup();
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), start.y + textHeight + ImGui::GetStyle().ItemSpacing.y));
    ImGui::Separator();
    ImGui::PopID();
}
float ButtonWidth(const char* label) {
    ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
    const float width = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::PopFont();
    return width;
}
void DrawPathRow(LauncherModel& model) {
    const char* change = "Change";
    const std::string folder = LauncherShortPath(model.ProjectsFolder(), ImGui::GetContentRegionAvail().x * 0.7f);
    SettingRow("projects-folder", "Projects folder", folder.c_str(), ButtonWidth(change), [&] {
        if (ToolButton(change)) model.BrowseProjectsFolder();
        ToolTooltip("Choose the default projects folder and find the projects in it");
    });
}
void DrawPathToggleRow(LauncherModel& model) {
    const auto onPath = model.OnPath();
    SettingRow("path", "Add to PATH",
               onPath ? "Run the launcher's commands from any terminal. Reopen terminals after changing this."
                      : "PATH status is unavailable.",
               ToolToggleWidth("##path"), [&] {
                   bool enabled = onPath.value_or(false);
                   ImGui::BeginDisabled(!onPath.has_value());
                   if (ToolToggle("##path", enabled)) model.SetOnPath(enabled);
                   ImGui::EndDisabled();
               });
}
void DrawUpdatesRow(LauncherModel& model) {
    const std::string running = std::string("Launcher ") + LauncherModel::LauncherVersion();
    if (!model.UpdatesSupported()) {
        const std::string detail = running + ". Installed launchers update themselves.";
        SettingRow("updates", "Updates", detail.c_str(), 0, [] {});
        return;
    }
    const char* check = ICON_FA_ROTATE " Check now";
    const float width = ButtonWidth(check) + ImGui::GetStyle().ItemSpacing.x + ToolToggleWidth("##updates");
    SettingRow("updates", "Check for updates at startup", running.c_str(), width, [&] {
        if (ToolButton(check)) model.CheckForUpdates();
        ImGui::SameLine();
        bool automatic = model.ChecksForUpdates();
        if (ToolToggle("##updates", automatic)) model.SetChecksForUpdates(automatic);
    });
}
void DrawAudioToolsRow(LauncherModel& model) {
    const AudioToolsState state = model.AudioTools();
    if (state == AudioToolsState::NotOffered) return;
    const bool installed = state == AudioToolsState::Installed;
    std::string detail = "NumPy and SciPy for sound scripts, downloaded only when you install them, about 50 MB.";
    if (state == AudioToolsState::Outdated) detail = "Other versions are installed. Install to replace them.";
    if (installed) {
        detail = "Installed:";
        for (const AudioPackage& package : model.AudioPackages())
            detail += " " + package.name + " " + package.version;
        detail += std::string(". Run sound scripts with: ") + LAUNCHER_EXECUTABLE_NAME + " launcher python script.py";
    }
    const char* action = installed ? "Remove" : ICON_FA_DOWNLOAD " Install";
    SettingRow("audio", "Audio tools", detail.c_str(), ButtonWidth(action), [&] {
        if (ToolButton(action)) {
            if (installed)
                model.RemoveAudioTools();
            else
                model.InstallAudioTools();
        }
    });
}
void DrawDeveloperRow(LauncherModel& model) {
    SettingRow("developer", "Developer mode", "Git-managed engines and publishing engine changes.",
               ToolToggleWidth("##developer"), [&] {
                   bool enabled = model.Mode() == LauncherMode::Developer;
                   if (ToolToggle("##developer", enabled)) {
                       if (enabled)
                           model.AskDeveloperMode();
                       else
                           model.LeaveDeveloperMode();
                   }
               });
}
} // namespace

void DrawSettingsPanel(LauncherModel& model) {
    if (!model.SettingsError().empty())
        LauncherToneText("Start with defaults from the notification to enable saving.", ToolTone::Warning);
    DrawPathRow(model);
    DrawPathToggleRow(model);
    DrawUpdatesRow(model);
    DrawAudioToolsRow(model);
    DrawDeveloperRow(model);
    ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_Text, ToolPalette::Secondary);
    if (ToolButton(ICON_FA_FOLDER_OPEN " Open install folder")) model.OpenInstallFolder();
    ContinueToolToolbar(ICON_FA_FILE_LINES " Third-party licenses");
    ImGui::BeginDisabled(!model.LicenseNoticesAvailable());
    if (ToolButton(ICON_FA_FILE_LINES " Third-party licenses")) model.OpenLicenseNotices();
    ImGui::EndDisabled();
    if (!model.LicenseNoticesAvailable()) ToolTooltip("Installed launchers include the notices file.");
    ImGui::PopStyleColor(2);
}
