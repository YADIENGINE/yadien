#include "System/Toolchain.h"
#include <nlohmann/json.hpp>
#include "Platform/FileSystem.h"
#include "Platform/Text.h"

std::vector<std::filesystem::path> Toolchain::PathFolders() const {
    return {python.executable.parent_path(), cmake.executable.parent_path(), ninja.executable.parent_path()};
}

Result<Toolchain> LoadToolchain(const std::filesystem::path& toolsFolder) {
    const std::filesystem::path file = toolsFolder / ToolchainFile;
    Result<nlohmann::json> json = ReadJsonFile(file);
    if (!json.Ok()) return Result<Toolchain>::Failure(json.Error());
    const nlohmann::json tools =
        json.Value().is_object() ? json.Value().value("tools", nlohmann::json()) : nlohmann::json();

    Toolchain toolchain;
    for (auto [name, tool] : {std::pair{"python", &toolchain.python}, std::pair{"cmake", &toolchain.cmake},
                              std::pair{"ninja", &toolchain.ninja}}) {
        if (!tools.is_object() || !tools.contains(name) || !tools[name].is_object())
            return Result<Toolchain>::Failure(PathToUtf8(file) + " has no " + name);
        const nlohmann::json& entry = tools[name];
        tool->version = entry.value("version", "");
        tool->executable = (toolsFolder / Utf8ToPath(entry.value("executable", ""))).lexically_normal();
        if (tool->version.empty() || !entry.contains("executable"))
            return Result<Toolchain>::Failure(PathToUtf8(file) + ": " + name + " needs version and executable");
    }
    return toolchain;
}
