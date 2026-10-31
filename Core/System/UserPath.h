#pragma once

#include "Platform/Result.h"
#include <filesystem>
#include <string>
#include <string_view>

// Settings → "Add to PATH" (design D15): the folder of <engine>.com / <engine>.exe in the user's PATH
// (HKCU\Environment, no elevation). New terminals see the change; Windows is told through WM_SETTINGCHANGE.

// The folder to add: the running launcher's.
[[nodiscard]] std::filesystem::path LauncherFolder();

[[nodiscard]] Result<bool> IsOnUserPath(const std::filesystem::path& folder);
[[nodiscard]] Status AddToUserPath(const std::filesystem::path& folder);
[[nodiscard]] Status RemoveFromUserPath(const std::filesystem::path& folder);

// The text rules, apart from the registry. Entries are ';'-separated; matching ignores case, a trailing separator and
// surrounding quotes; empty entries are kept as they are.
[[nodiscard]] bool PathListContains(std::string_view list, const std::filesystem::path& folder);
// Appends `folder` unless it is already listed.
[[nodiscard]] std::string PathListWith(std::string_view list, const std::filesystem::path& folder);
// Removes every entry naming `folder`.
[[nodiscard]] std::string PathListWithout(std::string_view list, const std::filesystem::path& folder);
