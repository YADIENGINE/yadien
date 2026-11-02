#include "UiSmoke.h"
#include <imgui_internal.h>
#include <raylib.h>
#include <rlgl.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>
#include <utility>
#include "Screens/LauncherScreens.h"
#include "System/CommandActivity.h"
#include "System/UserPath.h"
#include "ToolUi/ToolTheme.h"
#include "ToolUi/ToolWindow.h"

struct UpdateSmokeState {
    std::atomic<bool> upToDate = true;
    std::atomic<bool> finishDownload = false;
    std::atomic<bool> failDownload = false;
};

namespace {
enum class SmokeView {
    Empty,
    Create,
    Settings,
    Engines,
    Scrolled,
    List,
    Inspector,
    Search,
    NoResults,
    EngineChange,
    EngineTrialFailed,
    EngineTrialNotTested,
    UpdateUpToDate,
    UpdateAvailable,
    UpdateButton,
    UpdateDownloading,
    UpdateFailed,
    UpdateReady,
    UpdateReadyButton,
    UpdateWaiting
};
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
    SmokeStage{"engine-change", SmokeView::EngineChange},
    SmokeStage{"engine-change-narrow-150", SmokeView::EngineChange, 900, 560, 150},
    SmokeStage{"engine-trial-failed", SmokeView::EngineTrialFailed},
    SmokeStage{"engine-trial-failed-narrow-150", SmokeView::EngineTrialFailed, 900, 560, 150},
    SmokeStage{"engine-trial-not-tested", SmokeView::EngineTrialNotTested},
    // One update walked through its states, in this order.
    SmokeStage{"update-up-to-date", SmokeView::UpdateUpToDate},
    SmokeStage{"update-available", SmokeView::UpdateAvailable},
    SmokeStage{"update-available-narrow-150", SmokeView::UpdateAvailable, 900, 560, 150},
    SmokeStage{"update-button", SmokeView::UpdateButton},
    SmokeStage{"update-button-narrow", SmokeView::UpdateButton, 900, 560},
    SmokeStage{"update-button-compact", SmokeView::UpdateButton, 900, 560, 200},
    SmokeStage{"update-downloading", SmokeView::UpdateDownloading},
    SmokeStage{"update-failed", SmokeView::UpdateFailed},
    SmokeStage{"update-ready", SmokeView::UpdateReady},
    SmokeStage{"update-ready-button", SmokeView::UpdateReadyButton},
    SmokeStage{"update-waiting", SmokeView::UpdateWaiting},
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
bool Updating(SmokeView view) { return view >= SmokeView::UpdateUpToDate; }
bool ChangingEngine(SmokeView view) {
    return view >= SmokeView::EngineChange && view <= SmokeView::EngineTrialNotTested;
}
bool TryingEngine(SmokeView view) {
    return view == SmokeView::EngineTrialFailed || view == SmokeView::EngineTrialNotTested;
}

EngineTrial ScriptedTrial(SmokeView view) {
    if (view == SmokeView::EngineTrialNotTested)
        return {TrialVerdict::NotTested,
                "Game build",
                {"error: cmake was not found"},
                "Game build stopped with exit code 2"};
    return {TrialVerdict::Failed,
            "Editor build",
            {"ninja: error: unknown target 'MyGameEditorWorker'",
             "Editor/Scenes/FirstScene.cpp(12): error C2065: 'EditorPreview': undeclared identifier, and a much longer "
             "line so that wrapping inside the panel can be judged at the narrow window and the large scale"},
            ""};
}

constexpr int SmokeDownloadPercent = 42;
constexpr std::uint64_t SmokeDownloadBytes = 9045317;
constexpr const char* SmokeNotes = R"(Early preview. For testing. Windows only, unsigned (SmartScreen asks to confirm).

Launcher 0.3.1, contains engine 0.3.1.

- The launcher now updates itself: a button in the top bar shows when a new version is out
- Settings has a switch for the check at startup

Engine 0.3.1:

- Breaking: Editor previews are editor scenes now. Migrate: rename the worker target in the project's CMakeLists.txt
- Console scrolling keeps its place while new output arrives
- A much longer line so the panel's wrapping can be judged: new standalone graphics are registered by default and show up in the Assets panel without restarting the editor
)";

