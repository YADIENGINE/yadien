#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include "Model/LauncherModel.h"
#include "Platform/Text.h"
#include "Screens/ProjectPreview.h"
#include "Screens/ScreenLayout.h"
#include "System/DisplayText.h"
#include "Update/UpdateNotes.h"
#include "ToolUi/ToolPixel.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float UtilityWidth = 308.0f;
constexpr float ListMinWidth = 360.0f;
constexpr float ListMinHeight = 112.0f;
constexpr float FilterMinWidth = 140.0f;
constexpr float SortWidth = 138.0f;
constexpr float RowHeight = 62.0f;
constexpr float RowTile = 40.0f;
constexpr float RowInset = 12.0f;
constexpr float MetaColumnWidth = 124.0f;
constexpr float DetailTile = 52.0f;
constexpr float DetailPadding = 16.0f;
constexpr float DetailLabelWidth = 92.0f;
constexpr float UtilityPopupHeight = 600.0f;
constexpr float FirstRunPaddingX = 64.0f;
constexpr float FirstRunPaddingY = 56.0f;
constexpr float FirstRunActionWidth = 440.0f;
constexpr float FirstRunActionHeight = 66.0f;
constexpr float FirstRunMarkCell = 72.0f;
constexpr float FirstRunMarkMargin = 48.0f;
constexpr float FirstRunSetupNameWidth = 140.0f;
constexpr float PreviewGap = 12.0f;

const char* EngineSource(EngineKind kind) {
    switch (kind) {
    case EngineKind::Copy        : return "Stable copy";
    case EngineKind::Git         : return "Managed by git";
    case EngineKind::Unregistered: return "Unknown source";
    case EngineKind::Missing     : return "Engine missing";
    }
    return "";
}

// "Yesterday" as it reads inside a sentence.
std::string LowerFirst(std::string text) {
    if (!text.empty()) text[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    return text;
}

std::string LastOpenedText(const ProjectRow& row) {
    if (row.entry.lastOpened.empty()) return "Never opened";
    auto text = RelativeTimeText(row.entry.lastOpened, std::chrono::system_clock::now());
    return text.empty() ? row.entry.lastOpened : text;
}

// The tab's keys work only when nothing floats above it.
bool ProjectKeysActive(const LauncherModel& model) {
    return model.Panel() == LauncherPanel::None && model.Pending() == Confirmation::None &&
           !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

// Up / Down move the selection, Enter opens it. Owning the arrows keeps ImGui's own keyboard navigation from moving at
// the same time; a control reached with Tab still takes its Enter.
void ProjectKeys(LauncherModel& model) {
    const auto& io = ImGui::GetIO();
    if (!ProjectKeysActive(model) || io.WantTextInput) return;
    const ImGuiID owner = ImGui::GetID("##project-keys");
    ImGui::SetKeyOwner(ImGuiKey_UpArrow, owner);
    ImGui::SetKeyOwner(ImGuiKey_DownArrow, owner);
    const auto& visible = model.VisibleRows();
    if (visible.empty()) return;
    const auto selected = model.Selected();
    const auto current = selected ? std::ranges::find(visible, *selected) : visible.end();
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, ImGuiInputFlags_Repeat, owner))
        model.Select(current == visible.end() || current + 1 == visible.end() ? visible.back() : *(current + 1));
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, ImGuiInputFlags_Repeat, owner))
        model.Select(current == visible.end() || current == visible.begin() ? visible.front() : *(current - 1));
    if (!io.NavVisible && selected && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && model.ActionsFor(*selected).open)
        model.Open(*selected);
}

