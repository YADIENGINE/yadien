#pragma once

#include "Platform/Result.h"
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

// Windows CNG (BCrypt): no extra dependency. Hex is lower case.
[[nodiscard]] std::string Sha256Hex(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<std::string> Sha256FileHex(const std::filesystem::path& file);
