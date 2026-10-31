#pragma once

#include "Platform/Result.h"
#include <filesystem>
#include <string>
#include <vector>

// The tools the launcher ships (design D7, D8): <tools>/Toolchain.json, written by Scripts/fetch_toolchain.py.
struct Tool {
    std::string version;
    std::filesystem::path executable; // Absolute
};

struct Toolchain {
    Tool python;
    Tool cmake;
    Tool ninja;

    // Put before PATH for engine commands: the project's tools find cmake / ninja / python there.
    [[nodiscard]] std::vector<std::filesystem::path> PathFolders() const;
};

inline constexpr const char* ToolchainFile = "Toolchain.json";

[[nodiscard]] Result<Toolchain> LoadToolchain(const std::filesystem::path& toolsFolder);