void DrawProjectRow(LauncherModel& model, std::size_t index) {
    const auto& row = model.Rows()[index];
    const bool selected = model.Selected() == index;
    const float scale = ToolUiScale();
    const float inset = RowInset * scale;
    ImGui::PushID(static_cast<int>(index));
    const auto start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = RowHeight * scale;
    if (ImGui::Selectable("##project", selected, ImGuiSelectableFlags_AllowDoubleClick, {0, height})) {
        model.Select(index);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && model.ActionsFor(index).open) model.Open(index);
    }
    // The arrow keys move the selection (ProjectKeys); keep it in view while they are held.
    if (selected && !ImGui::IsItemVisible() &&
        (ImGui::IsKeyDown(ImGuiKey_UpArrow) || ImGui::IsKeyDown(ImGuiKey_DownArrow)))
        ImGui::SetScrollHereY(0.5f);
    const std::string path = PathToUtf8(row.entry.root);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) model.Select(index);
    if (ImGui::BeginPopupContextItem("##project-menu")) {
        const auto actions = model.ActionsFor(index);
        if (ImGui::MenuItem("Open", "Enter", false, actions.open)) model.Open(index);
        if (ImGui::MenuItem("Run", nullptr, false, actions.run)) model.Run(index);
        if (ImGui::MenuItem("Show in folder", nullptr, false, actions.showInFolder)) model.ShowInFolder(index);
        if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(path.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Remove from list", nullptr, false, actions.remove)) model.AskRemove(index);
        ImGui::EndPopup();
    }
    const std::string update = row.update ? "\nEngine update available: " + row.update->ToString() : std::string();
    const std::string tooltip =
        row.info.name + "\n" + path + (row.info.problem.empty() ? "" : "\n" + row.info.problem) + update;
    ToolTooltip(tooltip.c_str());
    auto* draw = ImGui::GetWindowDrawList();
    const float tile = std::round(RowTile * scale);
    ToolDrawPixelTile(*draw, {start.x + inset, start.y + (height - tile) * 0.5f}, row.info.name, tile);
    // Name and path on the left; engine and what to know about it on the right.
    const float metaWidth = MetaColumnWidth * scale;
    const float textStart = start.x + inset + tile + inset;
    const float textWidth = std::max(1.0f, start.x + width - inset - metaWidth - textStart);
    const float lineGap = ToolMetrics::Space * scale;
    ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
    const float nameHeight = ImGui::GetTextLineHeight();
    const float nameY = start.y + (height - nameHeight * 2 - lineGap) * 0.5f;
    const auto name = ToolClipText(row.info.name.c_str(), textWidth);
    draw->AddText({textStart, nameY},
                  ImGui::GetColorU32(row.folderMissing ? ToolPalette::Secondary : ToolPalette::Text), name.c_str());
    ImGui::PopFont();
    const float secondY = nameY + nameHeight + lineGap;
    ImGui::PushFont(ToolFont(ToolFontRole::Technical), ToolMetrics::CaptionSize);
    draw->AddText({textStart, secondY}, ImGui::GetColorU32(ToolPalette::Muted),
                  LauncherShortPath(row.entry.root, textWidth).c_str());
    ImGui::PopFont();
    const bool issue = row.folderMissing || !row.info.problem.empty() || !row.contractSupported;
    const std::string engineVersion = row.Version();
    // An available update is a quiet arrow here: the details panel offers it, and amber on every row would not say
    // more.
    const bool updatable = row.update.has_value() && !issue;
    const std::string version = (issue       ? ICON_FA_TRIANGLE_EXCLAMATION " "
                                 : updatable ? ICON_FA_ARROW_UP " "
                                             : "") +
                                (engineVersion.empty() ? "Unavailable" : engineVersion);
    const float metaRight = start.x + width - inset;
    const auto versionWidth = ImGui::CalcTextSize(version.c_str()).x;
    draw->AddText({metaRight - versionWidth, nameY},
                  ImGui::GetColorU32(issue       ? ToolToneColor(ToolTone::Warning)
                                     : updatable ? ToolPalette::Secondary
                                                 : ImGui::GetStyleColorVec4(ImGuiCol_Text)),
                  version.c_str());
    ImGui::PushFont(ToolFont(ToolFontRole::Ui), ToolMetrics::CaptionSize);
    const std::string state = row.folderMissing ? "Folder missing" : LastOpenedText(row);
    const auto stateColor = row.folderMissing ? ToolToneColor(ToolTone::Error) : ToolPalette::Muted;
    draw->AddText({metaRight - ImGui::CalcTextSize(state.c_str()).x, secondY}, ImGui::GetColorU32(stateColor),
                  state.c_str());
    ImGui::PopFont();
    ImGui::PopID();
}

