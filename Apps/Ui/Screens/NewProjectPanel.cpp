#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <algorithm>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float FooterGap = 10.0f;
constexpr float PrimaryWidth = 152.0f;
constexpr float MinContentWidth = 1.0f;
} // namespace

void DrawNewProjectPanel(LauncherModel& model) {
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + FooterGap * ToolUiScale();
    auto& form = model.NewProject();
    const auto engines = model.NewProjectEngines();
    if (ImGui::BeginChild("##new-project-form", {0, -footerHeight})) {
        LauncherMutedText("Choose a name, a parent folder, and an engine.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Project name");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        LauncherInputText("##name", form.name);
        ImGui::TextUnformatted("Location");
        const char* browse = ICON_FA_FOLDER_OPEN " Browse";
        const float browseWidth = ImGui::CalcTextSize(browse).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(
            std::max(MinContentWidth * ToolUiScale(),
                     ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x));
        LauncherInputText("##location", form.location);
        ImGui::SameLine();
        if (ToolButton(browse)) model.BrowseNewProjectLocation();
        ImGui::TextUnformatted("Engine");
        const char* preview = form.engine < engines.size() ? engines[form.engine].label.c_str() : "Select an engine";
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##engine", preview)) {
            for (std::size_t index = 0; index < engines.size(); ++index) {
                const bool selected = form.engine == index;
                if (ImGui::Selectable(engines[index].label.c_str(), selected)) form.engine = index;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (engines.empty()) LauncherToneText("No engines are available. Check the installation.", ToolTone::Warning);
        ImGui::SeparatorText("Project folder");
        LauncherPathText(model.NewProjectFolder());
        LauncherMutedText("A new folder will be created at this location.");
        const std::string problem = model.NewProjectProblem();
        if (problem.empty())
            LauncherToneText(ICON_FA_CHECK " Ready to create", ToolTone::Success);
        else
            LauncherToneText(problem.c_str(), ToolTone::Warning);
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (ToolButton("Cancel")) model.ClosePanel();
    const float buttonWidth = PrimaryWidth * ToolUiScale();
    ContinueToolToolbar(buttonWidth, true);
    ImGui::BeginDisabled(!model.NewProjectProblem().empty());
    if (ToolButton(ICON_FA_PLUS " Create project", {buttonWidth, 0}, true)) model.CreateProject();
    ImGui::EndDisabled();
}
