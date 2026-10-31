#pragma once

#include "Developer/DeveloperGit.h"
#include "Projects/CopyIntegrity.h"
#include "Projects/ProjectRow.h"
#include "Settings/LauncherSettings.h"
#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The slow facts of a Projects row, gathered in a background job after the list is shown:
// a copy's read-only check (hashes every engine file) and, in developer mode, a git engine's status.
struct ProjectDetails {
    std::filesystem::path root;
    std::optional<CopyIntegrity> integrity; // Copy only; not Clean(): "N engine files modified" + Repair
    std::optional<EngineGitStatus> git;     // Developer mode, git engine only
    std::string error;                      // Why a check could not run (shown as a warning)
};

// One entry per row that has something to check, in row order. Stops early (fewer entries) when `cancel` is set.
[[nodiscard]] std::vector<ProjectDetails> InspectProjectDetails(const std::vector<ProjectRow>& rows, LauncherMode mode,
                                                                const std::atomic<bool>* cancel = nullptr);
