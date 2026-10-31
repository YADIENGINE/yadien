#pragma once

#include "Engines/EngineManifest.h"
#include <filesystem>
#include <optional>
#include <string>

// How the project's Engine/ is managed, read from the files (design D4):
// Copy: Engine/EngineCopy.json (user mode copy); Git: Engine/ is a git submodule / checkout (developer project);
// Unregistered: an engine without either (copied by hand, source unknown); Missing: no usable Engine/.
enum class EngineKind { Copy, Git, Unregistered, Missing };

struct ProjectInfo {
    std::filesystem::path root;
    std::string name; // Project.json "name" (folder name when absent)
    EngineKind kind = EngineKind::Missing;
    std::optional<EngineManifest> manifest; // Set unless kind is Missing
    std::string problem; // Why the project cannot be used as is (no Project.json, broken manifest ...); empty if fine

    [[nodiscard]] std::filesystem::path EngineFolder() const { return root / EngineFolderName; }

    static constexpr const char* EngineFolderName = "Engine";
};

inline constexpr const char* ProjectFile = "Project.json";

// Reads only the files (fast enough for every row of the project list); hashes nothing.
[[nodiscard]] ProjectInfo InspectProject(const std::filesystem::path& root);
