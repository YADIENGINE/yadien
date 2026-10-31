#include "Settings/LauncherLayout.h"
#include <nlohmann/json.hpp>
#include "Platform/FileSystem.h"
#include "Platform/Text.h"

Result<LauncherLayout> ResolveLauncherLayout(const std::filesystem::path& executableFolder) {
    const std::filesystem::path file = executableFolder / LauncherLayoutFile;
    std::error_code error;
    if (!std::filesystem::exists(file, error))
        return LauncherLayout{executableFolder / "Engines", executableFolder / "Tools", executableFolder};

    Result<nlohmann::json> json = ReadJsonFile(file);
    if (!json.Ok()) return Result<LauncherLayout>::Failure(json.Error());
    const nlohmann::json& data = json.Value();
    for (const char* key : {"engines", "tools", "data"})
        if (!data.is_object() || !data.contains(key) || !data[key].is_string())
            return Result<LauncherLayout>::Failure(PathToUtf8(file) + " needs engines, tools and data folders");
    return LauncherLayout{Utf8ToPath(data["engines"].get<std::string>()), Utf8ToPath(data["tools"].get<std::string>()),
                          Utf8ToPath(data["data"].get<std::string>())};
}