bool ActionButton(const char* label, bool enabled, bool primary = false) {
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ToolSingleLineButton(label, primary);
    ImGui::EndDisabled();
    return pressed;
}

bool IconAction(const std::string& icon, const char* id, const char* tooltip, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    const float size = ImGui::GetFrameHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, ImGui::GetStyle().FramePadding.y});
    const bool pressed = ToolButton((icon + "##" + id).c_str(), {size, size});
    ImGui::PopStyleVar();
    ToolTooltip(tooltip);
    ImGui::EndDisabled();
    return pressed;
}

void DetailRow(const char* label, float labelWidth) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ToolPalette::Muted, "%s", label);
    ImGui::SameLine(labelWidth);
}

void DrawDetailFacts(LauncherModel& model, std::size_t index) {
    const auto& row = model.Rows()[index];
    const float labelWidth = ImGui::GetCursorPosX() + DetailLabelWidth * ToolUiScale();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        {ImGui::GetStyle().ItemSpacing.x, ToolMetrics::Space * ToolUiScale()});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 2 * ToolUiScale()});
    DetailRow("Engine", labelWidth);
    ImGui::TextUnformatted(row.Version().empty() ? "Unavailable" : row.Version().c_str());
    DetailRow("Source", labelWidth);
    ImGui::TextUnformatted(EngineSource(row.info.kind));
    const auto* details = model.DetailsFor(index);
    if (details && details->integrity) {
        const auto& integrity = *details->integrity;
        DetailRow("Files", labelWidth);
        if (integrity.Clean()) {
            ImGui::TextColored(ToolToneColor(ToolTone::Success), ICON_FA_CHECK " Unchanged");
        } else {
            const std::string counts = std::to_string(integrity.modified.size()) + " modified, " +
                                       std::to_string(integrity.missing.size()) + " missing, " +
                                       std::to_string(integrity.added.size()) + " added";
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ToolToneColor(ToolTone::Warning), "%s", counts.c_str());
            ImGui::PopTextWrapPos();
        }
    } else if (!details && model.DetailsRunning()) {
        DetailRow("Files", labelWidth);
        DrawLauncherActivity("Checking");
    }
    if (details && details->lastBuild) {
        const auto& build = *details->lastBuild;
        DetailRow("Last build", labelWidth);
        const std::string when = RelativeTimeText(build.finished, std::chrono::system_clock::now());
        const std::string text = (build.ok ? ICON_FA_CHECK " Passed" : ICON_FA_XMARK " Failed") +
                                 (when.empty() ? "" : ", " + LowerFirst(when));
        ImGui::TextColored(ToolToneColor(build.ok ? ToolTone::Success : ToolTone::Error), "%s", text.c_str());
        ToolTooltip((build.finished + " UTC").c_str());
    }
    if (details && details->git) {
        const auto& git = *details->git;
        DetailRow("Branch", labelWidth);
        ImGui::TextUnformatted(git.Detached() ? "Detached HEAD" : git.branch.c_str());
        DetailRow("Changes", labelWidth);
        ImGui::Text("%d files", git.changedFiles);
        if (git.hasUpstream) {
            DetailRow("Remote", labelWidth);
            ImGui::Text("%d ahead, %d behind", git.ahead, git.behind);
        }
    }
    DetailRow("Last opened", labelWidth);
    ImGui::TextUnformatted(LastOpenedText(row).c_str());
    if (!row.entry.lastOpened.empty()) ToolTooltip((row.entry.lastOpened + " UTC").c_str());
    ImGui::PopStyleVar(2);
}

