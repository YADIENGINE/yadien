#pragma once

#include <filesystem>
#include <memory>
#include "Model/LauncherModel.h"
#include "Runtime/Game.h"
#include "ToolUi/ToolUiContext.h"

struct UpdateSmokeState; // A scripted update backend's switches (UiSmoke.cpp)

// Opt-in snapshots of the production screens, fonts and backend; all models use temporary data.
class UiSmoke : public Game {
public:
    UiSmoke(LauncherLayout empty, LauncherLayout populated, LauncherLayout updating, std::filesystem::path output);
    [[nodiscard]] int Failures() const { return _failures; }

protected:
    void Update(float realSeconds) override;
    void DrawToolOverlay() const override;

private:
    void AdvanceUpdate();

    std::filesystem::path _output;
    std::shared_ptr<UpdateSmokeState> _updateState;
    std::shared_ptr<EngineTrial> _trialScript; // What the populated model's engine trial answers
    mutable LauncherModel _empty;
    mutable LauncherModel _populated;
    mutable LauncherModel _updating;
    std::unique_ptr<CommandActivity> _runningCommand; // Makes the update wait, for its stage
    mutable ToolUiContext _ui;
    std::size_t _stage = 0;
    bool _trialStarted = false;
    int _frame = 0;
    mutable int _failures = 0;
};
