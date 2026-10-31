#pragma once

#include "Platform/Result.h"
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Windows CNG (BCrypt): no extra dependency. Hex is lower case.
[[nodiscard]] std::string Sha256Hex(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<std::string> Sha256FileHex(const std::filesystem::path& file);
[[nodiscard]] std::vector<std::uint8_t> Pbkdf2Sha256(std::string_view password, std::span<const std::uint8_t> salt,
                                                     std::uint64_t iterations, size_t length);
[[nodiscard]] std::string ToHex(std::span<const std::uint8_t> bytes);
// False on malformed text (odd length, non-hex digit).
[[nodiscard]] bool FromHex(std::string_view text, std::vector<std::uint8_t>& bytes);
// Comparison whose duration does not depend on where the inputs differ.
[[nodiscard]] bool EqualConstantTime(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b);