void DrawProjectActions(LauncherModel& model) {
    const auto selected = model.Selected();
    if (!selected || std::ranges::find(model.VisibleRows(), *selected) == model.VisibleRows().end()) {
        // The list already says the search found nothing; here, where the projects come from.
        LauncherFieldLabel("PROJECTS FOLDER");
        LauncherPathText(model.ProjectsFolder());
        if (ToolButton(ICON_FA_FOLDER_OPEN " Change")) model.BrowseProjectsFolder();
        ToolTooltip("Choose the default projects folder and find the projects in it");
        LauncherMutedText("Projects found in this folder join the list.");
        return;
    }
    const auto index = *selected;
    const auto& row = model.Rows()[index];
    const auto actions = model.ActionsFor(index);
    const float scale = ToolUiScale();
    if (const auto* details = model.DetailsFor(index); details && !details->lastFrame.empty()) {
        DrawProjectPreview(details->lastFrame, ImGui::GetContentRegionAvail().x);
        ImGui::Dummy({0, PreviewGap * scale - ImGui::GetStyle().ItemSpacing.y * 2});
    }
    ToolPixelTile(row.info.name, DetailTile);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::HeadingSize);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(row.info.name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    LauncherPathText(row.entry.root);
    ImGui::EndGroup();
    const float gap = ToolMetrics::Space * scale;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {gap, ImGui::GetStyle().ItemSpacing.y});
    const float openWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - (ImGui::GetFrameHeight() + gap) * 2);
    ImGui::BeginDisabled(!actions.open);
    const bool open = ToolButton(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Open", {openWidth, 0}, true);
    ImGui::EndDisabled();
    if (open) model.Open(index);
    ImGui::SameLine();
    if (IconAction(ICON_FA_PLAY, "run", "Run the project", actions.run)) model.Run(index);
    ImGui::SameLine();
    if (IconAction(ICON_FA_FOLDER_OPEN, "folder", "Show in folder", actions.showInFolder)) model.ShowInFolder(index);
    ImGui::PopStyleVar();
    if (row.folderMissing) LauncherToneText("The project folder is missing.", ToolTone::Error);
    if (!row.contractSupported) LauncherToneText("Update the launcher to use this engine.", ToolTone::Error);
    if (!row.info.problem.empty()) LauncherToneText(row.info.problem.c_str(), ToolTone::Error);
    if (const auto* details = model.DetailsFor(index); details && !details->error.empty())
        LauncherToneText(details->error.c_str(), ToolTone::Warning);
    if (row.update && actions.updateEngine) {
        const std::string label = std::string(ICON_FA_ARROW_UP " Update engine to ") + row.update->ToString();
        if (ToolSingleLineButton(label.c_str())) model.ChangeEngine(index, EngineChange::Update);
        if (row.updateMayBreak) LauncherToneText("This update has breaking changes.", ToolTone::Warning);
    }
    ImGui::Dummy({0, ToolMetrics::Space * scale});
    DrawDetailFacts(model, index);
    if (const auto* details = model.DetailsFor(index); details && details->git && details->git->pointerChanged)
        LauncherToneText("Engine pointer changed", ToolTone::Warning);
    if (const auto* details = model.DetailsFor(index); details && details->integrity && !details->integrity->Clean())
        LauncherMutedText("Repair restores the stable engine copy.");
    ImGui::Dummy({0, ToolMetrics::Space * scale});
    ImGui::Separator();
    if (ActionButton(ICON_FA_CODE_BRANCH " Change engine version", actions.changeEngine))
        model.ChangeEngine(index, EngineChange::Update);
    if (ActionButton(ICON_FA_WRENCH " Repair engine", actions.repairEngine))
        model.ChangeEngine(index, EngineChange::Repair);
    if (ActionButton(ICON_FA_ARROWS_ROTATE " Replace engine", actions.replaceEngine))
        model.ChangeEngine(index, EngineChange::Replace);
    {
        const auto* details = model.DetailsFor(index);
        const std::uintmax_t bytes = details ? details->buildBytes : 0;
        const std::string clean =
            ICON_FA_BROOM " Clean build" + (bytes ? " (" + FormatDownloadSize(bytes) + ")" : std::string());
        if (ActionButton(clean.c_str(), bytes > 0 && !model.CurrentJob())) model.AskCleanBuild(index);
        ToolTooltip("Delete the build folders to free disk space. The next build compiles everything again.");
    }
    if (model.Mode() == LauncherMode::Developer) {
        if (ActionButton(ICON_FA_CLOUD_ARROW_DOWN " Update from remote", actions.engineGit))
            model.UpdateEngineFromRemote(index);
        if (ActionButton(ICON_FA_CLOUD_ARROW_UP " Publish changes", actions.engineGit)) model.AskPublishEngine(index);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ToolToneColor(ToolTone::Error));
    if (ActionButton(ICON_FA_XMARK " Remove from list", actions.remove)) model.AskRemove(index);
    ImGui::PopStyleColor();
}

