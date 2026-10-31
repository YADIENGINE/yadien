#pragma once

#include "Platform/Result.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The developer-mode password check (design D4): DeveloperAccess.json holds a PBKDF2-SHA256 hash, never the
// password; Scripts/set_developer_password.py writes it and the build embeds it. A door against opening the mode
// by accident, not security: developer work needs access to the engine repository anyway.
struct DeveloperAccessRecord {
    std::uint64_t iterations = 0;
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> hash;
};

[[nodiscard]] Result<DeveloperAccessRecord> ParseDeveloperAccess(std::string_view json);
// The record this launcher was built with; nullopt when the repository had no DeveloperAccess.json (the mode
// cannot be unlocked).
[[nodiscard]] std::optional<DeveloperAccessRecord> EmbeddedDeveloperAccess();
[[nodiscard]] bool CheckDeveloperPassword(const DeveloperAccessRecord& record, std::string_view password);
