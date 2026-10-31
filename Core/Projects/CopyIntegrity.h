#pragma once

#include "Engines/EngineCopyRecord.h"
#include "Platform/Result.h"
#include <filesystem>
#include <string>
#include <vector>

// The read-only rule of a copy (design D4): the project's Engine/ compared with its EngineCopy.json. Paths are
// relative to the engine folder ('/').
struct CopyIntegrity {
    std::vector<std::string> modified; // Content differs from the record
    std::vector<std::string> missing;  // In the record, not on disk
    std::vector<std::string> added;    // On disk, not in the record (generated __pycache__ folders are ignored)

    [[nodiscard]] bool Clean() const { return modified.empty() && missing.empty() && added.empty(); }
};

// Hashes every file (about a second for a full engine): run it on opening / before an update, not per frame.
[[nodiscard]] Result<CopyIntegrity> CheckCopyIntegrity(const std::filesystem::path& engineFolder,
                                                       const EngineCopyRecord& record);
