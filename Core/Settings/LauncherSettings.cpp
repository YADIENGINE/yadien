#include "Settings/LauncherSettings.h"
#include <nlohmann/json.hpp>
#include <array>
#include "Platform/FileSystem.h"
#include "Platform/KnownFolders.h"
#include "Platform/Text.h"

namespace {
constexpr std::array<const char*, 2> ModeNames = {"user", "developer"};
} // namespace

Result<LauncherSettings> LoadLauncherSettings(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) return LauncherSettings{};
    Result<nlohmann::json> json = ReadJsonFile(file);
    if (!json.Ok()) return Result<LauncherSettings>::Failure(json.Error());
    const nlohmann::json& data = json.Value();
    if (!data.is_object() || data.value("format", 0) != LauncherSettingsFormat)
        return Result<LauncherSettings>::Failure(PathToUtf8(file) + ": unsupported format");

    LauncherSettings settings;
    settings.mode = data.value("mode", "user") == ModeNames[1] ? LauncherMode::Developer : LauncherMode::User;
    settings.projectsFolder = Utf8ToPath(data.value("projectsFolder", ""));
    settings.uiScale = data.value("uiScale", 1.0f);
    for (const nlohmann::json& project : data.value("projects", nlohmann::json::array())) {
        if (!project.is_object() || !project.contains("root") || !project["root"].is_string()) continue;
        settings.projects.push_back({Utf8ToPath(project["root"].get<std::string>()), project.value("lastOpened", "")});
    }
    return settings;
}

Status SaveLauncherSettings(const std::filesystem::path& file, const LauncherSettings& settings) {
    nlohmann::json projects = nlohmann::json::array();
    for (const ProjectEntry& project : settings.projects)
        projects.push_back({{"root", PathToUtf8(project.root)}, {"lastOpened", project.lastOpened}});
    const nlohmann::json data = {
        {"format", LauncherSettingsFormat},
        {"mode", ModeNames[settings.mode == LauncherMode::Developer ? 1 : 0]},
        {"projectsFolder", PathToUtf8(settings.projectsFolder)},
        {"uiScale", settings.uiScale},
        {"projects", projects},
    };
    return WriteJsonFileAtomic(file, data);
}

std::filesystem::path DefaultProjectsFolder() {
    return DocumentsFolder() / Utf8ToPath(std::string(LAUNCHER_PRODUCT_NAME) + " Projects");
}

std::filesystem::path ProjectsFolderOf(const LauncherSettings& settings) {
    return settings.projectsFolder.empty() ? DefaultProjectsFolder() : settings.projectsFolder;
}