void DrawProjectToolbar(LauncherModel& model) {
    const float scale = ToolUiScale();
    if (BeginToolPanel("##project-toolbar", {0, 0}, ToolSurface::Toolbar, ImGuiChildFlags_AutoResizeY, 0,
                       ToolMetrics::Gap)) {
        const auto& style = ImGui::GetStyle();
        const char* addLabel = "Add existing";
        const char* newLabel = ICON_FA_PLUS " New project";
        ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
        const float addWidth = ImGui::CalcTextSize(addLabel).x + style.FramePadding.x * 2;
        const float newWidth = ImGui::CalcTextSize(newLabel).x + style.FramePadding.x * 2;
        ImGui::PopFont();
        const float sortWidth = SortWidth * scale;
        const float tools = sortWidth + addWidth + newWidth + style.ItemSpacing.x * 3;
        const float available = ImGui::GetContentRegionAvail().x;
        const bool oneRow = available - tools >= FilterMinWidth * scale;
        ImGui::SetNextItemWidth(oneRow ? available - tools : -1);
        // A recess reads as one only on a lighter surface; the bar is the darkest.
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ToolPalette::Panel);
        const bool keys = ProjectKeysActive(model);
        if (keys && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) ImGui::SetKeyboardFocusHere();
        if (keys && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) model.Create();
        LauncherInputText("##filter", model.Filter(), 0, ICON_FA_MAGNIFYING_GLASS " Search projects");
        ImGui::PopStyleColor();
        ToolTooltip("Filter by project name or path (Ctrl+F)");
        if (oneRow) ImGui::SameLine();
        ImGui::SetNextItemWidth(sortWidth);
        const char* sortLabel = model.Sort() == ProjectSort::LastOpened ? "Last opened"
                                : model.Sort() == ProjectSort::Name     ? "Name"
                                                                        : "Path";
        ImGui::PushStyleColor(ImGuiCol_FrameBg, {0, 0, 0, 0});
        if (ToolBeginCombo("##sort", sortLabel)) {
            if (ImGui::Selectable("Last opened", model.Sort() == ProjectSort::LastOpened))
                model.SetSort(ProjectSort::LastOpened);
            if (ImGui::Selectable("Name", model.Sort() == ProjectSort::Name)) model.SetSort(ProjectSort::Name);
            if (ImGui::Selectable("Path", model.Sort() == ProjectSort::Path)) model.SetSort(ProjectSort::Path);
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor();
        ToolTooltip("Sort projects");
        ImGui::SameLine();
        if (ToolButton(addLabel)) model.Import();
        ToolTooltip("Add a project that already exists on this computer");
        ImGui::SameLine();
        if (ToolButton(newLabel)) model.Create();
        ToolTooltip("Create a new project (Ctrl+N)");
    }
    EndToolPanel();
}

