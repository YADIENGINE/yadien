#pragma once

#include "Platform/Result.h"
#include <nlohmann/json_fwd.hpp>
#include <filesystem>
#include <string>
#include <vector>

[[nodiscard]] Result<std::string> ReadTextFile(const std::filesystem::path& file);
[[nodiscard]] Result<nlohmann::json> ReadJsonFile(const std::filesystem::path& file);
// Writes beside the file, then replaces it: readers see the old or the new content, never half of it.
[[nodiscard]] Status WriteFileAtomic(const std::filesystem::path& file, const std::string& content);
// Two-space indented JSON with a final newline (LF), the format of every file the launcher writes.
[[nodiscard]] Status WriteJsonFileAtomic(const std::filesystem::path& file, const nlohmann::json& value);

// Every regular file under `root`, as RelativeKey ('/' separated), sorted. Folders named in `skipFolders` (e.g.
// "__pycache__") are not entered.
[[nodiscard]] Result<std::vector<std::string>> ListFiles(const std::filesystem::path& root,
                                                         const std::vector<std::string>& skipFolders = {});
// Copies a folder tree; the destination must not exist.
[[nodiscard]] Status CopyTree(const std::filesystem::path& source, const std::filesystem::path& destination);
[[nodiscard]] Status RemoveTree(const std::filesystem::path& folder);
// Moves / renames within a volume (MoveFileEx); fails instead of replacing an existing destination.
[[nodiscard]] Status MovePath(const std::filesystem::path& from, const std::filesystem::path& to);
