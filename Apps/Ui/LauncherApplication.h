#pragma once

#include "Model/LauncherModel.h"
#include "Runtime/Game.h"
#include "ToolUi/ToolUiContext.h"
#include <filesystem>

// <engine>.exe: an engine Game window drawn entirely with the tool UI (ToolUi). Ticks the model, draws the screens.
class LauncherApplication : public Game {
public:
    explicit LauncherApplication(LauncherLayout layout);
    ~LauncherApplication() override;

protected:
    void Update(float realSeconds) override;
    void DrawWindow() const override;

private:
    // After the base Game has created the window; destroyed before it.
    mutable LauncherModel _model;
    mutable ToolUiContext _ui;
};
