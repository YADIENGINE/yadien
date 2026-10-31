#pragma once

#include "Platform/Process.h"
#include "Platform/Result.h"
#include <filesystem>
#include <string>
#include <vector>

// Git work of developer projects, whose Engine/ is a submodule of the engine repository (design D4). User mode never
// runs any of this (D14). Every operation streams git's output through `onLine` and stops at the first failing step.

// `git -C <folder> <arguments...>` (git from PATH).
[[nodiscard]] ProcessResult RunGit(const std::filesystem::path& folder, const std::vector<std::string>& arguments,
                                   const OutputLine& onLine);

struct EngineGitStatus {
    std::string branch;        // Empty when HEAD is detached
    std::string trackedBranch; // .gitmodules submodule.Engine.branch ("main" when unset)
    std::string head;          // Engine commit
    int changedFiles = 0;      // Uncommitted changes inside Engine/
    bool hasUpstream = false;
    int ahead = 0; // Engine commits not on the upstream yet (as of the last fetch)
    int behind = 0;
    bool pointerChanged = false; // The project's recorded Engine commit differs from Engine's HEAD

    [[nodiscard]] bool Detached() const { return branch.empty(); }
};

[[nodiscard]] Result<EngineGitStatus> ReadEngineGitStatus(const std::filesystem::path& projectRoot);

// "Update engine": Engine must have no uncommitted changes. Fetches, puts Engine on its tracked branch (fixing a
// detached HEAD), fast-forwards it to the remote and commits the new pointer in the project ("Update Engine").
[[nodiscard]] Status UpdateEngineFromRemote(const std::filesystem::path& projectRoot, const OutputLine& onLine);

// "Publish engine changes": the two-step flow. Engine: back on its branch (a detached HEAD whose commits are not on
// the branch stops), commit everything with `message`, pull --rebase, push. Project: commit the pointer
// ("Update Engine: <message>"). The project's commit is never pushed by the launcher.
[[nodiscard]] Status PublishEngineChanges(const std::filesystem::path& projectRoot, const std::string& message,
                                          const OutputLine& onLine);

// New developer project at `root` (empty folder): git init, Engine/ as a submodule of `repository` tracking `branch`.
// NewProject calls it before the engine's `new`, then CommitNewProject.
[[nodiscard]] Status InitDevelopmentRepository(const std::filesystem::path& root, const std::string& repository,
                                               const std::string& branch, const OutputLine& onLine);
[[nodiscard]] Status CommitNewProject(const std::filesystem::path& root, const std::string& name,
                                      const OutputLine& onLine);