void DrawProjectList(LauncherModel& model) {
    ProjectKeys(model);
    const float height = std::max(ListMinHeight * ToolUiScale(), ImGui::GetContentRegionAvail().y);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, ToolMetrics::Space * ToolUiScale()});
    if (ImGui::BeginChild("##projects-list", {0, height})) {
        if (model.VisibleRows().empty()) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                {ToolMetrics::Gap * ToolUiScale(), ToolMetrics::Gap * ToolUiScale()});
            ToolEmptyState("No matching projects", "Change your search to see other projects.");
            if (ToolButton("Clear search")) model.Filter().clear();
            ImGui::PopStyleVar();
        }
        for (const auto index : model.VisibleRows())
            DrawProjectRow(model, index);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// A large button: an icon, what it does, and one line about how.
bool FirstRunAction(const char* id, const char* icon, const char* title, const char* detail, bool primary,
                    float width) {
    const float scale = ToolUiScale();
    const ImVec2 size{width, FirstRunActionHeight * scale};
    const auto start = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const auto fill = primary ? (held      ? ToolPalette::AccentPressed
                                 : hovered ? ToolPalette::AccentHover
                                           : ToolPalette::Accent)
                              : (held      ? ToolPalette::Pressed
                                 : hovered ? ToolPalette::Hover
                                           : ToolPalette::Control);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, {start.x + size.x, start.y + size.y}, ImGui::GetColorU32(fill),
                        ToolMetrics::Radius * scale);
    const float inset = ToolMetrics::SectionGap * scale;
    const auto textColor = ImGui::GetColorU32(primary ? ToolPalette::AccentText : ToolPalette::Text);
    const auto detailColor = ImGui::GetColorU32(
        primary ? ImVec4{ToolPalette::AccentText.x, ToolPalette::AccentText.y, ToolPalette::AccentText.z, 0.72f}
                : ToolPalette::Secondary);
    const float iconWidth = ToolMetrics::BodySize * 1.4f * scale;
    ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::BodySize);
    const float lineHeight = ImGui::GetTextLineHeight();
    const float top = start.y + (size.y - lineHeight * 2 - ToolMetrics::Space * scale) * 0.5f;
    const float iconSize = ToolMetrics::HeadingSize * scale;
    const auto iconExtent = ImGui::GetFont()->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
    draw->AddText(ImGui::GetFont(), iconSize,
                  {start.x + inset + (iconWidth - iconExtent.x) * 0.5f, start.y + (size.y - iconExtent.y) * 0.5f},
                  textColor, icon);
    const float textX = start.x + inset + iconWidth + ToolMetrics::Gap * scale;
    draw->AddText({textX, top}, textColor, title);
    ImGui::PopFont();
    ImGui::PushFont(ToolFont(ToolFontRole::Ui), ToolMetrics::CaptionSize + 1);
    const auto detailText = ToolClipText(detail, std::max(1.0f, start.x + size.x - inset - textX));
    draw->AddText({textX, top + lineHeight + ToolMetrics::Space * scale}, detailColor, detailText.c_str());
    ImGui::PopFont();
    return pressed;
}

