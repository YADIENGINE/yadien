#include "Platform/FileSystem.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>
#include "Platform/Text.h"

namespace fs = std::filesystem;

namespace {
std::string Describe(const std::string& action, const fs::path& path, const std::error_code& error) {
    return action + " " + PathToUtf8(path) + ": " + error.message();
}
} // namespace

Result<std::string> ReadTextFile(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) return Result<std::string>::Failure("Cannot read " + PathToUtf8(file));
    std::ostringstream content;
    content << stream.rdbuf();
    return content.str();
}

Result<nlohmann::json> ReadJsonFile(const fs::path& file) {
    Result<std::string> text = ReadTextFile(file);
    if (!text.Ok()) return Result<nlohmann::json>::Failure(text.Error());
    nlohmann::json value = nlohmann::json::parse(text.Value(), nullptr, false);
    if (value.is_discarded()) return Result<nlohmann::json>::Failure(PathToUtf8(file) + " is not valid JSON");
    return value;
}

Status WriteFileAtomic(const fs::path& file, const std::string& content) {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    if (error) return Status::Failure(Describe("Cannot create", file.parent_path(), error));
    fs::path staging = file;
    staging += L".writing";
    {
        std::ofstream stream(staging, std::ios::binary | std::ios::trunc);
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!stream) return Status::Failure("Cannot write " + PathToUtf8(staging));
    }
    if (!MoveFileExW(staging.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const std::string reason = LastWindowsError();
        fs::remove(staging, error);
        return Status::Failure("Cannot replace " + PathToUtf8(file) + ": " + reason);
    }
    return Status::Success();
}

Status WriteJsonFileAtomic(const fs::path& file, const nlohmann::json& value) {
    return WriteFileAtomic(file, value.dump(2) + "\n");
}

Result<std::vector<std::string>> ListFiles(const fs::path& root, const std::vector<std::string>& skipFolders) {
    std::vector<std::string> files;
    std::error_code error;
    fs::recursive_directory_iterator walk(root, error);
    if (error) return Result<std::vector<std::string>>::Failure(Describe("Cannot list", root, error));
    for (; walk != fs::recursive_directory_iterator(); walk.increment(error)) {
        if (error) return Result<std::vector<std::string>>::Failure(Describe("Cannot list", root, error));
        const fs::directory_entry& entry = *walk;
        if (entry.is_directory()) {
            if (std::ranges::find(skipFolders, PathToUtf8(entry.path().filename())) != skipFolders.end())
                walk.disable_recursion_pending();
            continue;
        }
        if (entry.is_regular_file()) files.push_back(RelativeKey(entry.path(), root));
    }
    std::ranges::sort(files);
    return files;
}

Status CopyTree(const fs::path& source, const fs::path& destination) {
    std::error_code error;
    if (fs::exists(destination, error)) return Status::Failure(PathToUtf8(destination) + " already exists");
    fs::create_directories(destination.parent_path(), error);
    fs::copy(source, destination, fs::copy_options::recursive, error);
    if (error) return Status::Failure(Describe("Cannot copy to", destination, error));
    return Status::Success();
}

Status RemoveTree(const fs::path& folder) {
    std::error_code error;
    fs::remove_all(folder, error);
    if (error) return Status::Failure(Describe("Cannot delete", folder, error));
    return Status::Success();
}

Status MovePath(const fs::path& from, const fs::path& to) {
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        return Status::Failure("Cannot move " + PathToUtf8(from) + " to " + PathToUtf8(to) + ": " + LastWindowsError());
    return Status::Success();
}
