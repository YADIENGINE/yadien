#include "Engines/EngineManifest.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <system_error>
#include "Platform/FileSystem.h"
#include "Platform/Text.h"

namespace {
std::vector<std::string> Strings(const nlohmann::json& value) {
    std::vector<std::string> texts;
    if (!value.is_array()) return texts;
    for (const nlohmann::json& item : value)
        if (item.is_string()) texts.push_back(item.get<std::string>());
    return texts;
}

std::string Text(const nlohmann::json& note, const char* key) {
    const auto found = note.find(key);
    return found != note.end() && found->is_string() ? found->get<std::string>() : std::string();
}

EngineNote ReadNote(const nlohmann::json& note, const EngineVersion& version) {
    EngineNote entry{version,
                     Text(note, "name"),
                     Text(note, "summary"),
                     Text(note, "date"),
                     Strings(note.value("changes", nlohmann::json())),
                     {}};
    for (const nlohmann::json& breaking : note.value("breaking", nlohmann::json::array()))
        if (breaking.is_object())
            entry.breaking.push_back({breaking.value("change", ""), breaking.value("migrate", "")});
    return entry;
}

std::vector<EngineNote> ReadManifestNotes(const nlohmann::json& notes) {
    std::vector<EngineNote> result;
    if (!notes.is_array()) return result;
    for (const nlohmann::json& note : notes) {
        if (!note.is_object()) continue;
        const std::optional<EngineVersion> version = EngineVersion::Parse(note.value("version", ""));
        if (version) result.push_back(ReadNote(note, *version));
    }
    return result;
}

std::vector<EngineNote> ReadNoteFiles(const std::filesystem::path& folder) {
    std::vector<EngineNote> result;
    std::error_code error;
    std::filesystem::directory_iterator entry(folder, error);
    const std::filesystem::directory_iterator end;
    for (; !error && entry != end; entry.increment(error)) {
        const std::filesystem::path& file = entry->path();
        const std::optional<EngineVersion> version = EngineVersion::Parse(PathToUtf8(file.stem()));
        if (file.extension() != ".json" || !version) continue;
        Result<nlohmann::json> note = ReadJsonFile(file);
        if (note.Ok() && note.Value().is_object()) result.push_back(ReadNote(note.Value(), *version));
    }
    std::ranges::sort(result, [](const EngineNote& a, const EngineNote& b) { return b.version < a.version; });
    return result;
}
} // namespace

Result<EngineManifest> LoadEngineManifest(const std::filesystem::path& engineFolder) {
    const std::filesystem::path file = engineFolder / EngineManifestFile;
    Result<nlohmann::json> json = ReadJsonFile(file);
    if (!json.Ok()) return Result<EngineManifest>::Failure(json.Error());
    const nlohmann::json& data = json.Value();
    if (!data.is_object()) return Result<EngineManifest>::Failure(PathToUtf8(file) + " is not a JSON object");

    EngineManifest manifest;
    manifest.name = data.value("name", "");
    const std::optional<EngineVersion> version = EngineVersion::Parse(data.value("version", ""));
    const nlohmann::json contract = data.value("toolsContract", nlohmann::json());
    if (manifest.name.empty() || !version || !contract.is_number_integer())
        return Result<EngineManifest>::Failure(PathToUtf8(file) + " needs name, version and toolsContract");
    manifest.version = *version;
    manifest.toolsContract = contract.get<int>();
    manifest.repository = data.value("repository", "");
    const std::filesystem::path notesFolder = engineFolder / EngineNotesFolder;
    std::error_code error;
    manifest.notes = std::filesystem::is_directory(notesFolder, error)
                         ? ReadNoteFiles(notesFolder)
                         : ReadManifestNotes(data.value("notes", nlohmann::json()));
    return manifest;
}

std::string EngineNoteTitle(const EngineNote& note) {
    return note.version.ToString() + " " + (note.name.empty() ? "Pre-Release" : note.name);
}

Result<std::vector<EngineNote>> ParseEngineNotes(const std::string& json) {
    const nlohmann::json data = nlohmann::json::parse(json, nullptr, false);
    if (!data.is_array()) return Result<std::vector<EngineNote>>::Failure("Unexpected release notes");
    std::vector<EngineNote> notes;
    try {
        notes = ReadManifestNotes(data);
    } catch (const nlohmann::json::exception& error) { // A field of the wrong type
        return Result<std::vector<EngineNote>>::Failure(std::string("Unexpected release notes: ") + error.what());
    }
    std::ranges::sort(notes, [](const EngineNote& a, const EngineNote& b) { return b.version < a.version; });
    return notes;
}

std::vector<EngineNote> NotesSince(const EngineManifest& manifest, const EngineVersion& from) {
    std::vector<EngineNote> notes;
    for (const EngineNote& note : manifest.notes)
        if (note.version > from && note.version <= manifest.version) notes.push_back(note);
    return notes;
}
