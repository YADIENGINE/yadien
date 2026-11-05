#include "Screens/LauncherScreens.h"
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include "Model/LauncherModel.h"
#include "Screens/ScreenLayout.h"
#include "ToolUi/ToolPixel.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWidgets.h"

namespace {
constexpr float MinContentWidth = 1.0f;
constexpr float PreviewTile = 40.0f;
} // namespace

void DrawNewProjectPanel(LauncherModel& model) {
    auto& form = model.NewProject();
    const auto engines = model.NewProjectEngines();
    const std::size_t picked = model.NewProjectEngine(engines);
    const std::string problem = model.NewProjectProblem();
    bool submitted = false;
    if (ImGui::BeginChild("##new-project-form", {0, -LauncherFooterHeight()})) {
        LauncherFieldLabel("NAME");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        submitted = LauncherInputText("##name", form.name, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
        LauncherFieldLabel("LOCATION");
        const char* browse = ICON_FA_FOLDER_OPEN " Browse";
        ImGui::PushFont(ToolFont(ToolFontRole::Medium), ToolMetrics::BodySize);
        const float browseWidth = ImGui::CalcTextSize(browse).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::PopFont();
        ImGui::SetNextItemWidth(
            std::max(MinContentWidth * ToolUiScale(),
                     ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x));
        LauncherInputText("##location", form.location);
        ImGui::SameLine();
        if (ToolButton(browse)) model.BrowseNewProjectLocation();
        ImGui::Dummy({0, ToolMetrics::Space * ToolUiScale()});
        LauncherFieldLabel("ENGINE");
        const char* preview = picked < engines.size() ? engines[picked].label.c_str() : "Select an engine";
        ImGui::SetNextItemWidth(-1);
        if (ToolBeginCombo("##engine", preview)) {
            for (std::size_t index = 0; index < engines.size(); ++index) {
                const bool selected = picked == index;
                if (ImGui::Selectable(engines[index].label.c_str(), selected)) form.engine = engines[index].id;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (engines.empty()) LauncherToneText("No engines are available. Check the installation.", ToolTone::Warning);
        if (picked < engines.size() && engines[picked].online)
            LauncherMutedText("This version is downloaded when the project is created.");
        ImGui::Dummy({0, ToolMetrics::Gap * ToolUiScale()});
        if (form.name.empty()) {
            // Nothing is wrong yet: say what the name may hold, not that it is missing.
            LauncherMutedText("Letters, digits and _. The project gets a folder of this name in the location above.");
        } else {
            // What will exist on disk: the same tile the list will show.
            ToolPixelTile(form.name, PreviewTile);
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (problem.empty())
                LauncherToneText(ICON_FA_CHECK " Ready to create", ToolTone::Success);
            else
                LauncherToneText(problem.c_str(), ToolTone::Warning);
            LauncherPathText(model.NewProjectFolder());
            ImGui::EndGroup();
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    const std::array buttons{LauncherFooterButton{"Cancel"},
                             LauncherFooterButton{ICON_FA_PLUS " Create project", true, problem.empty()}};
    const int pressed = DrawLauncherFooter(buttons);
    if (pressed == 0) model.ClosePanel();
    if (pressed == 1 || (submitted && problem.empty())) model.CreateProject();
}
