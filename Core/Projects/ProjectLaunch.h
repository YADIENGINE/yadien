#pragma once

#include "Cli/EngineCommand.h"
#include "Jobs/BackgroundJob.h"
#include "Platform/Process.h"
#include "Platform/Result.h"
#include "Projects/ProjectInfo.h"
#include "System/Toolchain.h"
#include <functional>
#include <string>

// Starts a program and returns at once; an error message, empty on success. StartDetached in the launcher; tests
// record the command instead.
using ProcessStarter = std::function<std::string(const ProcessCommand&)>;

// "Open" (design D15) as one job body, OpenProjectSteps steps:
// 1. "Checking engine files": a copy's read-only check; changed files are reported as a warning line, the opening
//    goes on (the Projects row offers Repair).
// 2. "Building editor": the engine's `editor --build-only`, output streamed.
// 3. "Starting editor": the engine's `editor` started detached (already built, so it opens at once).
// The caller records the opening (MarkProjectOpened + save) on success and closes the launcher.
inline constexpr int OpenProjectSteps = 3;
[[nodiscard]] Status OpenProject(const ProjectInfo& project, const Toolchain& toolchain, const EngineRunner& runEngine,
                                 const ProcessStarter& start, JobContext& job);

// "Run": the engine's `run` (builds, starts the game and waits until it closes), RunProjectSteps steps. Cancel stops
// the build or closes the game.
inline constexpr int RunProjectSteps = 1;
[[nodiscard]] Status RunProject(const ProjectInfo& project, const EngineRunner& runEngine, JobContext& job);
