#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

// How times and paths read on screen, apart from any drawing.

// "Just now", "5 minutes ago", "Yesterday", "3 weeks ago"... from an ISO 8601 UTC time ("2026-10-31T18:20:00Z").
// Empty when `isoUtc` cannot be read; a time in the future reads "Just now".
[[nodiscard]] std::string RelativeTimeText(std::string_view isoUtc, std::chrono::system_clock::time_point now);

// The user's home folder; empty when the environment does not name one.
[[nodiscard]] std::filesystem::path UserHomeFolder();
// UTF-8 text of `path`, with `home` shortened to "~" when the path is inside it (case-insensitively, as on Windows).
[[nodiscard]] std::string ShortPathText(const std::filesystem::path& path, const std::filesystem::path& home);