class SmokeUpdates final : public UpdateBackend {
public:
    explicit SmokeUpdates(std::shared_ptr<UpdateSmokeState> state)
        : _state(std::move(state)) {}
    std::optional<AvailableUpdate> Pending() override { return std::nullopt; }
    Result<std::optional<AvailableUpdate>> Check(const std::atomic<bool>*) override {
        if (_state->upToDate) return std::optional<AvailableUpdate>{};
        return std::optional<AvailableUpdate>(AvailableUpdate{"0.3.1", SmokeDownloadBytes, SmokeNotes});
    }
    Status Download(const std::function<void(int)>& progress, const std::atomic<bool>* cancel) override {
        progress(SmokeDownloadPercent);
        while (!_state->finishDownload && !cancel->load())
            std::this_thread::yield();
        if (cancel->load()) return Status::Failure("Download canceled");
        if (_state->failDownload) return Status::Failure("Cannot download the update (check the internet connection)");
        return Status::Success();
    }
    Status ApplyAfterExit(bool) override { return Status::Success(); }

private:
    std::shared_ptr<UpdateSmokeState> _state;
};
} // namespace

UiSmoke::UiSmoke(LauncherLayout empty, LauncherLayout populated, LauncherLayout updating, std::filesystem::path output)
    : Game(SmokeSettings())
    , _output(std::move(output))
    , _updateState(std::make_shared<UpdateSmokeState>())
    , _empty(std::move(empty))
    , _trialScript(std::make_shared<EngineTrial>())
    , _populated(
          std::move(populated), nullptr, false,
          [script = _trialScript](const EngineUpdatePlan&, JobContext&) -> Result<EngineTrial> { return *script; })
    , _updating(std::move(updating), std::make_unique<SmokeUpdates>(_updateState))
    , _ui(ToolUiOptions{.keyboardNavigation = true}) {}

// Moves the one update on to this stage's state; the panel closes first (ClosePanel in Update).
void UiSmoke::AdvanceUpdate() {
    switch (Stages[_stage].view) {
    case SmokeView::UpdateUpToDate: _updating.CheckForUpdates(); break;
    case SmokeView::UpdateAvailable:
        _updateState->upToDate = false;
        _updating.CheckForUpdates();
        break;
    case SmokeView::UpdateDownloading:
        _updating.DownloadUpdate();
        _updating.OpenUpdatePanel();
        break;
    case SmokeView::UpdateFailed:
        _updateState->failDownload = true;
        _updateState->finishDownload = true;
        _updating.OpenUpdatePanel();
        break;
    case SmokeView::UpdateReady:
        _updateState->failDownload = false;
        _updating.DownloadUpdate();
        _updating.OpenUpdatePanel();
        break;
    case SmokeView::UpdateWaiting:
        _runningCommand = std::make_unique<CommandActivity>(LauncherFolder());
        _updating.RestartToUpdate();
        _updating.OpenUpdatePanel();
        break;
    default: break;
    }
}

void UiSmoke::Update(float realSeconds) {
    (void)realSeconds;
    if (_frame == FramesPerStage) {
        ++_stage;
        _frame = 0;
        _trialStarted = false;
    }
    if (_stage == Stages.size()) {
        RequestQuit();
        return;
    }
    const auto& stage = Stages[_stage];
    auto& model = Updating(stage.view) ? _updating : Populated(stage.view) ? _populated : _empty;
    if (_frame == 0) {
        SetWindowSize({stage.width, stage.height});
        model.ClosePanel();
        if (Updating(stage.view)) AdvanceUpdate();
        model.SelectTab(stage.view == SmokeView::Engines ? LauncherTab::Engines : LauncherTab::Projects);
        model.Filter().clear();
        if (stage.view == SmokeView::Create) model.Create();
        if (stage.view == SmokeView::Settings) model.OpenPanel(LauncherPanel::Settings);
        if (stage.view >= SmokeView::Inspector) model.Select(0);
        if (stage.view == SmokeView::Search) model.Filter() = "Workspace";
        if (stage.view == SmokeView::NoResults) model.Filter() = "no-project-has-this-name";
        if (ChangingEngine(stage.view)) {
            *_trialScript = ScriptedTrial(stage.view);
            model.ChangeEngine(0, EngineChange::Update);
        }
    }
    model.Tick();
    if (ChangingEngine(stage.view)) { // Waits for its jobs: the panel is what the snapshot is for
        if (TryingEngine(stage.view) && !_trialStarted && model.Plan()) {
            model.ConfirmEngineChange();
            _trialStarted = true;
        }
        const bool waiting = model.PlanRunning() || model.CurrentJob() || (TryingEngine(stage.view) && !model.Trial());
        if (waiting && _frame > 0) return;
    }
    ++_frame;
}

void UiSmoke::DrawToolOverlay() const {
    if (_stage == Stages.size()) return;
    const auto& stage = Stages[_stage];
    auto& model = Updating(stage.view) ? _updating : Populated(stage.view) ? _populated : _empty;
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
