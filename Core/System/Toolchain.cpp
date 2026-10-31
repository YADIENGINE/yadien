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
    const nlohmann::json audio =
        json.Value().is_object() ? json.Value().value("audio", nlohmann::json::object()) : nlohmann::json::object();
    if (!audio.is_object()) return Result<Toolchain>::Failure(PathToUtf8(file) + ": audio must be an object");
    for (const auto& [name, entry] : audio.items()) {
        if (!entry.is_object())
            return Result<Toolchain>::Failure(PathToUtf8(file) + ": audio." + name + " must be an object");
        AudioPackage package{name, entry.value("version", ""), entry.value("url", ""), entry.value("sha256", "")};
        if (package.version.empty() || package.url.empty() || package.sha256.empty())
            return Result<Toolchain>::Failure(PathToUtf8(file) + ": audio." + name + " needs version, url and sha256");
        toolchain.audio.push_back(std::move(package));
    }
    return toolchain;
}
