#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <string>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float FooterGap = 10.0f;

const char* ChangeLabel(EngineChange change) {
    switch (change) {
    case EngineChange::Update : return "Update engine";
    case EngineChange::Repair : return "Repair engine";
    case EngineChange::Replace: return "Replace engine";
    }
    return "";
}

void DrawAffectedFiles(const char* title, const std::vector<std::string>& files) {
    const std::string label = std::string(title) + " (" + std::to_string(files.size()) + ")";
    if (ToolSectionHeader(label.c_str(), !files.empty())) {
        if (files.empty()) LauncherMutedText("None");
        ImGui::PushFont(ToolFont(ToolFontRole::Technical), 0);
        ImGui::PushTextWrapPos(0);
        for (const auto& file : files)
            ImGui::BulletText("%s", file.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
}
} // namespace

void DrawEngineUpdatePanel(LauncherModel& model) {
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + FooterGap * ToolUiScale();
    const auto* plan = model.Plan();
    if (ImGui::BeginChild("##engine-change-review", {0, -footerHeight})) {
        ImGui::TextUnformatted(ChangeLabel(model.PendingChange()));
        if (const auto* project = model.ChangeProject()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(project->info.name.c_str());
            ImGui::PopTextWrapPos();
            LauncherPathText(project->entry.root);
        }
        if (model.PlanRunning()) {
            DrawLauncherActivity("Checking engine files and preparing changes...");
        } else if (!model.PlanError().empty()) {
            LauncherToneText(model.PlanError().c_str(), ToolTone::Error);
        } else if (plan) {
            ImGui::Separator();
            const std::string from = plan->from ? plan->from->ToString() : "Unknown engine";
            ImGui::Text("%s  " ICON_FA_ARROW_RIGHT "  %s", from.c_str(),
                        plan->target.manifest.version.ToString().c_str());
            if (plan->mayBreak) {
                ToolStatusBadge(ICON_FA_TRIANGLE_EXCLAMATION " May require project changes",
                                ToolToneColor(ToolTone::Warning));
                LauncherMutedText("Review breaking changes and migration steps before continuing.");
            }
            LauncherMutedText("Close the project's editor and game before applying this change.");
            if (!plan->overwritten.empty() || !plan->deleted.empty())
                LauncherToneText("The following engine file contents will be lost.", ToolTone::Warning);
            DrawAffectedFiles("Overwritten files", plan->overwritten);
            DrawAffectedFiles("Deleted files", plan->deleted);
            if (ToolSectionHeader("Release notes")) DrawLauncherNotes(plan->notes);
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (ToolButton("Back")) model.ClosePanel();
    ContinueToolToolbar(ChangeLabel(model.PendingChange()), true);
    ImGui::BeginDisabled(model.PlanRunning() || !plan || !model.PlanError().empty());
    if (ToolButton(ChangeLabel(model.PendingChange()), {}, true)) model.ConfirmEngineChange();
    ImGui::EndDisabled();
}
