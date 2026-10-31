#pragma once

#include "Platform/Result.h"
#include "Settings/LauncherSettings.h"
#include <filesystem>
#include <string>
#include <vector>

// Project list operations on LauncherSettings; the caller saves. None of them touches the project on disk.

// "Add existing": the folder must hold a Project.json and not be listed yet. The root is stored canonical.
[[nodiscard]] Status AddProject(LauncherSettings& settings, const std::filesystem::path& root);
// "Remove from list"; the folder stays on disk.
[[nodiscard]] Status RemoveProject(LauncherSettings& settings, const std::filesystem::path& root);
// Records an opening (`now` in ISO 8601 UTC; CurrentTimeIso()); adds the project when it is not listed.
void MarkProjectOpened(LauncherSettings& settings, const std::filesystem::path& root, const std::string& now);
// Entries sorted for the Projects screen: most recently opened first, never opened last (by path).
[[nodiscard]] std::vector<ProjectEntry> RecentProjects(const LauncherSettings& settings);

[[nodiscard]] std::string CurrentTimeIso();
// The form roots are compared and stored in: absolute, normalized, without a trailing separator.
[[nodiscard]] std::filesystem::path CanonicalRoot(const std::filesystem::path& root);
