#pragma once

#include "Platform/Result.h"
#include <filesystem>

// Opens a folder in Explorer ("Show in folder", "Open install folder"). Returns at once.
[[nodiscard]] Status OpenFolder(const std::filesystem::path& folder);
