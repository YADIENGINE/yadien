#pragma once

#include "Platform/Result.h"
#include <filesystem>
#include <string>
#include <vector>

// User: engine copies only. Developer: unlocked with the password; adds submodule projects and their git work.
enum class LauncherMode { User, Developer };
// Order of the Projects list: most recently opened first (never opened last), by name, by folder.
enum class ProjectSort { LastOpened, Name, Path };

struct ProjectEntry {
    std::filesystem::path root;
    std::string lastOpened; // ISO 8601 UTC ("2026-10-31T18:20:00Z"); empty when never opened from the launcher
};

// Settings.json (Docs/CoreApi.md → Settings). Unknown fields from a newer launcher are dropped on save.
struct LauncherSettings {
    LauncherMode mode = LauncherMode::User;
    std::vector<ProjectEntry> projects;   // In the order added; the UI sorts
    std::filesystem::path projectsFolder; // Where "New project" proposes; empty → DefaultProjectsFolder()
    float uiScale = 1.0f;
    ProjectSort projectSort = ProjectSort::LastOpened;
};

inline constexpr int LauncherSettingsFormat = 1;

// A missing file is a first start: default settings. A broken file is an error (it is never overwritten silently).
[[nodiscard]] Result<LauncherSettings> LoadLauncherSettings(const std::filesystem::path& file);
[[nodiscard]] Status SaveLauncherSettings(const std::filesystem::path& file, const LauncherSettings& settings);

// Documents\<Product> Projects
[[nodiscard]] std::filesystem::path DefaultProjectsFolder();
[[nodiscard]] std::filesystem::path ProjectsFolderOf(const LauncherSettings& settings);
