#include "Engines/EngineVersion.h"
#include <array>
#include <charconv>

std::optional<EngineVersion> EngineVersion::Parse(std::string_view text) {
    std::array<int, 3> parts{};
    size_t start = 0;
    for (size_t index = 0; index < parts.size(); ++index) {
        const size_t end = index + 1 < parts.size() ? text.find('.', start) : text.size();
        if (end == std::string_view::npos) return std::nullopt;
        const std::string_view digits = text.substr(start, end - start);
        if (digits.empty() || (digits.size() > 1 && digits.front() == '0')) return std::nullopt;
        const auto [last, error] = std::from_chars(digits.data(), digits.data() + digits.size(), parts[index]);
        if (error != std::errc() || last != digits.data() + digits.size()) return std::nullopt;
        start = end + 1;
    }
    return EngineVersion{parts[0], parts[1], parts[2]};
}

std::string EngineVersion::ToString() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::string EngineVersion::Series() const { return std::to_string(major) + "." + std::to_string(minor); }

bool EngineVersion::SameSeries(const EngineVersion& other) const {
    return major == other.major && minor == other.minor;
}

bool MayBreak(const EngineVersion& from, const EngineVersion& to) {
    if (to <= from) return false;
    if (from.major != to.major) return true;
    return from.major == 0 && from.minor != to.minor;
}
