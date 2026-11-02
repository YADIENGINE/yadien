#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <string>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float PagePadding = 16.0f;
constexpr float CardPadding = 14.0f;

std::string UsageText(int projects) {
    if (projects == 0) return "No projects use this version";
    return "Used by " + std::to_string(projects) + (projects == 1 ? " project" : " projects");
}

void DrawHeading(const EngineVersion& version, bool latest) {
    ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::HeadingSize);
    ImGui::Text("Engine %s", version.ToString().c_str());
    ImGui::PopFont();
    if (latest) {
        ContinueToolToolbar("Latest stable");
        ToolStatusBadge("Latest stable", ToolToneColor(ToolTone::Success));
    }
}

void DrawCatalogState(LauncherModel& model) {
    if (model.EngineCatalogRunning()) {
        DrawLauncherActivity("Looking for other engine versions...");
    } else if (!model.EngineCatalogError().empty()) {
        LauncherMutedText("Other engine versions could not be listed. These are the ones on this computer.");
        ToolTooltip(model.EngineCatalogError().c_str());
        ContinueToolToolbar("Try again", true);
        if (ToolButton("Try again")) model.RetryEngineCatalog();
    }
}
} // namespace

void DrawEnginesTab(LauncherModel& model) {
    const float scale = ToolUiScale();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ToolMetrics::Gap * scale, ToolMetrics::Gap * scale});
    if (BeginToolPanel("##engine-library", {0, 0}, ToolSurface::Workspace, 0, 0, PagePadding)) {
        const auto& library = model.Engines();
        for (const auto& problem : library.Problems())
            LauncherToneText(problem.c_str(), ToolTone::Error);
        if (model.DevelopmentEngineAvailable()) {
            ToolStatusBadge(ICON_FA_CODE " dev (main)", ToolToneColor(ToolTone::Accent));
            LauncherMutedText("Development source. Available when creating a developer project.");
        }
        DrawCatalogState(model);
        const std::vector<EngineVersion> versions = library.KnownVersions();
        if (versions.empty()) {
            ToolEmptyState("No stable engines installed", "Open System check to review the installation.");
            if (ToolButton("System check")) model.OpenPanel(LauncherPanel::SystemCheck);
        }
        std::size_t index = 0;
        for (const EngineVersion& version : versions) {
            ImGui::PushID(static_cast<int>(index));
            if (BeginToolPanel("##engine", {0, 0}, ToolSurface::Utility, ImGuiChildFlags_AutoResizeY, 0, CardPadding)) {
                DrawHeading(version, index == 0);
                const std::string usage = UsageText(model.ProjectsUsing(version));
                LauncherMutedText(usage.c_str());
                if (const AvailableEngine* engine = library.Find(version)) {
                    LauncherPathText(engine->folder);
                    if (ToolSectionHeader("Release notes", index == 0)) DrawLauncherNotes(engine->manifest.notes);
                } else {
                    LauncherMutedText("Not on this computer. A project choosing it downloads it.");
                    if (ToolButton(ICON_FA_DOWNLOAD " Download")) model.DownloadEngineVersion(version);
                }
            }
            EndToolPanel();
            ImGui::PopID();
            ++index;
        }
    }
    EndToolPanel();
    ImGui::PopStyleVar();
}
