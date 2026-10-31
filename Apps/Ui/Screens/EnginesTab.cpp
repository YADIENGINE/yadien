#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <string>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float CardPadding = ToolMetrics::Padding;
}

void DrawEnginesTab(LauncherModel& model) {
    if (BeginToolPanel("##engines-toolbar", {0, 0}, ToolSurface::Toolbar, ImGuiChildFlags_AutoResizeY)) {
        ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::SectionSize);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Installed engines");
        ImGui::PopFont();
        ContinueToolToolbar("Stable releases", true);
        ImGui::TextDisabled("Stable releases");
    }
    EndToolPanel();
    if (BeginToolPanel("##engine-library", {0, 0}, ToolSurface::Workspace)) {
        const auto& library = model.Engines();
        for (const auto& problem : library.Problems())
            LauncherToneText(problem.c_str(), ToolTone::Error);
        if (model.DevelopmentEngineAvailable()) {
            ToolStatusBadge(ICON_FA_CODE " dev (main)", ToolToneColor(ToolTone::Accent));
            LauncherMutedText("Development source. Available when creating a developer project.");
            ImGui::Separator();
        }
        if (library.Engines().empty()) {
            ToolEmptyState("No stable engines installed", "Open System check to review the installation.");
            if (ToolButton("System check")) model.OpenPanel(LauncherPanel::SystemCheck);
        }
        std::size_t index = 0;
        for (const auto& engine : library.Engines()) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                {CardPadding * ToolUiScale(), CardPadding * ToolUiScale()});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ToolSurfaceColor(ToolSurface::Content));
            if (ImGui::BeginChild("##engine", {0, 0},
                                  ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding |
                                      ImGuiChildFlags_AutoResizeY)) {
                const std::string version = engine.manifest.version.ToString();
                ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::SectionSize);
                ImGui::Text("Engine %s", version.c_str());
                ImGui::PopFont();
                const char* badge = index == 0 ? "Latest stable" : "Stable";
                ContinueToolToolbar(badge);
                ToolStatusBadge(badge, ToolToneColor(ToolTone::Success));
                const std::string usage =
                    std::to_string(model.ProjectsUsing(engine.manifest.version)) + " projects using this series";
                ContinueToolToolbar(usage.c_str(), true);
                ImGui::TextDisabled("%s", usage.c_str());
                LauncherPathText(engine.folder);
                if (ToolSectionHeader("Release notes")) DrawLauncherNotes(engine.manifest.notes);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::PopID();
            ++index;
        }
    }
    EndToolPanel();
}
