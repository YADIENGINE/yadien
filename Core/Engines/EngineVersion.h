#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

// MAJOR.MINOR.PATCH (engine Docs/ToolsContract.md → Manifest). A series is MAJOR.MINOR; Engines/ keeps one folder per
// series ("Engine0.2") holding its newest patch.
struct EngineVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;

    [[nodiscard]] static std::optional<EngineVersion> Parse(std::string_view text);
    [[nodiscard]] std::string ToString() const;
    [[nodiscard]] std::string Series() const;
    [[nodiscard]] bool SameSeries(const EngineVersion& other) const;

    auto operator<=>(const EngineVersion&) const = default;
};

// Whether moving from `from` to `to` may need changes in the project: before 1.0 a MINOR step, from 1.0 a MAJOR step.
[[nodiscard]] bool MayBreak(const EngineVersion& from, const EngineVersion& to);
