#include "UiSmoke.h"
#include <imgui_internal.h>
#include <rlgl.h>
#include <array>
#include <iostream>
#include <string_view>
#include <utility>
#include "Screens/LauncherScreens.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWindow.h"

namespace {
enum class SmokeView { Empty, Create, Settings, Engines, Scrolled, List, Inspector, Search, NoResults };
struct SmokeStage {
    const char* name;
    SmokeView view;
    int width = 1180;
    int height = 760;
    int scale = 100;
};
constexpr std::array Stages{
    SmokeStage{"empty", SmokeView::Empty},
    SmokeStage{"create", SmokeView::Create},
    SmokeStage{"settings", SmokeView::Settings},
    SmokeStage{"engines", SmokeView::Engines},
    SmokeStage{"projects", SmokeView::List},
    SmokeStage{"inspector", SmokeView::Inspector},
    SmokeStage{"search", SmokeView::Search},
    SmokeStage{"no-results", SmokeView::NoResults},
    SmokeStage{"narrow", SmokeView::Inspector, 900, 560},
    SmokeStage{"narrow-125", SmokeView::Inspector, 900, 560, 125},
    SmokeStage{"narrow-150", SmokeView::Inspector, 900, 560, 150},
    SmokeStage{"large-200", SmokeView::Inspector, 1180, 760, 200},
    SmokeStage{"empty-200", SmokeView::Empty, 900, 560, 200},
    SmokeStage{"empty-200-list", SmokeView::Scrolled, 900, 560, 200},
    SmokeStage{"empty-75", SmokeView::Empty, 900, 560, 75},
};
constexpr int FramesPerStage = 5;

GameSettings SmokeSettings() {
    WindowSettings window;
    window.title = "Launcher UI review";
    window.width = Stages.front().width;
    window.height = Stages.front().height;
    window.visible = false;
    window.alwaysRun = true;
    window.targetFps = 60;
    return ToolGameSettings("launcher-ui-review", window);
}

bool Populated(SmokeView view) { return view >= SmokeView::List; }
} // namespace

UiSmoke::UiSmoke(LauncherLayout empty, LauncherLayout populated, std::filesystem::path output)
    : Game(SmokeSettings())
    , _output(std::move(output))
    , _empty(std::move(empty))
    , _populated(std::move(populated))
    , _ui(ToolUiOptions{.keyboardNavigation = true}) {}

void UiSmoke::Update(float realSeconds) {
    (void)realSeconds;
    if (_frame == FramesPerStage) {
        ++_stage;
        _frame = 0;
    }
    if (_stage == Stages.size()) {
        RequestQuit();
        return;
    }
    const auto& stage = Stages[_stage];
    auto& model = Populated(stage.view) ? _populated : _empty;
    if (_frame == 0) {
        SetWindowSize({stage.width, stage.height});
        model.ClosePanel();
        model.SelectTab(stage.view == SmokeView::Engines ? LauncherTab::Engines : LauncherTab::Projects);
        model.Filter().clear();
        if (stage.view == SmokeView::Create) model.Create();
        if (stage.view == SmokeView::Settings) model.OpenPanel(LauncherPanel::Settings);
        if (stage.view >= SmokeView::Inspector) model.Select(0);
        if (stage.view == SmokeView::Search) model.Filter() = "Workspace";
        if (stage.view == SmokeView::NoResults) model.Filter() = "no-project-has-this-name";
    }
    model.Tick();
    ++_frame;
}

void UiSmoke::DrawWindow() const {
    if (_stage == Stages.size()) return;
    const auto& stage = Stages[_stage];
    auto& model = Populated(stage.view) ? _populated : _empty;
    _ui.BeginFrame(stage.scale);
    DrawLauncher(model);
    // Inspect that the list remains reachable when the smallest window is zoomed to 200%.
    for (auto* window : ImGui::GetCurrentContext()->Windows) {
        const std::string_view name(window->Name);
        const auto region = name.substr(name.find_last_of('/') + 1);
        if (window->Active && region.starts_with("##project-workspace_")) {
            if (stage.view == SmokeView::Scrolled)
                ImGui::SetScrollY(window, window->ScrollMax.y);
            else if (_frame == 1)
                ImGui::SetScrollY(window, 0);
        }
    }
    if (_frame == FramesPerStage) {
        for (const auto* window : ImGui::GetCurrentContext()->Windows) {
            if (window->Active && window->ScrollMax.x > 1.0f) {
                std::cerr << stage.name << ": horizontal overflow in " << window->Name << '\n';
                ++_failures;
            }
            const std::string_view name(window->Name);
            if (window->Active && window->ScrollMax.y > 1.0f &&
                (name.find("##project-toolbar") != std::string_view::npos ||
                 name.find("##application-header") != std::string_view::npos)) {
                std::cerr << stage.name << ": persistent controls require scrolling in " << name << '\n';
                ++_failures;
            }
        }
        const auto& style = ImGui::GetStyle();
        const float scale = ToolUiScale();
        ImGui::PushFont(ToolFont(ToolFontRole::Heading), ToolMetrics::HeadingSize);
        if (ToolUiScale() != scale) ++_failures;
        ImGui::PopFont();
        if (style.FontScaleMain != static_cast<float>(stage.scale) / 100.0f) ++_failures;
    }
    _ui.EndFrame();
    if (_frame == FramesPerStage) {
        rlDrawRenderBatchActive();
        const auto file = _output / (std::string(stage.name) + ".png");
        const Image image = LoadImageFromScreen();
        if (!ExportImage(image, file.string().c_str())) ++_failures;
        UnloadImage(image);
        std::cout << "Snapshot: " << file.string() << '\n';
    }
}
