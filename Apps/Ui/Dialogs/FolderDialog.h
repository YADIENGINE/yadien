#pragma once

#include <filesystem>
#include <optional>
#include <string>

// The Windows folder picker (IFileOpenDialog), modal over the launcher window. nullopt when the user cancels or the
// dialog cannot open. `initial` may be empty or missing.
[[nodiscard]] std::optional<std::filesystem::path> PickFolder(const std::string& title,
                                                              const std::filesystem::path& initial);
