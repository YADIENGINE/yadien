#include "Settings/LauncherLayout.h"
#include <nlohmann/json.hpp>
#include "Platform/FileSystem.h"
#include "Platform/Text.h"

namespace {
// Velopack puts the application in <root>/current/ beside <root>/Update.exe.
bool IsVelopackApplicationFolder(const std::filesystem::path& folder) {
    std::error_code error;
    return folder.filename() == "current" &&
           std::filesystem::is_regular_file(folder.parent_path() / "Update.exe", error);
}
} // namespace

Result<LauncherLayout> ResolveLauncherLayout(const std::filesystem::path& executableFolder) {
    const std::filesystem::path file = executableFolder / LauncherLayoutFile;
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        if (!IsVelopackApplicationFolder(executableFolder))
            return LauncherLayout{executableFolder / "Engines", executableFolder / "Tools", executableFolder};
        std::filesystem::path data = executableFolder.parent_path();
        data += L" Data";
        return LauncherLayout{executableFolder / "Engines", executableFolder / "Tools", data, true};
    }

    Result<nlohmann::json> json = ReadJsonFile(file);
    if (!json.Ok()) return Result<LauncherLayout>::Failure(json.Error());
    const nlohmann::json& data = json.Value();
    for (const char* key : {"engines", "tools", "data"})
        if (!data.is_object() || !data.contains(key) || !data[key].is_string())
            return Result<LauncherLayout>::Failure(PathToUtf8(file) + " needs engines, tools and data folders");
    return LauncherLayout{Utf8ToPath(data["engines"].get<std::string>()), Utf8ToPath(data["tools"].get<std::string>()),
                          Utf8ToPath(data["data"].get<std::string>())};
}
