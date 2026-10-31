#pragma once

#include "Platform/Result.h"
#include <filesystem>

// Opens a folder in Explorer ("Show in folder", "Open install folder"). Returns at once.
[[nodiscard]] Status OpenFolder(const std::filesystem::path& folder);
// Opens a file with its default program (the license notices). Returns at once.
[[nodiscard]] Status OpenFile(const std::filesystem::path& file);
