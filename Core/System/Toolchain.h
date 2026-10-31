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

// A package the launcher downloads only when the user asks (Settings → Audio tools, design D7): a Python wheel.
struct AudioPackage {
    std::string name;
    std::string version;
    std::string url;
    std::string sha256; // Of the wheel, lower-case hex
};

struct Toolchain {
    Tool python;
    Tool cmake;
    Tool ninja;
    std::vector<AudioPackage> audio; // Toolchain.json "audio"; empty when this launcher offers none

    // Put before PATH for engine commands: the project's tools find cmake / ninja / python there.
    [[nodiscard]] std::vector<std::filesystem::path> PathFolders() const;
};

inline constexpr const char* ToolchainFile = "Toolchain.json";

[[nodiscard]] Result<Toolchain> LoadToolchain(const std::filesystem::path& toolsFolder);
