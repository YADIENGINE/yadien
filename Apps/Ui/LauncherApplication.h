#pragma once

#include "Model/LauncherModel.h"
#include "Runtime/Game.h"
#include "ToolUi/ToolUiContext.h"
#include <filesystem>
#include <memory>

// <engine>.exe: an engine Game window drawn entirely with the tool UI (ToolUi). Ticks the model, draws the screens.
class LauncherApplication : public Game {
public:
    LauncherApplication(LauncherLayout layout, std::unique_ptr<UpdateBackend> updates, bool justUpdated);
    ~LauncherApplication() override;

protected:
    void Update(float realSeconds) override;
    void DrawToolOverlay() const override;

private:
    // After the base Game has created the window; destroyed before it.
    mutable LauncherModel _model;
    mutable ToolUiContext _ui;
};
