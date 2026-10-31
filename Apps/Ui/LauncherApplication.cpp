#include "LauncherApplication.h"
#include <string>
#include <utility>
#include "LauncherEntryPoint.h"
#include "Screens/LauncherScreens.h"
#include "ToolUi/ToolWindow.h"

namespace {
constexpr int WindowWidth = 1180;
constexpr int WindowHeight = 760;
constexpr int MinWindowWidth = 900;
constexpr int MinWindowHeight = 560;
constexpr int TargetFps = 60;

GameSettings LauncherWindow() {
    static const std::string title = std::string(LAUNCHER_PRODUCT_NAME);
    WindowSettings window;
    window.title = title.c_str();
    window.width = WindowWidth;
    window.height = WindowHeight;
    window.minWidth = MinWindowWidth;
    window.minHeight = MinWindowHeight;
    window.targetFps = TargetFps;
    window.alwaysRun = true; // Jobs report progress while the window is in the background
    return ToolGameSettings(std::string(LAUNCHER_EXECUTABLE_NAME) + "-launcher", window);
}
} // namespace

int RunLauncherWindow(LauncherLayout layout) {
    LauncherApplication application(std::move(layout));
    application.Run();
    return 0;
}

LauncherApplication::LauncherApplication(LauncherLayout layout)
    : Game(LauncherWindow())
    , _model(std::move(layout)) {}

LauncherApplication::~LauncherApplication() = default;

void LauncherApplication::Update(float realSeconds) {
    (void)realSeconds;
    _model.Tick();
    if (_model.QuitRequested()) RequestQuit();
}

void LauncherApplication::DrawWindow() const {
    _ui.BeginFrame(_model.UiScalePercent());
    DrawLauncher(_model);
    _ui.EndFrame();
}
