#pragma once

#include "Platform/Result.h"
#include <filesystem>
#include <map>
#include <string>

// Engine/EngineCopy.json: the record Scripts/import_engine.py writes beside a stable copy (Docs/Engines.md). Its
// presence marks a project's engine as a launcher copy (user mode).
struct EngineCopyRecord {
    std::string tag;                          // "v0.2.0"
    std::string commit;                       // Full engine commit the tag pointed at
    std::map<std::string, std::string> files; // Path relative to the engine folder ('/') → "sha256:<hex>"
};

inline constexpr const char* EngineCopyFile = "EngineCopy.json";
inline constexpr int EngineCopyFormat = 1;

[[nodiscard]] Result<EngineCopyRecord> LoadEngineCopyRecord(const std::filesystem::path& engineFolder);
