#pragma once

#include "Cli/EngineCommand.h"
#include "Engines/EngineLibrary.h"
#include "Platform/Result.h"
#include <filesystem>
#include <string>
#include <variant>

// "dev (main)" in developer mode: the engine repository as a submodule, following a branch (design D16).
struct DevelopmentEngine {
    std::string repository; // From the newest engine manifest's "repository"
    std::string branch = "main";
};

struct NewProjectRequest {
    std::string name;                                        // See ProjectNameProblem
    std::filesystem::path location;                          // The project is created at location / name
    std::variant<AvailableEngine, DevelopmentEngine> engine; // A stable copy (user mode) or the development engine
};

// Why the engine's `new --name` would refuse `name` (a letter, then letters, digits or '_'; at most 64), as one short
// sentence; empty when it is accepted. The rule is the engine's Tools contract, so the form can say it while typing.
[[nodiscard]] std::string ProjectNameProblem(const std::string& name);
// The folder a request creates, and why it cannot be used (a bad name, no location, a folder that exists and is not
// empty); for live UI validation.
[[nodiscard]] std::filesystem::path NewProjectFolder(const NewProjectRequest& request);
[[nodiscard]] Status CheckNewProjectFolder(const NewProjectRequest& request);

// Copy: copies the engine into <folder>/Engine, then runs the engine's `new --name`. No git (D14). An engine this
// launcher cannot drive is refused.
// Development: git init + Engine submodule, the engine's `new`, then a first commit.
// On failure everything the operation created is removed. Returns the project root; the caller adds it to the list.
[[nodiscard]] Result<std::filesystem::path> CreateProject(const NewProjectRequest& request,
                                                          const EngineRunner& runEngine, const OutputLine& onLine);
