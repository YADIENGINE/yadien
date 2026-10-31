#pragma once

#include "Engines/EngineLibrary.h"
#include "Platform/Result.h"
#include "Projects/ProjectInfo.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Replacing a project's Engine/ with a stable copy from Engines/ (design D4): always shown first, applied only after
// the user confirms. Allowed for copies (update, or the same version to repair edits), unregistered engines and
// projects without an engine; never for git-managed engines.
struct EngineUpdatePlan {
    std::filesystem::path projectRoot;
    std::optional<EngineVersion> from; // Unknown for unregistered / missing engines
    AvailableEngine target;
    std::vector<std::string> overwritten; // Files whose current content is lost (edits in a copy; any file otherwise)
    std::vector<std::string> deleted;     // Files not from the engine (user-added) that the new copy does not have
    std::vector<EngineNote> notes; // From the notes of the target, newest first (all of them when `from` is unknown)
    bool mayBreak = false;         // The step crosses a breaking boundary (MayBreak); unknown origin counts as breaking
};

// Hashes the current engine (see CheckCopyIntegrity).
[[nodiscard]] Result<EngineUpdatePlan> PlanEngineUpdate(const ProjectInfo& project, const AvailableEngine& target);

// Copies the target beside Engine/, then swaps the folders; on failure the old engine stays in place. Close the
// project's editor and game first: Windows does not move folders with open files.
[[nodiscard]] Status ApplyEngineUpdate(const EngineUpdatePlan& plan);
