#pragma once

#include "Developer/DeveloperGit.h"
#include "Projects/CopyIntegrity.h"
#include "Projects/ProjectRow.h"
#include "Settings/LauncherSettings.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// What a project's tools recorded about its last build (engine Docs/ToolsContract.md, Project status files).
struct ProjectLastBuild {
    bool ok = false;
    std::string finished; // ISO 8601 UTC
};

// The slow facts of a Projects row, gathered in a background job after the list is shown: a copy's read-only check
// (hashes every engine file), in developer mode a git engine's status, and the state of its build folders.
struct ProjectDetails {
    std::filesystem::path root;
    std::optional<CopyIntegrity> integrity; // Copy only; not Clean(): "N engine files modified" + Repair
    std::optional<EngineGitStatus> git;     // Developer mode, git engine only
    std::string error;                      // Why a check could not run (shown as a warning)
    std::optional<ProjectLastBuild> lastBuild;
    std::uintmax_t buildBytes = 0;   // Everything in its build folders
    std::filesystem::path lastFrame; // A recent frame the editor saved; empty when there is none
};

// One entry per row whose folder exists, in row order. Stops early (fewer entries) when `cancel` is set.
[[nodiscard]] std::vector<ProjectDetails> InspectProjectDetails(const std::vector<ProjectRow>& rows, LauncherMode mode,
                                                                const std::atomic<bool>* cancel = nullptr);

// Deletes the project's build folders; the next build starts from scratch. Fails while its editor or game holds them.
[[nodiscard]] Status CleanProjectBuild(const std::filesystem::path& root);
