#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolPixel.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float PanelPadding = 20.0f;
constexpr float PanelMargin = 18.0f;
constexpr float DefaultPanelWidth = 580.0f;
constexpr float DefaultPanelHeight = 460.0f;
constexpr float NewProjectPanelHeight = 410.0f;
constexpr float SettingsWidth = 580.0f;
constexpr float WidePanelWidth = 760.0f;
constexpr float WidePanelHeight = 580.0f;
constexpr float SystemPanelWidth = 650.0f;
constexpr float SystemPanelHeight = 510.0f;
constexpr float TopBarGap = ToolMetrics::SectionGap;
constexpr float WordmarkCell = 2.0f;
constexpr float NoticeMargin = 8.0f;
constexpr float NoticePadding = 10.0f;
constexpr float NoticeMaxHeight = 140.0f;
constexpr ImGuiWindowFlags PanelWindowFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
constexpr ImGuiWindowFlags FrameWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

const char* PanelTitle(LauncherPanel panel) {
    switch (panel) {
    case LauncherPanel::None        : return "";
    case LauncherPanel::NewProject  : return "New project";
    case LauncherPanel::Settings    : return "Settings";
    case LauncherPanel::SystemCheck : return "System check";
    case LauncherPanel::EngineUpdate: return "Review engine change";
    case LauncherPanel::Job         : return "Activity";
    case LauncherPanel::Update      : return "Launcher update";
    }
    return "";
}

// Only what needs reading: a working job, or which tools are the matter. A healthy system is just the green mark, and
// the first-run screen spells out the tools itself.
std::string SystemStateText(const LauncherModel& model) {
    if (model.CurrentJob()) return "Working";
    const SystemState system = model.System();
    if (system == SystemState::Checking) return "Checking";
    const bool firstRun = model.Rows().empty() && model.Tab() == LauncherTab::Projects;
    if (system == SystemState::Ready || firstRun || !model.Report()) return {};
    const bool missing = system == SystemState::NotReady;
    const CheckStatus wanted = missing ? CheckStatus::Missing : CheckStatus::Warning;
    std::size_t count = 0;
    std::string first;
    for (const CheckItem& item : model.Report()->items) {
        if (item.status != wanted) continue;
        if (count++ == 0) first = item.title;
    }
    const std::string text = count == 1 ? first + (missing ? " missing" : " has a warning")
                                        : std::to_string(count) + (missing ? " tools missing" : " tool warnings");
    return text + "  " ICON_FA_ANGLE_RIGHT;
}

ToolTone SystemTone(SystemState state) {
    switch (state) {
    case SystemState::Checking: return ToolTone::Information;
    case SystemState::Ready   : return ToolTone::Success;
    case SystemState::Warning : return ToolTone::Warning;
    case SystemState::NotReady: return ToolTone::Error;
    }
    return ToolTone::Information;
}

// The top bar's offer: "Update 0.3.1", the download's percentage or the restart it needs.
std::string UpdateButtonLabel(const LauncherModel& model) {
    switch (model.UpdateState()) {
    case UpdatePhase::Downloading:
        return std::string(ICON_FA_DOWNLOAD " Updating ") + std::to_string(model.UpdateProgress()) + "%";
    case UpdatePhase::Ready:
    case UpdatePhase::WaitingForCommands: return ICON_FA_ROTATE " Restart to update";
    case UpdatePhase::Idle              :
    case UpdatePhase::Checking          :
    case UpdatePhase::UpToDate          :
    case UpdatePhase::Available         :
    case UpdatePhase::Failed            : break;
    }
    return std::string(ICON_FA_CIRCLE_ARROW_UP " Update ") + model.Update()->version;
}

