#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float WindowPadding = ToolMetrics::Space;
constexpr float PanelPadding = 20.0f;
constexpr float PanelMargin = 18.0f;
constexpr float DefaultPanelWidth = 580.0f;
constexpr float DefaultPanelHeight = 510.0f;
constexpr float SettingsWidth = 510.0f;
constexpr float SettingsHeight = 430.0f;
constexpr float WidePanelWidth = 760.0f;
constexpr float WidePanelHeight = 580.0f;
constexpr float SystemPanelWidth = 650.0f;
constexpr float TopBarGap = ToolMetrics::SectionGap;
constexpr float StatusDotRadius = 3.0f;
constexpr float NoticePadding = 10.0f;
constexpr float NoticeAccentWidth = 3.0f;
constexpr float NoticeMaxHeight = 140.0f;
constexpr ImGuiWindowFlags PanelWindowFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
constexpr ImGuiWindowFlags FrameWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

const char* PanelTitle(LauncherPanel panel) {
    switch (panel) {
    case LauncherPanel::None        : return "";
    case LauncherPanel::NewProject  : return "Create project";
    case LauncherPanel::Settings    : return "Settings";
    case LauncherPanel::SystemCheck : return "System check";
    case LauncherPanel::EngineUpdate: return "Review engine change";
    case LauncherPanel::Job         : return "Activity";
    }
    return "";
}

const char* SystemLabel(SystemState state) {
    switch (state) {
    case SystemState::Checking: return "Checking";
    case SystemState::Ready   : return "Ready";
    case SystemState::Warning : return "Warning";
    case SystemState::NotReady: return "Not ready";
    }
    return "";
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

bool ContinueStatusBar(const char* label) {
    const float gap = ToolMetrics::SectionGap * ToolUiScale();
    const auto last = ImGui::GetItemRectMax();
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (last.x + gap + ImGui::CalcTextSize(label).x > right) return false;
    const float x = std::floor(last.x + gap * 0.5f);
    ImGui::GetWindowDrawList()->AddLine({x, ImGui::GetItemRectMin().y}, {x, last.y},
                                        ImGui::GetColorU32(ToolPalette::Border));
    ImGui::SameLine(0, gap);
    return true;
}

void DrawPanel(LauncherModel& model) {
    const LauncherPanel panel = model.Panel();
    if (panel == LauncherPanel::None) return;
    const float scale = ToolUiScale();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 size{DefaultPanelWidth * scale, DefaultPanelHeight * scale};
    if (panel == LauncherPanel::Settings) size = {SettingsWidth * scale, SettingsHeight * scale};
    if (panel == LauncherPanel::SystemCheck) size.x = SystemPanelWidth * scale;
    if (panel == LauncherPanel::Job || panel == LauncherPanel::EngineUpdate)
        size = {WidePanelWidth * scale, WidePanelHeight * scale};
    size.x = std::min(size.x, viewport->WorkSize.x - PanelMargin * scale * 2);
    size.y = std::min(size.y, viewport->WorkSize.y - PanelMargin * scale * 2);
    ImVec2 position{viewport->WorkPos.x + (viewport->WorkSize.x - size.x) * 0.5f,
                    viewport->WorkPos.y + (viewport->WorkSize.y - size.y) * 0.5f};
    if (panel == LauncherPanel::Settings)
        position = {viewport->WorkPos.x + viewport->WorkSize.x - size.x - PanelMargin * scale,
                    viewport->WorkPos.y + PanelMargin * scale};
    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {PanelPadding * scale, PanelPadding * scale});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
    if (ImGui::Begin(PanelTitle(panel), nullptr, PanelWindowFlags)) {
        const float closeWidth = ImGui::GetFrameHeight();
        ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::SectionSize);
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
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void DrawNoticeCard(const char* id, const char* title, const char* message, ToolTone tone, LauncherModel& model,
                    bool settingsError) {
    const float scale = ToolUiScale();
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {NoticePadding * scale, NoticePadding * scale});
    ImGui::SetNextWindowSizeConstraints({0, 0}, {ImGui::GetContentRegionAvail().x, NoticeMaxHeight * scale});
    if (ImGui::BeginChild("##notice", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        const auto start = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            start, {start.x + NoticeAccentWidth * scale, start.y + ImGui::GetWindowHeight()},
            ImGui::GetColorU32(ToolToneColor(tone)));
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
    ImGui::PopStyleVar();
    ImGui::PopID();
}
} // namespace

