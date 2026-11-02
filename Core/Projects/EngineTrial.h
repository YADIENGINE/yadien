#pragma once

#include "Cli/EngineCommand.h"
#include "Jobs/BackgroundJob.h"
#include "Platform/Result.h"
#include "Projects/EngineUpdate.h"
#include <filesystem>
#include <string>
#include <vector>

// Trying an engine on a project before the project changes (design D16): a temporary copy of the project (without its
// Engine/, build and release folders) gets the target engine and goes through a clean build of the game, of the editor
// and the project's tests. A step only runs when Project.json has its section. The real project is not touched; moving
// it is ApplyEngineUpdate, afterwards.
//
// The whole check is needed: moving an engine under an existing build tree reports success without compiling anything,
// and a project can pass the game build and still fail the editor's (engine 0.3 renamed the default worker target).
enum class TrialVerdict {
    Passed,    // Every step succeeded
    Failed,    // The project does not work with this engine yet (exit code 1)
    NotTested, // The tools could not do the job (missing compiler, nothing to build): says nothing about the project
};

struct EngineTrial {
    TrialVerdict verdict = TrialVerdict::NotTested;
    std::string check;               // The step that stopped it ("Editor build")
    std::vector<std::string> errors; // Its error summary, or the end of its output
    std::string reason;              // NotTested: why
};

// Copying, then the three build steps.
inline constexpr int EngineTrialSteps = 4;

// What a trial of this project goes through, in order: the copy, then the checks Project.json has a section for.
[[nodiscard]] std::vector<std::string> EngineTrialStepNames(const std::filesystem::path& projectRoot);

// `folder` is the launcher's own and is removed before and after. A canceled trial is a failure ("Canceled").
[[nodiscard]] Result<EngineTrial> RunEngineTrial(const EngineUpdatePlan& plan, const std::filesystem::path& folder,
                                                 const EngineRunner& runEngine, JobContext& job);

// Text to give an AI (or read): what was tried, what failed and the migration steps of the versions it crosses.
[[nodiscard]] std::string EngineTrialReport(const EngineUpdatePlan& plan, const EngineTrial& trial,
                                            const std::string& projectName);
