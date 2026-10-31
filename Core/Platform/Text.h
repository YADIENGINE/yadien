#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// The core speaks UTF-8 (std::string); Windows APIs take UTF-16. Paths cross with PathToUtf8 / Utf8ToPath.
[[nodiscard]] std::wstring Utf8ToWide(std::string_view text);
[[nodiscard]] std::string WideToUtf8(std::wstring_view text);
[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path Utf8ToPath(std::string_view text);
// "Engine/Core/X.h" style: relative to `base`, '/' separators (EngineCopy.json keys).
[[nodiscard]] std::string RelativeKey(const std::filesystem::path& path, const std::filesystem::path& base);
[[nodiscard]] std::string ToLowerAscii(std::string_view text);
// GetLastError() as text ("Access is denied"), for error messages.
[[nodiscard]] std::string LastWindowsError();
