#pragma once

#include <imgui.h>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include "Engines/EngineManifest.h"
#include "ToolUi/ToolTheme.h"

// Small presentation helpers shared only by the launcher screens.
bool LauncherInputText(const char* label, std::string& value, ImGuiInputTextFlags flags = 0,
                       const char* hint = nullptr);
void LauncherMutedText(const char* text);
void LauncherToneText(const char* text, ToolTone tone);
// A small quiet label above a field or group.
void LauncherFieldLabel(const char* text);
// One line, "~" for the home folder, the middle shortened to fit; the whole path in a tooltip.
void LauncherPathText(const std::filesystem::path& path, float maxWidth = 0);
// The same text as drawn: shortened for `width` in the current font.
[[nodiscard]] std::string LauncherShortPath(const std::filesystem::path& path, float width);
void DrawLauncherActivity(const char* label);
// A wrapping bullet line of release notes; both notes views use it.
void DrawLauncherBullet(const char* text);
void DrawLauncherNotes(const std::vector<EngineNote>& notes);

struct LauncherFooterButton {
    const char* label;
    bool primary = false;
    bool enabled = true;
    ToolTone tone = ToolTone::Accent; // Text color of a button that warns (not primary); Accent keeps the default
};
// Dialog footer: buttons on the right, the main action last and focused; returns the pressed one's index or -1.
// `afterItem`: another button already stands on the footer's left.
[[nodiscard]] int DrawLauncherFooter(std::span<const LauncherFooterButton> buttons, bool afterItem = false);
[[nodiscard]] float LauncherFooterHeight();
