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
constexpr float FooterGap = 10.0f;
constexpr float MinOutputHeight = 90.0f;
constexpr float ProgressHeight = 3.0f;
constexpr float OutputPadding = 10.0f;

void CopyOutput(const std::vector<std::string>& lines) {
    std::string text;
    for (const auto& line : lines) {
        text += line;
        text += '\n';
    }
    ImGui::SetClipboardText(text.c_str());
}
} // namespace

void DrawJobPanel(LauncherModel& model) {
    const BackgroundJob* job = model.CurrentJob();
    if (!job) return;
    const JobState state = job->State();
    const bool running = state == JobState::Running;
    const auto& output = model.JobOutput();
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + FooterGap * ToolUiScale();
    ImGui::PushID(job);
    if (ImGui::BeginChild("##job-body", {0, -footerHeight})) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(state == JobState::Failed ? model.JobFailureTitle().c_str() : job->Title().c_str());
        ImGui::PopTextWrapPos();
        const int step = job->StepIndex();
        const int count = job->StepCount();
        const std::string stepText = job->StepText() + " (" + std::to_string(step) + "/" + std::to_string(count) + ")";
        if (running)
            DrawLauncherActivity(stepText.c_str());
        else {
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(stepText.c_str());
            ImGui::PopTextWrapPos();
        }
        if (count > 0) {
            const float progress = std::clamp(static_cast<float>(step) / static_cast<float>(count), 0.0f, 1.0f);
            ImGui::ProgressBar(progress, {-1, ProgressHeight * ToolUiScale()}, "");
        }
        if (state == JobState::Failed)
            LauncherToneText(job->Error().c_str(), ToolTone::Error);
        else if (state == JobState::Canceled)
            LauncherToneText("Operation canceled", ToolTone::Warning);
        else if (job->CancelRequested())
            LauncherToneText("Canceling...", ToolTone::Warning);
        ImGui::Separator();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("OUTPUT / %zu lines", output.size());
        ContinueToolToolbar(ICON_FA_COPY " Copy output", true);
        ImGui::BeginDisabled(output.empty());
        if (ImGui::SmallButton(ICON_FA_COPY " Copy output")) CopyOutput(output);
        ImGui::EndDisabled();
        ContinueToolToolbar(ICON_FA_ARROW_DOWN " Latest");
        const bool jumpToEnd = ImGui::SmallButton(ICON_FA_ARROW_DOWN " Latest");
        ToolTooltip("Scroll to the latest output and follow new lines");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            {OutputPadding * ToolUiScale(), OutputPadding * ToolUiScale()});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        const float outputHeight = std::max(MinOutputHeight * ToolUiScale(), ImGui::GetContentRegionAvail().y);
        if (ImGui::BeginChild("##output", {0, outputHeight}, ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            const bool following = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
            ImGui::PushFont(ToolFont(ToolFontRole::Technical), 0);
            if (output.empty()) LauncherMutedText("Waiting for output...");
            for (std::size_t index = 0; index < output.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                ImGui::TextUnformatted(output[index].empty() ? " " : output[index].c_str());
                if (ImGui::BeginPopupContextItem("##copy-line")) {
                    if (ImGui::MenuItem("Copy line")) ImGui::SetClipboardText(output[index].c_str());
                    if (ImGui::MenuItem("Copy all output")) CopyOutput(output);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::PopFont();
            if (following || jumpToEnd) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (running) {
        ImGui::BeginDisabled(job->CancelRequested());
        if (ToolButton("Cancel")) model.CancelJob();
        ImGui::EndDisabled();
    } else if (ToolButton(ICON_FA_ARROW_LEFT " Back")) {
        model.DismissJob();
    }
    ImGui::PopID();
}
