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
constexpr float UtilityWidth = 316.0f;
constexpr float ListMinWidth = 360.0f;
constexpr float ListMinHeight = 112.0f;
constexpr float FilterMinWidth = 140.0f;
constexpr float SortWidth = 148.0f;
constexpr float RowHeight = 48.0f;
constexpr float RowInset = 6.0f;
constexpr float EngineColumnWidth = 150.0f;
constexpr float UtilityPopupHeight = 600.0f;
constexpr float SelectedAccentWidth = 2.0f;

const char* EngineSource(EngineKind kind) {
    switch (kind) {
    case EngineKind::Copy        : return "Stable copy";
    case EngineKind::Git         : return "Managed by git";
    case EngineKind::Unregistered: return "Unknown source";
    case EngineKind::Missing     : return "Engine missing";
    }
    return "";
}

void DrawProjectRow(LauncherModel& model, std::size_t index) {
    const auto& row = model.Rows()[index];
    const bool selected = model.Selected() == index;
    const float scale = ToolUiScale();
    const float inset = RowInset * scale;
    ImGui::PushID(static_cast<int>(index));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const auto start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = RowHeight * scale;
    if (ImGui::Selectable("##project", selected,
                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, {0, height})) {
        model.Select(index);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && model.ActionsFor(index).open) model.Open(index);
    }
    const auto utf8 = row.entry.root.u8string();
    const std::string path(utf8.begin(), utf8.end());
    const std::string tooltip = row.info.name + "\n" + path + (row.info.problem.empty() ? "" : "\n" + row.info.problem);
    ToolTooltip(tooltip.c_str());
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec4 clip{start.x + inset, start.y, start.x + width - inset, start.y + height};
    ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {start.x + inset, start.y + inset},
                  ImGui::GetColorU32(ImGuiCol_Text), row.info.name.c_str(), nullptr, 0, &clip);
    ImGui::PopFont();
    ImGui::PushFont(ToolFont(ToolFontRole::Technical), ToolMetrics::CaptionSize);
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                  {start.x + inset, start.y + height - inset - ImGui::GetTextLineHeight()},
                  ImGui::GetColorU32(ImGuiCol_TextDisabled), path.c_str(), nullptr, 0, &clip);
    ImGui::PopFont();
    if (selected)
        draw->AddRectFilled(start, {start.x + SelectedAccentWidth * scale, start.y + height},
                            ImGui::GetColorU32(ToolToneColor(ToolTone::Accent)));
    ImGui::TableNextColumn();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + inset);
    const bool issue = row.folderMissing || !row.info.problem.empty() || !row.contractSupported;
    const std::string engineVersion = row.Version();
    const std::string version =
        (issue ? ICON_FA_TRIANGLE_EXCLAMATION " " : "") + (engineVersion.empty() ? "Unavailable" : engineVersion);
    ImGui::TextColored(issue ? ToolToneColor(ToolTone::Warning) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s",
                       version.c_str());
    ImGui::PushFont(ToolFont(ToolFontRole::Ui), ToolMetrics::CaptionSize);
    ImGui::TextColored(row.update ? ToolToneColor(ToolTone::Accent) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                       "%s",
                       row.folderMissing ? "Folder missing"
                       : row.update      ? "Update available"
                                         : EngineSource(row.info.kind));
    if (row.update) ToolTooltip(row.update->ToString().c_str());
    ImGui::PopFont();
    ImGui::PopID();
}

bool ActionButton(const char* label, bool enabled, bool primary = false) {
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ToolSingleLineButton(label, primary);
    ImGui::EndDisabled();
    return pressed;
}

