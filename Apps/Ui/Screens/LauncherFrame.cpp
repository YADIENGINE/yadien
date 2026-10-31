#include "Screens/LauncherScreens.h"
#include <imgui.h>
#include "Model/LauncherModel.h"
#include "ToolUi/ToolWidgets.h"

// Skeleton: plain regions and the navigation that reaches every tab and panel. The visual design replaces the
// bodies; the model calls stay.
namespace {
const char* PanelTitle(LauncherPanel panel) {
    switch (panel) {
    case LauncherPanel::None        : return "";
    case LauncherPanel::NewProject  : return "New project";
    case LauncherPanel::Settings    : return "Settings";
    case LauncherPanel::SystemCheck : return "System check";
    case LauncherPanel::EngineUpdate: return "Engine update";
    case LauncherPanel::Job         : return "Job";
    }
    return "";
}

void DrawPanel(LauncherModel& model) {
    const LauncherPanel panel = model.Panel();
    if (panel == LauncherPanel::None) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        {viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f},
        ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({viewport->WorkSize.x * 0.6f, viewport->WorkSize.y * 0.6f}, ImGuiCond_Always);
    if (ImGui::Begin(PanelTitle(panel), nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings)) {
        switch (panel) {
        case LauncherPanel::None        : break;
        case LauncherPanel::NewProject  : DrawNewProjectPanel(model); break;
        case LauncherPanel::Settings    : DrawSettingsPanel(model); break;
        case LauncherPanel::SystemCheck : DrawSystemCheckPanel(model); break;
        case LauncherPanel::EngineUpdate: DrawEngineUpdatePanel(model); break;
        case LauncherPanel::Job         : DrawJobPanel(model); break;
        }
        if (panel != LauncherPanel::Job && ImGui::Button("Close")) model.ClosePanel();
    }
    ImGui::End();
}
} // namespace

void DrawLauncher(LauncherModel& model) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##launcher", nullptr, flags)) {
        DrawTopBar(model);
        DrawNotice(model);
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("##tab", {0, -statusHeight})) {
            if (model.Tab() == LauncherTab::Projects)
                DrawProjectsTab(model);
            else
                DrawEnginesTab(model);
        }
        ImGui::EndChild();
        DrawStatusBar(model);
    }
    ImGui::End();
    DrawPanel(model);
    DrawConfirmation(model);
}

void DrawTopBar(LauncherModel& model) {
    ImGui::TextUnformatted(LauncherModel::ProductName());
    ImGui::SameLine();
    if (ToolSegment("Projects", model.Tab() == LauncherTab::Projects)) model.SelectTab(LauncherTab::Projects);
    ImGui::SameLine();
    if (ToolSegment("Engines", model.Tab() == LauncherTab::Engines)) model.SelectTab(LauncherTab::Engines);
    ImGui::SameLine();
    if (ImGui::Button("System")) model.OpenPanel(LauncherPanel::SystemCheck);
    ImGui::SameLine();
    if (ImGui::Button("Settings")) model.OpenPanel(LauncherPanel::Settings);
    ImGui::Separator();
}

void DrawNotice(LauncherModel& model) {
    if (const auto& notice = model.CurrentNotice()) {
        ImGui::Text("%s: %s", notice->title.c_str(), notice->message.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss")) model.DismissNotice();
    }
    if (!model.SettingsError().empty()) {
        ImGui::TextUnformatted(model.SettingsError().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Start with defaults")) model.StartWithDefaultSettings();
    }
}

void DrawStatusBar(LauncherModel& model) {
    ImGui::Separator();
    ImGui::Text("Launcher %s", LauncherModel::LauncherVersion());
    if (model.Mode() == LauncherMode::Developer) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Developer mode");
    }
}
