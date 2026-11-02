#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <cmath>
#include <string>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"
#include "Update/UpdateNotes.h"

namespace {
constexpr float DetailsWidth = 340.0f;
constexpr float ListMinWidth = 360.0f;
constexpr float ListPadding = 12.0f;
constexpr float DetailsPadding = 16.0f;
constexpr float NarrowListShare = 0.42f;
constexpr float RowHeight = 40.0f;
constexpr float RowInset = 12.0f;
constexpr float StatusColumn = 96.0f; // From the row's start
constexpr float UsageColumn = 226.0f;
constexpr float SizeColumnWidth = 84.0f;
constexpr float DetailLabelWidth = 76.0f;

std::string UsageText(int projects) {
    if (projects == 0) return "None";
    return std::to_string(projects) + (projects == 1 ? " project" : " projects");
}

struct VersionState {
    const char* text;
    ImVec4 color;
};
VersionState StateOf(const EngineLibrary& library, const EngineVersion& version, bool latest) {
    if (latest) return {"Latest", ToolToneColor(ToolTone::Success)};
    if (library.Find(version)) return {"Installed", ToolPalette::Secondary};
    return {"Online", ToolPalette::Muted};
}

std::string SizeText(const LauncherModel& model, const EngineVersion& version) {
    const auto size = model.EngineSize(version);
    return size && *size > 0 ? FormatDownloadSize(*size) : std::string();
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

void DrawColumnHeadings(float width) {
    const float scale = ToolUiScale();
    const auto start = ImGui::GetCursorScreenPos();
    const float inset = RowInset * scale;
    ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::CaptionSize);
    auto* draw = ImGui::GetWindowDrawList();
    const auto color = ImGui::GetColorU32(ToolPalette::Muted);
    draw->AddText({start.x + inset, start.y}, color, "Version");
    draw->AddText({start.x + StatusColumn * scale, start.y}, color, "Status");
    draw->AddText({start.x + UsageColumn * scale, start.y}, color, "Used by");
    constexpr const char* Size = "Size";
    draw->AddText({start.x + width - inset - ImGui::CalcTextSize(Size).x, start.y}, color, Size);
    ImGui::Dummy({width, ImGui::GetTextLineHeight()});
    ImGui::PopFont();
}

void DrawVersionRow(LauncherModel& model, const EngineVersion& version, bool latest, bool selected) {
    const float scale = ToolUiScale();
    const float height = RowHeight * scale;
    const float inset = RowInset * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const auto start = ImGui::GetCursorScreenPos();
    const std::string text = version.ToString();
    ImGui::PushID(text.c_str());
    if (ImGui::Selectable("##engine", selected, 0, {0, height})) model.SelectEngine(version);
    ImGui::PopID();
    auto* draw = ImGui::GetWindowDrawList();
    const float textY = start.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
    draw->AddText({start.x + inset, textY}, ImGui::GetColorU32(ToolPalette::Text), text.c_str());
    ImGui::PopFont();
    const auto state = StateOf(model.Engines(), version, latest);
    draw->AddText({start.x + StatusColumn * scale, textY}, ImGui::GetColorU32(state.color), state.text);
    const int projects = model.ProjectsUsing(version);
    draw->AddText({start.x + UsageColumn * scale, textY},
                  ImGui::GetColorU32(projects > 0 ? ToolPalette::Secondary : ToolPalette::Muted),
                  UsageText(projects).c_str());
    const std::string size = SizeText(model, version);
    ImGui::PushFont(ToolFont(ToolFontRole::Technical), ToolMetrics::CaptionSize);
    const float sizeY = start.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
    if (width > (UsageColumn + SizeColumnWidth) * scale)
        draw->AddText({start.x + width - inset - ImGui::CalcTextSize(size.c_str()).x, sizeY},
                      ImGui::GetColorU32(ToolPalette::Muted), size.c_str());
    ImGui::PopFont();
}

void DrawVersionList(LauncherModel& model, const std::vector<EngineVersion>& versions) {
    const auto& library = model.Engines();
    for (const auto& problem : library.Problems())
        LauncherToneText(problem.c_str(), ToolTone::Error);
    if (model.DevelopmentEngineAvailable()) {
        ToolStatusBadge(ICON_FA_CODE " dev (main)", ToolToneColor(ToolTone::Accent));
        LauncherMutedText("Development source. Available when creating a developer project.");
    }
    if (versions.empty()) {
        ToolEmptyState("No stable engines installed", "Open System check to review the installation.");
        if (ToolButton("System check")) model.OpenPanel(LauncherPanel::SystemCheck);
    } else {
        DrawColumnHeadings(ImGui::GetContentRegionAvail().x);
        const auto shown = model.ShownEngine();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, ToolMetrics::Space * ToolUiScale()});
        for (std::size_t index = 0; index < versions.size(); ++index)
            DrawVersionRow(model, versions[index], index == 0, shown && *shown == versions[index]);
        ImGui::PopStyleVar();
    }
    DrawCatalogState(model);
}

