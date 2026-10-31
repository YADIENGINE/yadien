#pragma once

#include <filesystem>
#include "Model/LauncherModel.h"
#include "Runtime/Game.h"
#include "ToolUi/ToolUiContext.h"

// Opt-in snapshots of the production screens, fonts and backend; all models use temporary data.
class UiSmoke : public Game {
public:
    UiSmoke(LauncherLayout empty, LauncherLayout populated, std::filesystem::path output);
    [[nodiscard]] int Failures() const { return _failures; }

protected:
    void Update(float realSeconds) override;
    void DrawToolOverlay() const override;

private:
    std::filesystem::path _output;
    mutable LauncherModel _empty;
    mutable LauncherModel _populated;
    mutable ToolUiContext _ui;
    std::size_t _stage = 0;
    int _frame = 0;
    mutable int _failures = 0;
};
