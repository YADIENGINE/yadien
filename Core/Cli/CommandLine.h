#pragma once

#include "Settings/LauncherLayout.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// `<engine>.com` (design D6): `<engine> launcher <job>` runs the launcher's own jobs; every other argument list goes
// unchanged to the project's engine (`<engine> build --release` → `python Engine build --release`). The engine never
// adds a command named "launcher". Exit codes follow the engine's: 0 success, 1 the job failed, 2 usage / setup.

struct ParsedCommandLine {
    std::optional<std::filesystem::path> project; // --project ROOT / --project=ROOT before the command
    std::vector<std::string> arguments;           // The rest, unchanged
    std::string error;
};

[[nodiscard]] ParsedCommandLine ParseCommandLine(const std::vector<std::string>& arguments);

// The project a command in `start` means: the first folder from `start` upwards that holds a Project.json, or an
// Engine/ with a manifest (a folder about to run the engine's `new`).
[[nodiscard]] std::optional<std::filesystem::path> FindProjectRoot(const std::filesystem::path& start);

// Runs a whole command line; output goes to stdout / stderr.
[[nodiscard]] int RunCommandLine(const std::vector<std::string>& arguments, const LauncherLayout& layout,
                                 const std::filesystem::path& currentFolder);