void DetailFact(const char* label, const std::string& value) {
    ImGui::TextColored(ToolPalette::Muted, "%s", label);
    ImGui::SameLine(DetailLabelWidth * ToolUiScale() + ImGui::GetStyle().WindowPadding.x);
    ImGui::TextUnformatted(value.c_str());
}

void DrawVersionDetails(LauncherModel& model, const EngineVersion& version, bool latest) {
    const auto& library = model.Engines();
    const AvailableEngine* engine = library.Find(version);
    ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::HeadingSize);
    ImGui::Text("Engine %s", version.ToString().c_str());
    ImGui::PopFont();
    if (engine) {
        if (ToolSingleLineButton(ICON_FA_FOLDER_OPEN " Show in folder")) model.ShowEngineFolder(version);
    } else if (ToolSingleLineButton(ICON_FA_DOWNLOAD " Download", true)) {
        model.DownloadEngineVersion(version);
    }
    ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
    const auto state = StateOf(library, version, latest);
    DetailFact("Status", state.text);
    DetailFact("Used by", UsageText(model.ProjectsUsing(version)));
    if (engine) {
        DetailFact("Source", engine->downloaded ? "Downloaded" : "Comes with the launcher");
        if (const std::string size = SizeText(model, version); !size.empty()) DetailFact("Size", size);
        ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
        LauncherPathText(engine->folder);
        if (ToolSectionHeader("Release notes")) DrawLauncherNotes(engine->manifest.notes);
        const std::string problem = model.EngineRemovalProblem(version);
        ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
        ImGui::Separator();
        ImGui::BeginDisabled(!problem.empty());
        ImGui::PushStyleColor(ImGuiCol_Text, ToolToneColor(ToolTone::Error));
        const bool remove = ToolSingleLineButton(ICON_FA_XMARK " Remove from this computer");
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        ToolTooltip(problem.c_str());
        if (remove) model.AskRemoveEngine(version);
    } else {
        ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
        LauncherMutedText("Not on this computer. A project choosing it downloads it. Its release notes come with it.");
    }
}
} // namespace

void DrawEnginesTab(LauncherModel& model) {
    const float scale = ToolUiScale();
    const std::vector<EngineVersion> versions = model.Engines().KnownVersions();
    const auto shown = model.ShownEngine();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float detailsWidth = DetailsWidth * scale;
    const bool sideBySide = available.x >= detailsWidth + ListMinWidth * scale;
    const ImVec2 listSize{sideBySide ? available.x - detailsWidth : 0,
                          sideBySide || !shown ? 0 : std::round(available.y * NarrowListShare)};
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ToolMetrics::Gap * scale, ToolMetrics::Gap * scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    if (BeginToolPanel("##engine-library", listSize, ToolSurface::Workspace, 0, 0, ListPadding))
        DrawVersionList(model, versions);
    EndToolPanel();
    if (shown) {
        if (sideBySide) ImGui::SameLine(0, 0);
        if (BeginToolPanel("##engine-details", {0, 0}, ToolSurface::Utility, 0, 0, DetailsPadding))
            DrawVersionDetails(model, *shown, !versions.empty() && versions.front() == *shown);
        EndToolPanel();
    }
    ImGui::PopStyleVar(2);
}
