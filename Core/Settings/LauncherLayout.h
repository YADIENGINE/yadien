#pragma once

#include "Platform/Result.h"
#include <filesystem>

// Where the launcher finds its parts. Installed (D15): everything beside the executable —
// <install>/Engines, <install>/Tools, <install>/Settings.json. A development build has LauncherLayout.json beside
// its executable (written by CMake) pointing at the repository's Engines/ and ToolCache/ and a data folder in the
// build tree, so a development launcher never touches the installed one's settings.
struct LauncherLayout {
    std::filesystem::path enginesFolder;
    std::filesystem::path toolsFolder; // Toolchain.json + python/, cmake/, ninja/
    std::filesystem::path dataFolder;  // Settings.json

    [[nodiscard]] std::filesystem::path SettingsFile() const { return dataFolder / "Settings.json"; }
};

inline constexpr const char* LauncherLayoutFile = "LauncherLayout.json";

// Layout for an executable living in `executableFolder`.
[[nodiscard]] Result<LauncherLayout> ResolveLauncherLayout(const std::filesystem::path& executableFolder);
