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
    std::string name;               // The engine's `new --name` checks the rules (letters, digits, '_')
    std::filesystem::path location; // The project is created at location / name
    std::variant<AvailableEngine, DevelopmentEngine> engine; // A stable copy (user mode) or the development engine
};

// The folder a request creates, and why it cannot be used (exists and is not empty); for live UI validation.
[[nodiscard]] std::filesystem::path NewProjectFolder(const NewProjectRequest& request);
[[nodiscard]] Status CheckNewProjectFolder(const NewProjectRequest& request);

// Copy: copies the engine into <folder>/Engine, then runs the engine's `new --name`. No git (D14).
// Development: git init + Engine submodule, the engine's `new`, then a first commit.
// On failure everything the operation created is removed. Returns the project root; the caller adds it to the list.
[[nodiscard]] Result<std::filesystem::path> CreateProject(const NewProjectRequest& request,
                                                          const EngineRunner& runEngine, const OutputLine& onLine);
