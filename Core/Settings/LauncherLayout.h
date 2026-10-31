#pragma once

#include "Platform/Result.h"
#include <filesystem>

// Where the launcher finds its parts. Installed (D15): Engines/ and Tools/ beside the executable; Settings.json beside
// it too, except in a Velopack install (<root>/current/ + <root>/Update.exe): Velopack replaces current/ on an update
// and the whole <root> when a newer Setup is run over it, so the data (settings, audio tools) lives in the sibling
// folder "<root> Data" and the uninstall hook removes it. A development build has LauncherLayout.json beside
// its executable (written by CMake) pointing at the repository's Engines/ and ToolCache/ and a data folder in the
// build tree, so a development launcher never touches the installed one's settings.
struct LauncherLayout {
    std::filesystem::path enginesFolder;
    std::filesystem::path toolsFolder; // Toolchain.json + python/, cmake/, ninja/
    std::filesystem::path dataFolder;  // Settings.json, AudioTools/
    bool velopackInstall = false;      // Installed by Velopack: dataFolder is the launcher's to delete on uninstall

    [[nodiscard]] std::filesystem::path SettingsFile() const { return dataFolder / "Settings.json"; }
    // The audio tools (System/AudioTools.h) live beside the settings: kept across launcher updates.
    [[nodiscard]] std::filesystem::path AudioToolsFolder() const { return dataFolder / "AudioTools"; }
};

inline constexpr const char* LauncherLayoutFile = "LauncherLayout.json";
// Beside the executable in a package (Scripts/package.py): every third-party license the launcher ships.
inline constexpr const char* LicenseNoticesFile = "ThirdPartyNotices.txt";

// Layout for an executable living in `executableFolder`.
[[nodiscard]] Result<LauncherLayout> ResolveLauncherLayout(const std::filesystem::path& executableFolder);