void DrawProjectActions(LauncherModel& model) {
    const auto selected = model.Selected();
    if (!selected || std::ranges::find(model.VisibleRows(), *selected) == model.VisibleRows().end()) {
        ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::SectionSize);
        ImGui::TextUnformatted("No project selected");
        ImGui::PopFont();
        LauncherMutedText("Select a project in the list to inspect its engine and available actions.");
        ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
        if (BeginToolPanel("##folder-summary", {0, 0}, ToolSurface::Content, ImGuiChildFlags_AutoResizeY, 0,
                           ToolMetrics::Gap)) {
            ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::CaptionSize);
            ImGui::TextDisabled("PROJECTS FOLDER");
            ImGui::PopFont();
            LauncherPathText(model.ProjectsFolder());
            if (ToolSingleLineButton(ICON_FA_FOLDER_OPEN " Projects Folder...")) model.BrowseProjectsFolder();
            ToolTooltip("Choose the default projects folder and discover projects in it automatically");
        }
        EndToolPanel();
        if (ToolSectionHeader("Quick actions")) {
            if (ToolSingleLineButton(ICON_FA_PLUS " Create Project")) model.Create();
            if (ToolSingleLineButton(ICON_FA_FOLDER_OPEN " Import Project")) model.Import();
        }
        if (ToolSectionHeader("Project discovery")) {
            LauncherMutedText("Projects Folder finds projects in the selected folder and its subfolders.");
            LauncherMutedText("New projects start in this folder. Import Project adds one existing project.");
        }
        return;
    }
    const auto index = *selected;
    const auto& row = model.Rows()[index];
    const auto actions = model.ActionsFor(index);
    ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::SectionSize);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(row.info.name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    LauncherPathText(row.entry.root);
    ImGui::Text("Engine %s", row.Version().empty() ? "unavailable" : row.Version().c_str());
    LauncherMutedText(EngineSource(row.info.kind));
    if (row.update)
        LauncherToneText(row.updateMayBreak ? "Engine update available (breaking changes)" : "Engine update available",
                         ToolTone::Accent);
    if (ActionButton(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Edit / Open project", actions.open, true)) model.Open(index);
    if (ActionButton(ICON_FA_PLAY " Run project", actions.run)) model.Run(index);
    if (ActionButton(ICON_FA_FOLDER_OPEN " Show in folder", actions.showInFolder)) model.ShowInFolder(index);
    if (ToolSectionHeader("Engine actions")) {
        if (ActionButton(ICON_FA_ARROW_UP " Update engine", actions.updateEngine))
            model.ChangeEngine(index, EngineChange::Update);
        if (ActionButton(ICON_FA_WRENCH " Repair engine", actions.repairEngine))
            model.ChangeEngine(index, EngineChange::Repair);
        if (ActionButton(ICON_FA_ARROWS_ROTATE " Replace engine", actions.replaceEngine))
            model.ChangeEngine(index, EngineChange::Replace);
        if (model.Mode() == LauncherMode::Developer) {
            if (ActionButton(ICON_FA_CLOUD_ARROW_DOWN " Update from remote", actions.engineGit))
                model.UpdateEngineFromRemote(index);
            if (ActionButton(ICON_FA_CLOUD_ARROW_UP " Publish changes", actions.engineGit))
                model.AskPublishEngine(index);
        }
    }
    if (ToolSectionHeader("Project metadata")) {
        if (row.folderMissing) LauncherToneText("The project folder is missing.", ToolTone::Error);
        if (!row.contractSupported) LauncherToneText("Update the launcher to use this engine.", ToolTone::Error);
        if (!row.info.problem.empty()) LauncherToneText(row.info.problem.c_str(), ToolTone::Error);
        if (const auto* details = model.DetailsFor(index)) {
            if (!details->error.empty()) LauncherToneText(details->error.c_str(), ToolTone::Warning);
            if (details->integrity) {
                const auto& integrity = *details->integrity;
                if (integrity.Clean()) {
                    LauncherToneText("Engine files unchanged", ToolTone::Success);
                } else {
                    const std::string counts = std::to_string(integrity.modified.size()) + " modified, " +
                                               std::to_string(integrity.missing.size()) + " missing, " +
                                               std::to_string(integrity.added.size()) + " added";
                    LauncherToneText(counts.c_str(), ToolTone::Warning);
                    LauncherMutedText("Repair restores the stable engine copy.");
                }
            }
            if (details->git) {
                const auto& git = *details->git;
                ImGui::PushTextWrapPos(0);
                ImGui::Text("Branch: %s", git.Detached() ? "Detached HEAD" : git.branch.c_str());
                ImGui::Text("%d changed files", git.changedFiles);
                if (git.hasUpstream) ImGui::Text("%d ahead / %d behind", git.ahead, git.behind);
                if (git.pointerChanged) LauncherToneText("Engine pointer changed", ToolTone::Warning);
                ImGui::PopTextWrapPos();
            }
        } else if (model.DetailsRunning()) {
            DrawLauncherActivity("Checking details");
        }
        LauncherMutedText("Last opened (UTC)");
        LauncherMutedText(row.entry.lastOpened.empty() ? "Never opened" : row.entry.lastOpened.c_str());
    }
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ToolToneColor(ToolTone::Error));
    if (ActionButton(ICON_FA_XMARK " Remove from list", actions.remove)) model.AskRemove(index);
    ImGui::PopStyleColor();
}

void DrawUtility(LauncherModel& model) {
    if (BeginToolPanel("##inspector-header", {0, 0}, ToolSurface::PanelHeader, ImGuiChildFlags_AutoResizeY, 0,
                       ToolMetrics::Gap)) {
        ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::SectionSize);
        ImGui::TextUnformatted("Project details");
        ImGui::PopFont();
    }
    EndToolPanel();
    if (ImGui::BeginChild("##utility-body", {0, 0})) {
        DrawProjectActions(model);
    }
    ImGui::EndChild();
}

