#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <array>
#include <string>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
std::string VersionLabel(const LauncherModel& model, const EngineVersion& version) {
    return version.ToString() + (model.VersionDownloaded(version) ? "" : " (download)");
}

void DrawAffectedFiles(const char* title, const std::vector<std::string>& files) {
    if (files.empty()) return;
    const std::string label = std::string(title) + " (" + std::to_string(files.size()) + ")";
    if (ToolSectionHeader(label.c_str())) {
        ImGui::PushFont(ToolFont(ToolFontRole::Technical), 0);
        ImGui::PushTextWrapPos(0);
        for (const auto& file : files)
            ImGui::BulletText("%s", file.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
}

void DrawProjectHeader(const LauncherModel& model) {
    if (const auto* project = model.ChangeProject()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(project->info.name.c_str());
        ImGui::PopTextWrapPos();
        LauncherPathText(project->entry.root);
    }
}

void DrawVersionChoice(LauncherModel& model) {
    if (model.PendingChange() == EngineChange::Repair) return;
    ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
    LauncherFieldLabel("VERSION");
    const std::string current = VersionLabel(model, model.ChangeVersion());
    ImGui::SetNextItemWidth(-1);
    if (ToolBeginCombo("##engine-version", current.c_str())) {
        for (const EngineVersion& version : model.ChangeVersions()) {
            const std::string label = VersionLabel(model, version);
            const bool selected = version == model.ChangeVersion();
            if (ImGui::Selectable(label.c_str(), selected)) model.ChooseChangeVersion(version);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void DrawReview(LauncherModel& model) {
    const auto* plan = model.Plan();
    if (ImGui::BeginChild("##engine-change-review", {0, -LauncherFooterHeight()})) {
        DrawProjectHeader(model);
        DrawVersionChoice(model);
        ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
        if (model.PlanRunning()) {
            const std::string step = model.PlanStep();
            DrawLauncherActivity(step.empty() ? "Preparing changes..." : step.c_str());
        } else if (!model.PlanError().empty()) {
            LauncherToneText(model.PlanError().c_str(), ToolTone::Error);
        } else if (plan) {
            const std::string from = plan->from ? plan->from->ToString() : "Unknown engine";
            ImGui::Text("%s  " ICON_FA_ARROW_RIGHT "  %s", from.c_str(),
                        plan->target.manifest.version.ToString().c_str());
            if (!model.SwitchError().empty()) LauncherToneText(model.SwitchError().c_str(), ToolTone::Error);
            if (plan->wentBack)
                LauncherToneText("This is an older engine. Saves and settings written by the newer one may not load.",
                                 ToolTone::Warning);
            else if (plan->mayBreak)
                ToolStatusBadge(ICON_FA_TRIANGLE_EXCLAMATION " May require project changes",
                                ToolToneColor(ToolTone::Warning));
            if (model.PendingChange() == EngineChange::Repair)
                LauncherMutedText("Close the project's editor and game before applying this change.");
            else
                LauncherMutedText("A temporary copy of the project is built with this engine first: game, editor and "
                                  "tests. Your project changes only if they pass.");
            if (!plan->overwritten.empty() || !plan->deleted.empty())
                LauncherToneText("The following engine file contents will be lost.", ToolTone::Warning);
            else
                LauncherMutedText("No changes of yours in the engine files will be lost.");
            DrawAffectedFiles("Overwritten files", plan->overwritten);
            DrawAffectedFiles("Deleted files", plan->deleted);
            if (ToolSectionHeader(plan->wentBack ? "What this version does not have" : "Release notes"))
                DrawLauncherNotes(plan->notes);
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    const bool ready = !model.PlanRunning() && plan && model.PlanError().empty();
    const char* confirm = model.PendingChange() == EngineChange::Repair ? "Repair engine" : "Test and change";
    const std::array buttons{LauncherFooterButton{"Back"}, LauncherFooterButton{confirm, true, ready}};
    const int pressed = DrawLauncherFooter(buttons);
    if (pressed == 0) model.ClosePanel();
    if (pressed == 1) model.ConfirmEngineChange();
}

void DrawTrialErrors(const EngineTrial& trial) {
    if (trial.errors.empty()) return;
    ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
    LauncherFieldLabel("ERRORS");
    ImGui::PushFont(ToolFont(ToolFontRole::Technical), 0);
    ImGui::PushTextWrapPos(0);
    for (const std::string& line : trial.errors)
        ImGui::TextUnformatted(line.empty() ? " " : line.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
}

// What the trial said. The project is untouched here unless `Passed` and moving it is what failed.
void DrawTrialResult(LauncherModel& model, const EngineTrial& trial) {
    const auto* plan = model.Plan();
    if (!plan) return;
    const std::string version = plan->target.manifest.version.ToString();
    const std::string current = plan->from ? plan->from->ToString() : "the current engine";
    const bool moving = trial.verdict == TrialVerdict::Passed;
    if (ImGui::BeginChild("##engine-trial-result", {0, -LauncherFooterHeight()})) {
        DrawProjectHeader(model);
        ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
        ImGui::PushTextWrapPos(0);
        switch (trial.verdict) {
        case TrialVerdict::Passed:
            LauncherToneText((ICON_FA_CHECK " The project works with engine " + version).c_str(), ToolTone::Success);
            LauncherToneText(model.SwitchError().c_str(), ToolTone::Error);
            break;
        case TrialVerdict::Failed:
            LauncherToneText(
                (ICON_FA_TRIANGLE_EXCLAMATION " " + trial.check + " failed with engine " + version).c_str(),
                ToolTone::Error);
            LauncherMutedText(("Your project was not changed and stays on " + current +
                               ". To move anyway and fix it there, the steps below help; an AI can use the report.")
                                  .c_str());
            break;
        case TrialVerdict::NotTested:
            LauncherToneText((ICON_FA_TRIANGLE_EXCLAMATION " Engine " + version + " could not be tested").c_str(),
                             ToolTone::Warning);
            LauncherMutedText((trial.reason + ". This says nothing about the project, which was not changed.").c_str());
            break;
        }
        ImGui::PopTextWrapPos();
        DrawTrialErrors(trial);
        if (!moving && ToolSectionHeader("Release notes and migration steps")) DrawLauncherNotes(plan->notes);
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (moving) {
        const std::array buttons{LauncherFooterButton{"Back"}, LauncherFooterButton{ICON_FA_ROTATE " Try again", true}};
        const int pressed = DrawLauncherFooter(buttons);
        if (pressed == 0) model.ClosePanel();
        if (pressed == 1) model.SwitchEngine();
        return;
    }
    // At most one amber button (D23), and it is the safe way out, not moving a project that did not pass.
    const std::string keep = "Stay on " + current;
    const std::array buttons{
        LauncherFooterButton{ICON_FA_COPY " Copy report"}, LauncherFooterButton{"Change anyway"},
        LauncherFooterButton{trial.verdict == TrialVerdict::Failed ? keep.c_str() : "Try again", true}};
    const int pressed = DrawLauncherFooter(buttons);
    if (pressed == 0) ImGui::SetClipboardText(model.TrialReport().c_str());
    if (pressed == 1) model.SwitchEngine();
    if (pressed == 2) {
        if (trial.verdict == TrialVerdict::Failed)
            model.ClosePanel();
        else
            model.ConfirmEngineChange();
    }
}
} // namespace

void DrawEngineUpdatePanel(LauncherModel& model) {
    if (const std::optional<EngineTrial>& trial = model.Trial(); trial && model.Plan())
        DrawTrialResult(model, *trial);
    else
        DrawReview(model);
}
