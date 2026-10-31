#pragma once

#include <imgui.h>
#include <filesystem>
#include <string>
#include <vector>
#include "Engines/EngineManifest.h"
#include "ToolUi/ToolTheme.h"

// Small presentation helpers shared only by the launcher screens.
bool LauncherInputText(const char* label, std::string& value, ImGuiInputTextFlags flags = 0,
                       const char* hint = nullptr);
void LauncherMutedText(const char* text);
void LauncherToneText(const char* text, ToolTone tone);
void LauncherPathText(const std::filesystem::path& path, float maxWidth = 0);
void DrawLauncherActivity(const char* label);
void DrawLauncherNotes(const std::vector<EngineNote>& notes);
