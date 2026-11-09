#include "Projects/ProjectEngineVersion.h"
#include <nlohmann/json.hpp>
#include <cctype>
#include <string>
#include <string_view>
#include "Platform/FileSystem.h"
#include "Projects/ProjectInfo.h"

namespace {
constexpr const char* EngineSection = "engine";
constexpr const char* VersionKey = "version";

// A span of the file's text: [begin, end).
struct Span {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// A minimal reader over text nlohmann already accepted: it only finds where members and values start and end.
class JsonText {
public:
    explicit JsonText(std::string_view text)
        : _text(text) {}

    [[nodiscard]] std::size_t SkipSpace(std::size_t at) const {
        while (at < _text.size() && std::isspace(static_cast<unsigned char>(_text[at])))
            ++at;
        return at;
    }

    // `at` is on the opening quote; the result is past the closing one.
    [[nodiscard]] std::size_t SkipString(std::size_t at) const {
        for (++at; at < _text.size(); ++at) {
            if (_text[at] == '\\')
                ++at;
            else if (_text[at] == '"')
                return at + 1;
        }
        return at;
    }

    [[nodiscard]] std::size_t SkipValue(std::size_t at) const {
        if (at >= _text.size()) return at;
        if (_text[at] == '"') return SkipString(at);
        if (_text[at] != '{' && _text[at] != '[') {
            while (at < _text.size() && _text[at] != ',' && _text[at] != '}' && _text[at] != ']' &&
                   !std::isspace(static_cast<unsigned char>(_text[at])))
                ++at;
            return at;
        }
        int depth = 0;
        while (at < _text.size()) {
            const char c = _text[at];
            if (c == '"') {
                at = SkipString(at);
                continue;
            }
            if (c == '{' || c == '[') ++depth;
            if (c == '}' || c == ']') --depth;
            ++at;
            if (depth == 0) return at;
        }
        return at;
    }

    // The value of `key` among the members of the object opening at `open`.
    [[nodiscard]] std::optional<Span> Member(std::size_t open, std::string_view key) const {
        std::size_t at = SkipSpace(open + 1);
        while (at < _text.size() && _text[at] == '"') {
            const std::size_t nameEnd = SkipString(at);
            const std::string_view name = _text.substr(at + 1, nameEnd - at - 2);
            const std::size_t valueBegin = SkipSpace(SkipSpace(nameEnd) + 1); // Past the ':'
            const std::size_t valueEnd = SkipValue(valueBegin);
            if (name == key) return Span{valueBegin, valueEnd};
            at = SkipSpace(valueEnd);
            if (at < _text.size() && _text[at] == ',') at = SkipSpace(at + 1);
        }
        return std::nullopt;
    }

    // The last character of the object opening at `open` (its '}').
    [[nodiscard]] std::size_t ObjectEnd(std::size_t open) const { return SkipValue(open) - 1; }

private:
    std::string_view _text;
};

std::string Quoted(const EngineVersion& version) { return "\"" + version.ToString() + "\""; }

// The text with the version in place; empty when the text has no object to put it in.
std::string WithVersion(const std::string& text, const EngineVersion& version) {
    const JsonText json(text);
    const std::size_t root = json.SkipSpace(0);
    if (root >= text.size() || text[root] != '{') return {};
    const std::string newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string section = "{ \"" + std::string(VersionKey) + "\": " + Quoted(version) + " }";
    std::string edited = text;
    if (const std::optional<Span> engine = json.Member(root, EngineSection)) {
        if (text[engine->begin] != '{') return edited.replace(engine->begin, engine->end - engine->begin, section);
        if (const std::optional<Span> value = json.Member(engine->begin, VersionKey))
            return edited.replace(value->begin, value->end - value->begin, Quoted(version));
        const bool empty = text[json.SkipSpace(engine->begin + 1)] == '}';
        if (empty) return edited.replace(engine->begin, engine->end - engine->begin, section);
        return edited.insert(engine->begin + 1, " \"" + std::string(VersionKey) + "\": " + Quoted(version) + ",");
    }
    // A new section goes last, on its own line, indented like the file's first member.
    const std::size_t close = json.ObjectEnd(root);
    const std::size_t first = json.SkipSpace(root + 1);
    std::size_t lineStart = text.rfind('\n', first);
    lineStart = lineStart == std::string::npos || lineStart < root ? first : lineStart + 1;
    const std::string indent = first == close ? std::string("  ") : text.substr(lineStart, first - lineStart);
    const std::string member = "\"" + std::string(EngineSection) + "\": " + section;
    if (first == close) return edited.replace(root, close - root + 1, "{" + newline + indent + member + newline + "}");
    std::size_t last = close; // Past the last value, before the whitespace ahead of '}'
    while (last > root && std::isspace(static_cast<unsigned char>(text[last - 1])))
        --last;
    return edited.insert(last, "," + newline + indent + member);
}
} // namespace

std::optional<EngineVersion> RecordedEngineVersion(const nlohmann::json& project) {
    if (!project.is_object()) return std::nullopt;
    const auto engine = project.find(EngineSection);
    if (engine == project.end() || !engine->is_object()) return std::nullopt;
    const auto version = engine->find(VersionKey);
    if (version == engine->end() || !version->is_string()) return std::nullopt;
    return EngineVersion::Parse(version->get<std::string>());
}

Status RecordProjectEngine(const std::filesystem::path& projectRoot, const EngineVersion& version) {
    const std::filesystem::path file = projectRoot / ProjectFile;
    Result<std::string> text = ReadTextFile(file);
    if (!text.Ok()) return Status::Failure(text.Error());
    const nlohmann::json before = nlohmann::json::parse(text.Value(), nullptr, false);
    if (before.is_discarded() || !before.is_object())
        return Status::Failure(file.filename().string() + " is not a JSON object; the engine version was not recorded");
    if (RecordedEngineVersion(before) == version) return Status::Success();
    const std::string edited = WithVersion(text.Value(), version);
    const nlohmann::json after = nlohmann::json::parse(edited, nullptr, false);
    // The edit must leave every other setting as it was; anything else is a bug here, not a reason to break the file
    nlohmann::json expected = before;
    if (!expected[EngineSection].is_object()) expected[EngineSection] = nlohmann::json::object();
    expected[EngineSection][VersionKey] = version.ToString();
    if (after.is_discarded() || after != expected)
        return Status::Failure("Could not record engine " + version.ToString() + " in " + file.filename().string());
    return WriteFileAtomic(file, edited);
}
