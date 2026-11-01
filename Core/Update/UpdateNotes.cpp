#include "Update/UpdateNotes.h"
#include <array>
#include <cstdio>

namespace {
constexpr std::string_view Whitespace = " \t\r";
constexpr std::string_view BreakingPrefix = "Breaking:";
constexpr std::array<std::string_view, 2> ItemPrefixes = {"- ", "* "};
constexpr std::uint64_t BytesPerKilobyte = 1024;
constexpr std::uint64_t BytesPerMegabyte = BytesPerKilobyte * 1024;

std::string_view Trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(Whitespace);
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(Whitespace) - first + 1);
}

UpdateNote ParseLine(std::string_view line) {
    for (const std::string_view prefix : ItemPrefixes) {
        if (!line.starts_with(prefix)) continue;
        const std::string_view item = Trim(line.substr(prefix.size()));
        if (item.starts_with(BreakingPrefix))
            return {UpdateNoteKind::Breaking, std::string(Trim(item.substr(BreakingPrefix.size())))};
        return {UpdateNoteKind::Item, std::string(item)};
    }
    if (line.starts_with('#'))
        return {UpdateNoteKind::Heading, std::string(Trim(line.substr(line.find_first_not_of('#'))))};
    if (line.ends_with(':')) return {UpdateNoteKind::Heading, std::string(line.substr(0, line.size() - 1))};
    return {UpdateNoteKind::Paragraph, std::string(line)};
}
} // namespace

std::vector<UpdateNote> ParseUpdateNotes(std::string_view markdown) {
    std::vector<UpdateNote> notes;
    while (!markdown.empty()) {
        const std::size_t end = markdown.find('\n');
        const std::string_view line = Trim(markdown.substr(0, end));
        markdown.remove_prefix(end == std::string_view::npos ? markdown.size() : end + 1);
        if (!line.empty()) notes.push_back(ParseLine(line));
    }
    return notes;
}

std::string FormatDownloadSize(std::uint64_t bytes) {
    std::array<char, 32> text{};
    if (bytes < BytesPerMegabyte)
        std::snprintf(text.data(), text.size(), "%llu KB",
                      static_cast<unsigned long long>((bytes + BytesPerKilobyte - 1) / BytesPerKilobyte));
    else
        std::snprintf(text.data(), text.size(), "%.1f MB",
                      static_cast<double>(bytes) / static_cast<double>(BytesPerMegabyte));
    return text.data();
}