// One line of the setup list: its state, name and detail, and what to do about it. Returns the button's press.
bool SetupRow(const char* name, const std::string& detail, std::optional<ToolTone> tone, const char* button,
              float width) {
    const float scale = ToolUiScale();
    const auto start = ImGui::GetCursorPos();
    const float height = ImGui::GetFrameHeight();
    ImGui::Dummy({width, height});
    const auto next = ImGui::GetCursorPos();
    const float textY = start.y + ImGui::GetStyle().FramePadding.y;
    const float buttonWidth = button ? ImGui::CalcTextSize(button).x + ImGui::GetStyle().FramePadding.x * 2 : 0.0f;
    const float detailStart = FirstRunSetupNameWidth * scale;
    ImGui::SetCursorPos({start.x, textY});
    if (tone)
        ToolStateCell(ToolToneColor(*tone));
    else
        ToolActivity();
    ImGui::SetCursorPos({start.x + ToolMetrics::Gap * 2 * scale, textY});
    ImGui::TextUnformatted(name);
    ImGui::SetCursorPos({start.x + detailStart, textY});
    const auto shown =
        ToolClipMiddle(detail.c_str(), std::max(1.0f, width - detailStart - buttonWidth - ToolMetrics::Gap * scale));
    ImGui::TextColored(ToolPalette::Secondary, "%s", shown.c_str());
    if (shown != detail) ToolTooltip(detail.c_str());
    bool pressed = false;
    if (button) {
        ImGui::SetCursorPos({start.x + width - buttonWidth, start.y});
        pressed = ToolButton(button);
    }
    ImGui::SetCursorPos(next);
    ImGui::Dummy({0, 0}); // ImGui wants an item after moving the cursor
    return pressed;
}

// What a first project needs, each with its way forward.
void DrawFirstRunSetup(LauncherModel& model, float width) {
    const auto system = model.System();
    std::string tools = "Ready";
    std::optional<ToolTone> toolsTone = ToolTone::Success;
    const char* toolsButton = nullptr;
    bool canInstall = false;
    if (system == SystemState::Checking) {
        tools = "Checking";
        toolsTone.reset();
    } else if (system != SystemState::Ready && model.Report()) {
        const bool missing = system == SystemState::NotReady;
        const CheckStatus wanted = missing ? CheckStatus::Missing : CheckStatus::Warning;
        tools.clear();
        for (const CheckItem& item : model.Report()->items) {
            if (item.status != wanted) continue;
            tools += (tools.empty() ? "" : ", ") + item.title;
            canInstall = canInstall || item.fix == CheckFix::InstallMsvc;
        }
        tools += missing ? " missing" : " warning";
        toolsTone = missing ? ToolTone::Error : ToolTone::Warning;
        toolsButton = canInstall ? ICON_FA_DOWNLOAD " Install" : "Review";
    }
    if (SetupRow("Build tools", tools, toolsTone, toolsButton, width)) {
        if (canInstall)
            model.InstallMsvc();
        else
            model.OpenPanel(LauncherPanel::SystemCheck);
    }
    const auto& engines = model.Engines().Engines();
    const bool hasEngine = !engines.empty();
    const std::string engine =
        hasEngine ? engines.front().manifest.version.ToString() + " installed" : "None installed";
    if (SetupRow("Engine", engine, hasEngine ? ToolTone::Success : ToolTone::Error, hasEngine ? nullptr : "Engines",
                 width))
        model.SelectTab(LauncherTab::Engines);
    if (SetupRow("Projects folder", LauncherShortPath(model.ProjectsFolder(), width), ToolTone::Success,
                 ICON_FA_FOLDER_OPEN " Change", width))
        model.BrowseProjectsFolder();
    ToolTooltip("Choose the default projects folder and find the projects in it");
}

