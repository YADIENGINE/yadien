#include "Projects/CopyIntegrity.h"
#include "Platform/Crypto.h"
#include "Platform/FileSystem.h"
#include "Platform/Text.h"

namespace {
constexpr std::string_view HashPrefix = "sha256:";
} // namespace

Result<CopyIntegrity> CheckCopyIntegrity(const std::filesystem::path& engineFolder, const EngineCopyRecord& record) {
    Result<std::vector<std::string>> files = ListFiles(engineFolder, {"__pycache__"});
    if (!files.Ok()) return Result<CopyIntegrity>::Failure(files.Error());

    CopyIntegrity integrity;
    for (const std::string& file : files.Value()) {
        if (file == EngineCopyFile) continue;
        const auto expected = record.files.find(file);
        if (expected == record.files.end()) {
            integrity.added.push_back(file);
            continue;
        }
        Result<std::string> hash = Sha256FileHex(engineFolder / Utf8ToPath(file));
        if (!hash.Ok()) return Result<CopyIntegrity>::Failure(hash.Error());
        if (std::string(HashPrefix) + hash.Value() != expected->second) integrity.modified.push_back(file);
    }
    for (const auto& [file, hash] : record.files) {
        std::error_code error;
        if (!std::filesystem::exists(engineFolder / Utf8ToPath(file), error)) integrity.missing.push_back(file);
    }
    return integrity;
}
