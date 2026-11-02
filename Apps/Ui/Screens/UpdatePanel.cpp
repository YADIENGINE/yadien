#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <span>
#include <string>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float HeroIconGap = 12.0f;
constexpr float NotesPadding = 12.0f;
constexpr float NoteGroupGap = 8.0f;
constexpr float BulletGap = 8.0f;
constexpr float ProgressHeight = 6.0f;
constexpr float ProgressRounding = 3.0f;
constexpr float SectionGap = 10.0f;
constexpr double PercentScale = 100.0;

// The state at the top of the panel: a toned icon, one strong line and one quiet line.
void DrawHero(const char* icon, ToolTone tone, const std::string& title, const std::string& detail) {
    if (BeginToolPanel("##update-hero", {0, 0}, ToolSurface::Content, ImGuiChildFlags_AutoResizeY)) {
        ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::HeadingSize);
        ImGui::PushStyleColor(ImGuiCol_Text, ToolToneColor(tone));
        ImGui::TextUnformatted(icon);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, HeroIconGap * ToolUiScale());
        ImGui::BeginGroup();
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopFont();
        LauncherMutedText(detail.c_str());
        ImGui::EndGroup();
    }
    EndToolPanel();
}

void DrawNotes(const std::vector<UpdateNote>& notes) {
    ImGui::Dummy({0, SectionGap * ToolUiScale() * 0.5f});
    ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::CaptionSize);
    ImGui::TextDisabled("WHAT'S NEW");
    ImGui::PopFont();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {NotesPadding * ToolUiScale(), NotesPadding * ToolUiScale()});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    if (ImGui::BeginChild("##update-notes", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding)) {
        if (notes.empty()) LauncherMutedText("No release notes provided.");
        ImGui::PushTextWrapPos(0);
        bool first = true;
        for (const UpdateNote& note : notes) {
            if (!first && (note.kind == UpdateNoteKind::Heading || note.kind == UpdateNoteKind::Paragraph))
                ImGui::Dummy({0, NoteGroupGap * ToolUiScale()});
            switch (note.kind) {
            case UpdateNoteKind::Heading:
                ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
                ImGui::TextUnformatted(note.text.c_str());
                ImGui::PopFont();
                break;
            case UpdateNoteKind::Paragraph: LauncherMutedText(note.text.c_str()); break;
            case UpdateNoteKind::Item: // BulletText does not wrap
                ImGui::Bullet();
                ImGui::SameLine(0, BulletGap * ToolUiScale());
                ImGui::TextUnformatted(note.text.c_str());
                break;
            case UpdateNoteKind::Breaking:
                ToolStatusBadge(ICON_FA_TRIANGLE_EXCLAMATION " Breaking change", ToolToneColor(ToolTone::Warning));
                ImGui::TextUnformatted(note.text.c_str());
                break;
            }
            first = false;
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void DrawProgress(int percent, std::uint64_t totalBytes) {
    const float scale = ToolUiScale();
    ImGui::Dummy({0, SectionGap * scale * 0.5f});
    const float fraction = std::clamp(static_cast<float>(percent) / 100.0f, 0.0f, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ProgressRounding * scale);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ToolToneColor(ToolTone::Accent));
    ImGui::ProgressBar(fraction, {-1, ProgressHeight * scale}, "");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    const auto done = static_cast<std::uint64_t>(static_cast<double>(totalBytes) * percent / PercentScale);
    const std::string text =
        std::to_string(percent) + "%  /  " + FormatDownloadSize(done) + " of " + FormatDownloadSize(totalBytes);
    LauncherMutedText(text.c_str());
}

using FooterButton = LauncherFooterButton;

int DrawFooterButtons(std::span<const FooterButton> buttons, bool afterItem = false) {
    return DrawLauncherFooter(buttons, afterItem);
}

void BeginBody() { (void)ImGui::BeginChild("##update-body", {0, -LauncherFooterHeight()}); }

void EndBody() {
    ImGui::EndChild();
    ImGui::Separator();
}

void DrawChecking() {
    BeginBody();
    DrawLauncherActivity("Checking for updates...");
    EndBody();
}

void DrawUpToDate(LauncherModel& model) {
    BeginBody();
    DrawHero(ICON_FA_CIRCLE_CHECK, ToolTone::Success, "You're up to date",
             std::string("Launcher ") + LauncherModel::LauncherVersion() + " is the newest version.");
    EndBody();
    if (ToolButton("Done", {}, true)) model.ClosePanel();
}

void DrawAvailable(LauncherModel& model, const AvailableUpdate& update) {
    BeginBody();
    DrawHero(ICON_FA_CIRCLE_ARROW_UP, ToolTone::Accent, "Version " + update.version + " is available",
             std::string("You have ") + LauncherModel::LauncherVersion() + "  /  " +
                 FormatDownloadSize(update.downloadBytes) + " download");
    DrawNotes(model.UpdateNotes());
    EndBody();
    if (ToolButton("Skip this version")) model.SkipUpdate();
    ToolTooltip("Hide this version until a newer one comes out");
    constexpr std::array Buttons{FooterButton{"Later"}, FooterButton{ICON_FA_DOWNLOAD " Update", true}};
    const int pressed = DrawFooterButtons(Buttons, true);
    if (pressed == 0) model.ClosePanel();
    if (pressed == 1) model.DownloadUpdate();
}

void DrawDownloading(LauncherModel& model, const AvailableUpdate& update) {
    BeginBody();
    DrawHero(ICON_FA_DOWNLOAD, ToolTone::Accent, "Downloading version " + update.version,
             "You can keep working; nothing changes until the launcher restarts.");
    DrawProgress(model.UpdateProgress(), update.downloadBytes);
    DrawNotes(model.UpdateNotes());
    EndBody();
    constexpr std::array Buttons{FooterButton{"Cancel"}, FooterButton{"Hide"}};
    const int pressed = DrawFooterButtons(Buttons);
    if (pressed == 0) model.CancelUpdateDownload();
    if (pressed == 1) model.ClosePanel();
}

void DrawReady(LauncherModel& model, const AvailableUpdate& update) {
    BeginBody();
    DrawHero(ICON_FA_CIRCLE_CHECK, ToolTone::Success, "Version " + update.version + " is ready to install",
             "The launcher closes and opens again. Your projects and settings are not touched.");
    DrawNotes(model.UpdateNotes());
    EndBody();
    constexpr std::array Buttons{FooterButton{"Later"}, FooterButton{ICON_FA_ROTATE " Restart now", true}};
    const int pressed = DrawFooterButtons(Buttons);
    if (pressed == 0) model.ClosePanel();
    if (pressed == 1) model.RestartToUpdate();
}

void DrawWaiting(LauncherModel& model, const AvailableUpdate& update) {
    BeginBody();
    DrawHero(ICON_FA_CLOCK, ToolTone::Warning, "Waiting for running commands",
             "Installing would close them, so version " + update.version + " waits until they finish.");
    ImGui::Dummy({0, SectionGap * ToolUiScale() * 0.5f});
    DrawLauncherActivity("The launcher restarts as soon as nothing is running.");
    EndBody();
    constexpr std::array Buttons{FooterButton{"Cancel"}};
    if (DrawFooterButtons(Buttons) == 0) model.CancelUpdateRestart();
}

void DrawFailed(LauncherModel& model) {
    const std::optional<AvailableUpdate>& update = model.Update();
    BeginBody();
    DrawHero(ICON_FA_TRIANGLE_EXCLAMATION, ToolTone::Error,
             update ? "The update did not finish" : "Could not check for updates", model.UpdateError());
    if (update) DrawNotes(model.UpdateNotes());
    EndBody();
    constexpr std::array Buttons{FooterButton{"Close"}, FooterButton{ICON_FA_ROTATE " Try again", true}};
    const int pressed = DrawFooterButtons(Buttons);
    if (pressed == 0) model.ClosePanel();
    if (pressed == 1) {
        if (update)
            model.DownloadUpdate();
        else
            model.CheckForUpdates();
    }
}
} // namespace

void DrawUpdatePanel(LauncherModel& model) {
    const std::optional<AvailableUpdate>& update = model.Update();
    switch (model.UpdateState()) {
    case UpdatePhase::Idle:
    case UpdatePhase::Checking: DrawChecking(); break;
    case UpdatePhase::UpToDate: DrawUpToDate(model); break;
    case UpdatePhase::Failed  : DrawFailed(model); break;
    case UpdatePhase::Available:
        if (update) DrawAvailable(model, *update);
        break;
    case UpdatePhase::Downloading:
        if (update) DrawDownloading(model, *update);
        break;
    case UpdatePhase::Ready:
        if (update) DrawReady(model, *update);
        break;
    case UpdatePhase::WaitingForCommands:
        if (update) DrawWaiting(model, *update);
        break;
    }
}