// No projects yet: the screen is about starting one, not an empty list.
void DrawFirstRun(LauncherModel& model) {
    const float scale = ToolUiScale();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto available = ImGui::GetContentRegionAvail();
    const float paddingX = std::min(FirstRunPaddingX * scale, available.x * 0.08f);
    const float paddingY = std::min(FirstRunPaddingY * scale, available.y * 0.08f);
    const float width = std::min(FirstRunActionWidth * scale, std::max(1.0f, available.x - paddingX * 2));
    // The logo's letter, as large as the room allows, in the quietest tone there is. It leaves when the room is gone.
    const float cell = ToolPixelCell(FirstRunMarkCell);
    const auto mark = ToolPixelTextSize("Y", cell);
    if (available.x >= paddingX * 2 + width + mark.x + FirstRunMarkMargin * scale && available.y >= mark.y)
        ToolDrawPixelText(
            *ImGui::GetWindowDrawList(),
            {origin.x + available.x - mark.x - FirstRunMarkMargin * scale, origin.y + (available.y - mark.y) * 0.5f},
            "Y", cell, ImGui::GetColorU32(ToolPalette::Panel));
    ImGui::SetCursorScreenPos({origin.x + paddingX, origin.y + paddingY});
    ImGui::BeginGroup();
    ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::TitleSize);
    ImGui::TextUnformatted("Start a project");
    ImGui::PopFont();
    ImGui::Dummy({0, ToolMetrics::Space * scale});
    if (FirstRunAction("##first-new", ICON_FA_PLUS, "New project", "Creates a folder for it, ready to run", true,
                       width))
        model.Create();
    if (FirstRunAction("##first-add", ICON_FA_FOLDER_OPEN, "Add existing project", "Pick a folder that already has one",
                       false, width))
        model.Import();
    ImGui::Dummy({0, ToolMetrics::Gap * scale});
    DrawFirstRunSetup(model, width);
    ImGui::EndGroup();
}
} // namespace

void DrawProjectsTab(LauncherModel& model) {
    const float scale = ToolUiScale();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ToolMetrics::Gap * scale, ToolMetrics::Gap * scale});
    if (model.Rows().empty()) {
        if (BeginToolPanel("##project-workspace", {0, 0}, ToolSurface::Workspace, 0, 0, 0)) DrawFirstRun(model);
        EndToolPanel();
        ImGui::PopStyleVar();
        return;
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float utilityWidth = UtilityWidth * scale;
    const bool sideBySide = available.x >= utilityWidth + ListMinWidth * scale;
    if (BeginToolPanel("##project-workspace", {sideBySide ? available.x - utilityWidth : 0, 0}, ToolSurface::Workspace,
                       0, 0, 0)) {
        DrawProjectToolbar(model);
        const bool missing = std::ranges::any_of(model.Rows(), [](const ProjectRow& row) { return row.folderMissing; });
        if (missing || !sideBySide) {
            ImGui::SetCursorPosX(ToolMetrics::Gap * scale);
            bool sameLine = false;
            if (!sideBySide) {
                if (ToolButton("Project details " ICON_FA_ANGLE_DOWN)) ImGui::OpenPopup("##utility-popup");
                const auto* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowSize(
                    {std::min(utilityWidth, viewport->WorkSize.x - ToolMetrics::Padding * scale * 2),
                     std::min(UtilityPopupHeight * scale, viewport->WorkSize.y - ToolMetrics::Padding * scale * 2)});
                if (ImGui::BeginPopup("##utility-popup")) {
                    DrawProjectActions(model);
                    ImGui::EndPopup();
                }
                sameLine = true;
            }
            if (missing) {
                if (sameLine) ContinueToolToolbar("Remove missing projects");
                if (ToolButton("Remove missing projects")) model.RemoveMissing();
            }
        }
        ImGui::SetCursorPosX(ToolMetrics::Gap * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
        if (ImGui::BeginChild("##list-area",
                              {std::max(1.0f, ImGui::GetContentRegionAvail().x - ToolMetrics::Gap * scale), 0}))
            DrawProjectList(model);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    EndToolPanel();
    if (sideBySide) {
        ImGui::SameLine(0, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
        if (BeginToolPanel("##launcher-utility", {utilityWidth, 0}, ToolSurface::Utility, 0, 0, DetailPadding))
            DrawProjectActions(model);
        EndToolPanel();
        ImGui::PopStyleVar();
    }
    ImGui::PopStyleVar();
}
