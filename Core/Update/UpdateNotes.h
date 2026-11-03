#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A release's notes as the update panel draws them. The text is hand-written Markdown: paragraphs, "- " items and
// "Engine 1.2.3:" group titles; "- Breaking: ..." items are shown as warnings. Other Markdown stays plain text.
enum class UpdateNoteKind { Heading, Paragraph, Item, Breaking };

struct UpdateNote {
    UpdateNoteKind kind = UpdateNoteKind::Paragraph;
    std::string text;
};

[[nodiscard]] std::vector<UpdateNote> ParseUpdateNotes(std::string_view markdown);
// "840 KB", "9.0 MB".
[[nodiscard]] std::string FormatDownloadSize(std::uint64_t bytes);