void DrawProjectToolbar(LauncherModel& model) {
    if (BeginToolPanel("##project-toolbar", {0, 0}, ToolSurface::Toolbar, ImGuiChildFlags_AutoResizeY, 0,
                       ToolMetrics::Gap)) {
        if (ToolButton(ICON_FA_PLUS " Create Project", {}, true)) model.Create();
        ContinueToolToolbar(ICON_FA_FOLDER_OPEN " Import Project");
        if (ToolButton(ICON_FA_FOLDER_OPEN " Import Project")) model.Import();
        ContinueToolToolbar(ICON_FA_FOLDER_OPEN " Projects Folder");
        if (ToolButton(ICON_FA_FOLDER_OPEN " Projects Folder")) model.BrowseProjectsFolder();
        const auto folder = model.ProjectsFolder().u8string();
        const std::string hint = std::string(folder.begin(), folder.end()) +
                                 "\nChoose the default projects folder and scan it for projects.";
        ToolTooltip(hint.c_str());
        const float sortWidth = SortWidth * ToolUiScale();
        ContinueToolToolbar((FilterMinWidth + SortWidth) * ToolUiScale() + ImGui::GetStyle().ItemSpacing.x, true);
        ImGui::SetNextItemWidth(
            std::max(1.0f, ImGui::GetContentRegionAvail().x - sortWidth - ImGui::GetStyle().ItemSpacing.x));
        LauncherInputText("##filter", model.Filter(), 0, ICON_FA_MAGNIFYING_GLASS " Search projects...");
        ToolTooltip("Filter by project name or path");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(sortWidth);
        const char* sortLabel = model.Sort() == ProjectSort::LastOpened ? "Last opened"
                                : model.Sort() == ProjectSort::Name     ? "Name"
                                                                        : "Path";
        if (ImGui::BeginCombo("##sort", sortLabel)) {
            if (ImGui::Selectable("Last opened", model.Sort() == ProjectSort::LastOpened))
                model.SetSort(ProjectSort::LastOpened);
            if (ImGui::Selectable("Name", model.Sort() == ProjectSort::Name)) model.SetSort(ProjectSort::Name);
            if (ImGui::Selectable("Path", model.Sort() == ProjectSort::Path)) model.SetSort(ProjectSort::Path);
            ImGui::EndCombo();
        }
        ToolTooltip("Sort projects");
    }
    EndToolPanel();
}

void DrawProjectList(LauncherModel& model) {
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
    const float height = std::max(ListMinHeight * ToolUiScale(), ImGui::GetContentRegionAvail().y);
    if (BeginToolPanel("##projects-list", {0, height}, ToolSurface::Content, 0,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (ImGui::BeginTable("##project-table", 2, flags)) {
            ImGui::TableSetupColumn("Project / Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Engine / Status", ImGuiTableColumnFlags_WidthFixed,
                                    EngineColumnWidth * ToolUiScale());
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::SectionSize);
            ImGui::TableHeadersRow();
            ImGui::PopFont();
            if (model.VisibleRows().empty()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                LauncherMutedText(model.Rows().empty() ? "No projects found." : "No matching projects.");
                LauncherMutedText(model.Rows().empty() ? "Create or import a project, or choose Projects Folder."
                                                       : "Change your search to see other projects.");
                if (!model.Filter().empty() && ToolButton("Clear search")) model.Filter().clear();
            }
            for (const auto index : model.VisibleRows())
                DrawProjectRow(model, index);
            ImGui::EndTable();
        }
    }
    EndToolPanel();
}
} // namespace

void DrawProjectsTab(LauncherModel& model) {
    DrawProjectToolbar(model);
    const float scale = ToolUiScale();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float utilityWidth = UtilityWidth * scale;
    const bool sideBySide = available.x >= utilityWidth + ListMinWidth * scale + gap;
    if (BeginToolPanel("##project-workspace", {sideBySide ? available.x - utilityWidth - gap : 0, 0},
                       ToolSurface::Workspace, 0, 0, ToolMetrics::Gap)) {
        if (BeginToolPanel("##workspace-header", {0, 0}, ToolSurface::PanelHeader, ImGuiChildFlags_AutoResizeY, 0,
                           ToolMetrics::Gap)) {
            ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::SectionSize);
            if (!sideBySide) ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Projects");
            ImGui::PopFont();
            ContinueToolToolbar("000 of 000 projects");
            ImGui::PushFont(ToolFont(ToolFontRole::Ui), ToolMetrics::CaptionSize);
            if (!sideBySide) ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%zu of %zu projects", model.VisibleRows().size(), model.Rows().size());
            ImGui::PopFont();
            if (!sideBySide) {
                ContinueToolToolbar("Project details " ICON_FA_ANGLE_DOWN, true);
                if (ToolButton("Project details " ICON_FA_ANGLE_DOWN)) ImGui::OpenPopup("##utility-popup");
                const auto* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowSize(
                    {std::min(utilityWidth, viewport->WorkSize.x - ToolMetrics::Padding * scale * 2),
                     std::min(UtilityPopupHeight * scale, viewport->WorkSize.y - ToolMetrics::Padding * scale * 2)});
                if (ImGui::BeginPopup("##utility-popup")) {
                    DrawUtility(model);
                    ImGui::EndPopup();
                }
            }
        }
        EndToolPanel();
        const bool missing = std::ranges::any_of(model.Rows(), [](const ProjectRow& row) { return row.folderMissing; });
        if (missing && ToolButton("Remove missing projects")) model.RemoveMissing();
        DrawProjectList(model);
    }
    EndToolPanel();
    if (sideBySide) {
        ImGui::SameLine();
        if (BeginToolPanel("##launcher-utility", {utilityWidth, 0}, ToolSurface::Utility, 0, 0, ToolMetrics::Gap))
            DrawUtility(model);
        EndToolPanel();
    }
}
