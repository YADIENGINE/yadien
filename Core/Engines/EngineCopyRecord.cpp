#include "Engines/EngineCopyRecord.h"
#include <nlohmann/json.hpp>
#include "Platform/FileSystem.h"
#include "Platform/Text.h"

Result<EngineCopyRecord> LoadEngineCopyRecord(const std::filesystem::path& engineFolder) {
    const std::filesystem::path file = engineFolder / EngineCopyFile;
    Result<nlohmann::json> json = ReadJsonFile(file);
    if (!json.Ok()) return Result<EngineCopyRecord>::Failure(json.Error());
    const nlohmann::json& data = json.Value();
    const std::string where = PathToUtf8(file);
    if (!data.is_object() || data.value("format", 0) != EngineCopyFormat)
        return Result<EngineCopyRecord>::Failure(where + ": unsupported format (this launcher reads format " +
                                                 std::to_string(EngineCopyFormat) + ")");
    const nlohmann::json source = data.value("source", nlohmann::json::object());
    const nlohmann::json files = data.value("files", nlohmann::json());
    if (!source.is_object() || !files.is_object())
        return Result<EngineCopyRecord>::Failure(where + " needs source and files");

    EngineCopyRecord record{source.value("tag", ""), source.value("commit", ""), {}};
    for (const auto& [path, hash] : files.items()) {
        if (!hash.is_string()) return Result<EngineCopyRecord>::Failure(where + ": " + path + " has no hash");
        record.files.emplace(path, hash.get<std::string>());
    }
    return record;
}