void DrawLauncher(LauncherModel& model) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {WindowPadding * ToolUiScale(), WindowPadding * ToolUiScale()});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        {ToolMetrics::Space * ToolUiScale(), ToolMetrics::Space * ToolUiScale()});
    if (ImGui::Begin("##launcher", nullptr, FrameWindowFlags)) {
        const bool covered = model.Panel() != LauncherPanel::None || model.Pending() != Confirmation::None;
        ImGui::BeginDisabled(covered);
        DrawTopBar(model);
        DrawNotice(model);
        const float statusHeight = (ToolMetrics::CaptionSize + ToolMetrics::Space * 2) * ToolUiScale() + 2;
        if (ImGui::BeginChild("##tab", {0, -statusHeight - ImGui::GetStyle().ItemSpacing.y})) {
            if (model.Tab() == LauncherTab::Projects)
                DrawProjectsTab(model);
            else
                DrawEnginesTab(model);
        }
        ImGui::EndChild();
        DrawStatusBar(model);
        ImGui::EndDisabled();
        if (model.Panel() != LauncherPanel::None)
            ImGui::GetWindowDrawList()->AddRectFilled(
                viewport->WorkPos,
                {viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y},
                ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    DrawPanel(model);
    DrawConfirmation(model);
}

void DrawTopBar(LauncherModel& model) {
    const float scale = ToolUiScale();
    if (BeginToolPanel("##application-header", {0, 0}, ToolSurface::Header, ImGuiChildFlags_AutoResizeY, 0,
                       ToolMetrics::Gap)) {
        ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::IdentitySize);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(LauncherModel::ProductName());
        ImGui::PopFont();
        ContinueToolToolbar("Projects", true);
        if (ToolSegment("Projects", model.Tab() == LauncherTab::Projects)) model.SelectTab(LauncherTab::Projects);
        ContinueToolToolbar("Engines");
        if (ToolSegment("Engines", model.Tab() == LauncherTab::Engines)) model.SelectTab(LauncherTab::Engines);
        ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
        const std::string status = std::string("    ") + SystemLabel(model.System());
        const char* settings = ICON_FA_GEAR " Settings";
        float rightWidth = ImGui::CalcTextSize(status.c_str()).x + ImGui::CalcTextSize(settings).x +
                           ImGui::GetStyle().FramePadding.x * 4 + ImGui::GetStyle().ItemSpacing.x;
        const float rightEdge = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        const bool compact =
            ImGui::GetWindowPos().x + rightEdge - rightWidth < ImGui::GetItemRectMax().x + TopBarGap * scale;
        if (compact) rightWidth = ImGui::GetFrameHeight() * 2 + ImGui::GetStyle().ItemSpacing.x;
        const float rightStart = rightEdge - rightWidth;
        if (ImGui::GetWindowPos().x + rightStart >= ImGui::GetItemRectMax().x + TopBarGap * scale)
            ImGui::SameLine(std::floor(rightStart));
        else
            ContinueToolToolbar(rightWidth, true);
        ImGui::PushStyleColor(ImGuiCol_Text, ToolPalette::Secondary);
        const bool checkPressed =
            compact ? ToolIconButton("system-check", "", SystemLabel(model.System())) : ToolButton(status.c_str());
        if (checkPressed) model.OpenPanel(LauncherPanel::SystemCheck);
        const ImVec2 start = ImGui::GetItemRectMin();
        const float dotX =
            compact ? ImGui::GetItemRectSize().x * 0.5f : ImGui::GetStyle().FramePadding.x + StatusDotRadius * scale;
        ImGui::GetWindowDrawList()->AddCircleFilled(
            {start.x + dotX, std::floor(start.y + ImGui::GetFrameHeight() * 0.5f)}, StatusDotRadius * scale,
            ImGui::GetColorU32(ToolToneColor(SystemTone(model.System()))));
        if (!compact) ToolTooltip("Check build tools and installed engines");
        ImGui::SameLine();
        const bool settingsPressed =
            compact ? ToolIconButton("settings", ICON_FA_GEAR, "Settings") : ToolButton(settings);
        if (settingsPressed) model.OpenPanel(LauncherPanel::Settings);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    EndToolPanel();
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

void DrawStatusBar(LauncherModel& model) {
    const float scale = ToolUiScale();
    const float height = (ToolMetrics::CaptionSize + ToolMetrics::Space * 2) * scale + 2;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ToolSurfaceColor(ToolSurface::Status));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ToolMetrics::Padding * scale, ToolMetrics::Space * scale});
    if (ImGui::BeginChild("##status-bar", {0, height}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::PushFont(ToolFont(ToolFontRole::Ui), ToolMetrics::CaptionSize);
        ImGui::TextDisabled("Launcher %s", LauncherModel::LauncherVersion());
        const std::string count = std::to_string(model.Rows().size()) + " projects";
        if (ContinueStatusBar(count.c_str())) ImGui::TextDisabled("%s", count.c_str());
        const char* status = model.CurrentJob() ? "Working" : SystemLabel(model.System());
        if (ContinueStatusBar(status)) ImGui::TextColored(ToolToneColor(SystemTone(model.System())), "%s", status);
        if (model.Mode() == LauncherMode::Developer) {
            if (ContinueStatusBar(ICON_FA_CODE " Developer mode"))
                ImGui::TextColored(ToolToneColor(ToolTone::Accent), ICON_FA_CODE " Developer mode");
        }
        if (model.DetailsRunning()) {
            const char* checking = "Checking project details";
            if (ContinueStatusBar(checking)) ImGui::TextDisabled("%s", checking);
        }
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}