void DrawPanel(LauncherModel& model) {
    const LauncherPanel panel = model.Panel();
    if (panel == LauncherPanel::None) return;
    const float scale = ToolUiScale();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 size{DefaultPanelWidth * scale, DefaultPanelHeight * scale};
    if (panel == LauncherPanel::Settings) size.x = SettingsWidth * scale;
    if (panel == LauncherPanel::NewProject) size.y = NewProjectPanelHeight * scale;
    if (panel == LauncherPanel::SystemCheck || panel == LauncherPanel::Update)
        size = {SystemPanelWidth * scale, SystemPanelHeight * scale};
    if (panel == LauncherPanel::Job || panel == LauncherPanel::EngineUpdate)
        size = {WidePanelWidth * scale, WidePanelHeight * scale};
    size.x = std::min(size.x, viewport->WorkSize.x - PanelMargin * scale * 2);
    size.y = std::min(size.y, viewport->WorkSize.y - PanelMargin * scale * 2);
    // Settings is as tall as its rows, up to what fits; the other panels have a fixed frame around their own scrolling.
    const bool fitsContent = panel == LauncherPanel::Settings;
    const ImVec2 center{viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                        viewport->WorkPos.y + viewport->WorkSize.y * 0.5f};
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
    if (fitsContent)
        ImGui::SetNextWindowSizeConstraints({size.x, 0}, {size.x, viewport->WorkSize.y - PanelMargin * scale * 2});
    else
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {PanelPadding * scale, PanelPadding * scale});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    const ImGuiWindowFlags flags =
        fitsContent ? (PanelWindowFlags & ~ImGuiWindowFlags_NoScrollbar) | ImGuiWindowFlags_AlwaysAutoResize
                    : PanelWindowFlags;
    if (ImGui::Begin(PanelTitle(panel), nullptr, flags)) {
        // Opened with the mouse: no keyboard ring on the first control until the keyboard is used.
        if (ImGui::IsWindowAppearing()) ImGui::SetNavCursorVisible(false);
        const float closeWidth = ImGui::GetFrameHeight();
        ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::HeadingSize);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(PanelTitle(panel));
        ImGui::PopFont();
        if (panel != LauncherPanel::Job) {
            ImGui::SameLine(std::max(0.0f, ImGui::GetWindowWidth() - PanelPadding * scale - closeWidth));
            if (ToolIconButton("close-panel", ICON_FA_XMARK, "Close")) model.ClosePanel();
            if (model.Pending() == Confirmation::None &&
                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape))
                model.ClosePanel();
        }
        ImGui::Separator();
        switch (panel) {
        case LauncherPanel::None        : break;
        case LauncherPanel::NewProject  : DrawNewProjectPanel(model); break;
        case LauncherPanel::Settings    : DrawSettingsPanel(model); break;
        case LauncherPanel::SystemCheck : DrawSystemCheckPanel(model); break;
        case LauncherPanel::EngineUpdate: DrawEngineUpdatePanel(model); break;
        case LauncherPanel::Job         : DrawJobPanel(model); break;
        case LauncherPanel::Update      : DrawUpdatePanel(model); break;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void DrawNoticeCard(const char* id, const char* title, const char* message, ToolTone tone, LauncherModel& model,
                    bool settingsError) {
    const float scale = ToolUiScale();
    ImGui::PushID(id);
    const float margin = NoticeMargin * scale;
    ImGui::SetCursorPos({margin, ImGui::GetCursorPosY() + margin});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {NoticePadding * scale, NoticePadding * scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ToolMetrics::Gap * scale, ToolMetrics::Space * scale});
    const float width = ImGui::GetContentRegionAvail().x - margin;
    ImGui::SetNextWindowSizeConstraints({0, 0}, {width, NoticeMaxHeight * scale});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ToolPalette::Elevated);
    if (ImGui::BeginChild("##notice", {width, 0},
                          ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY)) {
        const float controlWidth =
            settingsError ? ImGui::CalcTextSize("Start with defaults").x + ImGui::GetStyle().FramePadding.x * 2
                          : ImGui::GetFrameHeight();
        if (ImGui::BeginTable("##notice-layout", 2)) {
            ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Dismiss", ImGuiTableColumnFlags_WidthFixed, controlWidth);
            ImGui::TableNextColumn();
            LauncherToneText(title, tone);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(message);
            ImGui::PopTextWrapPos();
            ImGui::TableNextColumn();
            if (settingsError) {
                if (ToolButton("Start with defaults")) model.StartWithDefaultSettings();
            } else if (ToolIconButton("dismiss", ICON_FA_XMARK, "Dismiss notification")) {
                model.DismissNotice();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

// "New engine version!" under the top bar while a version online is newer than every engine here; its button opens
// the version in the Engines tab, the cross hides it until a newer one comes.
void DrawNewEngineStrip(LauncherModel& model) {
    const EngineNote* note = model.NewEngineAnnouncement();
    if (!note) return;
    const float scale = ToolUiScale();
    const std::string message = EngineNoteTitle(*note) + " - " + note->summary;
    constexpr const char* Open = ICON_FA_BOOK_OPEN " Release notes";
    ImGui::PushID("new-engine");
    const float margin = NoticeMargin * scale;
    ImGui::SetCursorPos({margin, ImGui::GetCursorPosY() + margin});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {NoticePadding * scale, NoticePadding * scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ToolMetrics::Gap * scale, ToolMetrics::Space * scale});
    const float width = ImGui::GetContentRegionAvail().x - margin;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ToolPalette::Elevated);
    ImGui::PushStyleColor(ImGuiCol_Border, ToolToneColor(ToolTone::Accent));
    if (ImGui::BeginChild("##strip", {width, 0},
                          ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_Borders)) {
        ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
        const float openWidth = ImGui::CalcTextSize(Open).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::PopFont();
        if (ImGui::BeginTable("##strip-layout", 3)) {
            ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Open", ImGuiTableColumnFlags_WidthFixed, openWidth);
            ImGui::TableSetupColumn("Dismiss", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableNextColumn();
            LauncherToneText(ICON_FA_STAR " New engine version!", ToolTone::Accent);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(message.c_str());
            ImGui::PopTextWrapPos();
            ImGui::TableNextColumn();
            if (ToolButton(Open)) model.ShowNewEngine();
            ImGui::TableNextColumn();
            if (ToolIconButton("dismiss", ICON_FA_XMARK, "Hide until a newer engine")) model.DismissNewEngine();
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

// Text and a state mark that opens the System check; only the mark when `compact`.
void DrawSystemState(LauncherModel& model, bool compact) {
    const float scale = ToolUiScale();
    const auto system = model.System();
    const bool working = model.CurrentJob() != nullptr;
    const std::string label = SystemStateText(model);
    const float gap = ToolMetrics::Gap * scale;
    const float mark = ToolPixelCell(11.0f);
    const float frame = ImGui::GetFrameHeight();
    const float width = compact ? frame : mark + gap + ImGui::CalcTextSize(label.c_str()).x + gap;
    const auto start = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##system-check", {width, frame})) model.OpenPanel(LauncherPanel::SystemCheck);
    const bool hovered = ImGui::IsItemHovered();
    const auto end = ImGui::GetItemRectMax();
    ToolTooltip(compact && !label.empty() ? label.c_str() : "Check build tools and installed engines");
    auto* draw = ImGui::GetWindowDrawList();
    if (hovered)
        draw->AddRectFilled(start, end, ImGui::GetColorU32(ToolPalette::Elevated), ToolMetrics::Radius * scale);
    const float textHeight = ImGui::GetTextLineHeight();
    const float textY = start.y + (frame - textHeight) * 0.5f;
    ImGui::SetCursorScreenPos({start.x + (compact ? (frame - mark) * 0.5f : gap * 0.5f), textY});
    if (working || system == SystemState::Checking)
        ToolActivity();
    else
        ToolStateCell(ToolToneColor(SystemTone(system)));
    if (!compact)
        draw->AddText({start.x + gap * 0.5f + mark + gap * 0.5f, textY},
                      ImGui::GetColorU32(hovered ? ToolPalette::Text : ToolPalette::Secondary), label.c_str());
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy({width, frame});
}
} // namespace

void DrawLauncher(LauncherModel& model) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ToolPalette::Window);
    if (ImGui::Begin("##launcher", nullptr, FrameWindowFlags)) {
        const bool covered = model.Panel() != LauncherPanel::None || model.Pending() != Confirmation::None;
        ImGui::BeginDisabled(covered);
        DrawTopBar(model);
        DrawNewEngineStrip(model);
        DrawNotice(model);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
        if (ImGui::BeginChild("##tab", {0, 0})) {
            if (model.Tab() == LauncherTab::Projects)
                DrawProjectsTab(model);
            else
                DrawEnginesTab(model);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (model.Panel() != LauncherPanel::None)
            ImGui::GetWindowDrawList()->AddRectFilled(
                viewport->WorkPos,
                {viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y},
                ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    DrawPanel(model);
    DrawConfirmation(model);
}

void DrawTopBar(LauncherModel& model) {
    const float scale = ToolUiScale();
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ToolMetrics::Space * scale, ToolMetrics::Space * scale});
    if (BeginToolPanel("##application-header", {0, 0}, ToolSurface::Header, ImGuiChildFlags_AutoResizeY, 0,
                       ToolMetrics::Gap)) {
        const float frame = ImGui::GetFrameHeight();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ToolMetrics::Space * scale);
        ToolPixelText(LauncherModel::ProductName(), WordmarkCell, ToolPalette::Accent, frame);
        ContinueToolToolbar("Projects", true);
        if (ToolSegment("Projects", model.Tab() == LauncherTab::Projects)) model.SelectTab(LauncherTab::Projects);
        ContinueToolToolbar("Engines");
        if (ToolSegment("Engines", model.Tab() == LauncherTab::Engines)) model.SelectTab(LauncherTab::Engines);
        ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
        const auto& style = ImGui::GetStyle();
        const float spacing = style.ItemSpacing.x;
        const bool developer = model.Mode() == LauncherMode::Developer;
        const char* developerLabel = ICON_FA_CODE " Developer";
        const bool updateOffered = model.UpdateOffered();
        const std::string updateLabel = updateOffered ? UpdateButtonLabel(model) : std::string();
        const std::string stateLabel = SystemStateText(model);
        const float gap = ToolMetrics::Gap * scale;
        float rightWidth =
            ToolPixelCell(11.0f) + gap + ImGui::CalcTextSize(stateLabel.c_str()).x + gap + spacing + frame;
        if (updateOffered)
            rightWidth += ImGui::CalcTextSize(updateLabel.c_str()).x + style.FramePadding.x * 2 + spacing;
        if (developer) rightWidth += ImGui::CalcTextSize(developerLabel).x + spacing * 3;
        const float rightEdge = ImGui::GetWindowWidth() - style.WindowPadding.x;
        const bool compact =
            ImGui::GetWindowPos().x + rightEdge - rightWidth < ImGui::GetItemRectMax().x + TopBarGap * scale;
        if (compact) rightWidth = frame * (updateOffered ? 3 : 2) + spacing * (updateOffered ? 2 : 1);
        const float rightStart = rightEdge - rightWidth;
        if (ImGui::GetWindowPos().x + rightStart >= ImGui::GetItemRectMax().x + TopBarGap * scale)
            ImGui::SameLine(std::floor(rightStart));
        else
            ContinueToolToolbar(rightWidth, true);
        if (updateOffered) {
            const std::string updateId = updateLabel + "###launcher-update";
            ImGui::PushStyleColor(ImGuiCol_Text, ToolPalette::Accent);
            ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToolPalette::Elevated);
            const bool updatePressed =
                compact ? ToolIconButton("launcher-update", ICON_FA_CIRCLE_ARROW_UP, updateLabel.c_str())
                        : ToolButton(updateId.c_str());
            ImGui::PopStyleColor(3);
            if (updatePressed) model.OpenUpdatePanel();
            ImGui::SameLine();
        }
        if (developer && !compact) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ToolToneColor(ToolTone::Accent), "%s", developerLabel);
            ImGui::SameLine(0, spacing * 3);
        }
        ImGui::PopFont();
        DrawSystemState(model, compact);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ToolPalette::Secondary);
        const bool settingsPressed = ToolIconButton("settings", ICON_FA_GEAR, "Settings");
        ImGui::PopStyleColor();
        if (settingsPressed) model.OpenPanel(LauncherPanel::Settings);
        const auto windowStart = ImGui::GetWindowPos();
        const float lineY = windowStart.y + ImGui::GetWindowHeight() - 1;
        ImGui::GetWindowDrawList()->AddRectFilled({windowStart.x, lineY},
                                                  {windowStart.x + ImGui::GetWindowWidth(), lineY + 1},
                                                  ImGui::GetColorU32(ToolPalette::Separator));
    }
    EndToolPanel();
    ImGui::PopStyleVar(2);
}

void DrawNotice(LauncherModel& model) {
    if (const auto& notice = model.CurrentNotice()) {
        const ToolTone tone = notice->level == NoticeLevel::Error     ? ToolTone::Error
                              : notice->level == NoticeLevel::Warning ? ToolTone::Warning
                                                                      : ToolTone::Information;
        DrawNoticeCard("notice", notice->title.c_str(), notice->message.c_str(), tone, model, false);
    }
    if (!model.SettingsError().empty())
        DrawNoticeCard("settings-error", "Could not read settings", model.SettingsError().c_str(), ToolTone::Error,
                       model, true);
}
