#pragma once

#include "Engines/EngineLibrary.h"
#include "Platform/Result.h"
#include "Projects/ProjectInfo.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Replacing a project's Engine/ with a stable copy (design D4): always shown first, applied only after the user
// confirms. Allowed for copies (any other version, or the same one to repair edits), unregistered engines and projects
// without an engine; never for git-managed engines, nor for an engine this launcher cannot drive.
struct EngineUpdatePlan {
    std::filesystem::path projectRoot;
    std::optional<EngineVersion> from; // Unknown for unregistered / missing engines
    AvailableEngine target;
    std::vector<std::string> overwritten; // Files whose current content is lost (edits in a copy; any file otherwise)
    std::vector<std::string> deleted;     // Files not from the engine (user-added) that the new copy does not have
    // What the step brings, newest first (all of the target's when `from` is unknown); going back: what it takes away.
    std::vector<EngineNote> notes;
    bool mayBreak = false; // The step crosses a breaking boundary (MayBreak); unknown origin counts as breaking
    bool wentBack = false; // The target is older than the project's engine
};

// Hashes the current engine (see CheckCopyIntegrity).
[[nodiscard]] Result<EngineUpdatePlan> PlanEngineUpdate(const ProjectInfo& project, const AvailableEngine& target);

// The project's build folders / release folder from Project.json, relative to its root. A path that leaves the project
// or is its engine is not listed.
[[nodiscard]] std::vector<std::filesystem::path> ProjectBuildFolders(const std::filesystem::path& root);
[[nodiscard]] std::optional<std::filesystem::path> ProjectReleaseFolder(const std::filesystem::path& root);

// Copies the target beside Engine/, removes the build folders, then swaps the folders; on failure the old engine stays
// in place. A build tree left in place would be wrong: the files of a copy can be older than its objects, so a build
// would report success without compiling anything. Close the project's editor and game first: Windows does not move
// folders with open files.
[[nodiscard]] Status ApplyEngineUpdate(const EngineUpdatePlan& plan);
