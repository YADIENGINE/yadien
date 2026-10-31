#pragma once

#include "Platform/Process.h"
#include "Platform/Result.h"
#include "Projects/ProjectInfo.h"
#include "System/Toolchain.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Running the project's own engine tools (design D6, D7): the launcher's Python runs <project>/Engine with the
// arguments unchanged; the launcher has no build logic and does not know the engine's commands.

// The tools contracts (engine Docs/ToolsContract.md) this launcher can drive.
[[nodiscard]] bool SupportsToolsContract(int contract);

// `python -X utf8 <root>/Engine --project <root> <arguments...>` with the toolchain first on PATH and, when the
// windowed launcher exists beside this executable, ENGINE_LAUNCHER = its path (the editor's "Back to project list";
// engine Docs/ToolsContract.md). Fails when the project has no usable engine or speaks a contract this launcher does
// not know (update the launcher).
[[nodiscard]] Result<ProcessCommand> EngineCommand(const Toolchain& toolchain, const ProjectInfo& project,
                                                   const std::vector<std::string>& arguments);

inline constexpr const char* LauncherVariable = "ENGINE_LAUNCHER";

// <engine>.exe beside the running executable (the windowed launcher); empty when it is not there (CLI-only build).
[[nodiscard]] std::filesystem::path WindowedLauncherExecutable();

// Runs an engine command for the project at `root` (its Engine/ in place, Project.json not required) and streams
// the output. Injected into operations such as NewProject so tests can run without Python.
using EngineRunner = std::function<ProcessResult(const std::filesystem::path& root,
                                                 const std::vector<std::string>& arguments, const OutputLine& onLine)>;

// The real runner: EngineCommand + RunCaptured. `cancel` may be null.
[[nodiscard]] EngineRunner LauncherEngineRunner(Toolchain toolchain, const std::atomic<bool>* cancel = nullptr);
