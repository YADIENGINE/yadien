#include "Engines/EngineManifest.h"
#include <nlohmann/json.hpp>
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

std::vector<EngineNote> ReadNotes(const nlohmann::json& notes) {
    std::vector<EngineNote> result;
    if (!notes.is_array()) return result;
    for (const nlohmann::json& note : notes) {
        if (!note.is_object()) continue;
        const std::optional<EngineVersion> version = EngineVersion::Parse(note.value("version", ""));
        if (!version) continue;
        EngineNote entry{*version, note.value("date", ""), Strings(note.value("changes", nlohmann::json())), {}};
        for (const nlohmann::json& breaking : note.value("breaking", nlohmann::json::array()))
            if (breaking.is_object())
                entry.breaking.push_back({breaking.value("change", ""), breaking.value("migrate", "")});
        result.push_back(std::move(entry));
    }
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
    manifest.notes = ReadNotes(data.value("notes", nlohmann::json()));
    return manifest;
}

std::vector<EngineNote> NotesSince(const EngineManifest& manifest, const EngineVersion& from) {
    std::vector<EngineNote> notes;
    for (const EngineNote& note : manifest.notes)
        if (note.version > from && note.version <= manifest.version) notes.push_back(note);
    return notes;
}
