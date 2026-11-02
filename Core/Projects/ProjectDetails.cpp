#include "Projects/ProjectDetails.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include "Engines/EngineCopyRecord.h"
#include "Platform/FileSystem.h"
#include "Projects/EngineUpdate.h"

namespace {
// Engine Docs/ToolsContract.md, Project status files. Older engines write none of them.
constexpr const char* LastBuildFile = "LastBuild.json";
constexpr const char* LastFrameFile = "LastFrame.png";

std::optional<ProjectLastBuild> ReadLastBuild(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) return std::nullopt;
    const Result<nlohmann::json> json = ReadJsonFile(file);
    if (!json.Ok() || !json.Value().is_object()) return std::nullopt;
    const auto ok = json.Value().find("ok");
    const auto finished = json.Value().find("finished");
    if (ok == json.Value().end() || !ok->is_boolean() || finished == json.Value().end() || !finished->is_string())
        return std::nullopt;
    return ProjectLastBuild{ok->get<bool>(), finished->get<std::string>()};
}

// Several build folders can share one path; each counts once. The newest build wins (ISO 8601 sorts by time).
void ReadBuildFolders(ProjectDetails& entry) {
    std::vector<std::filesystem::path> folders = ProjectBuildFolders(entry.root);
    std::ranges::sort(folders);
    folders.erase(std::ranges::unique(folders).begin(), folders.end());
    for (const std::filesystem::path& relative : folders) {
        const std::filesystem::path folder = entry.root / relative;
        std::error_code error;
        if (!std::filesystem::is_directory(folder, error)) continue;
        if (const Result<std::uintmax_t> size = FolderSize(folder); size.Ok()) entry.buildBytes += size.Value();
        if (auto build = ReadLastBuild(folder / LastBuildFile);
            build && (!entry.lastBuild || build->finished > entry.lastBuild->finished))
            entry.lastBuild = std::move(build);
        if (std::filesystem::is_regular_file(folder / LastFrameFile, error)) entry.lastFrame = folder / LastFrameFile;
    }
}
} // namespace

std::vector<ProjectDetails> InspectProjectDetails(const std::vector<ProjectRow>& rows, LauncherMode mode,
                                                  const std::atomic<bool>* cancel) {
    std::vector<ProjectDetails> details;
    for (const ProjectRow& row : rows) {
        if (cancel && *cancel) break;
        if (row.folderMissing) continue;
        ProjectDetails entry{row.entry.root};
        if (row.info.kind == EngineKind::Copy) {
            Result<EngineCopyRecord> record = LoadEngineCopyRecord(row.info.EngineFolder());
            if (!record.Ok()) {
                entry.error = record.Error();
            } else if (Result<CopyIntegrity> integrity = CheckCopyIntegrity(row.info.EngineFolder(), record.Value());
                       integrity.Ok()) {
                entry.integrity = std::move(integrity).Value();
            } else {
                entry.error = integrity.Error();
            }
        } else if (row.info.kind == EngineKind::Git && mode == LauncherMode::Developer) {
            if (Result<EngineGitStatus> status = ReadEngineGitStatus(row.entry.root); status.Ok())
                entry.git = std::move(status).Value();
            else
                entry.error = status.Error();
        }
        ReadBuildFolders(entry);
        details.push_back(std::move(entry));
    }
    return details;
}

Status CleanProjectBuild(const std::filesystem::path& root) {
    for (const std::filesystem::path& relative : ProjectBuildFolders(root)) {
        std::error_code error;
        if (!std::filesystem::exists(root / relative, error)) continue;
        if (Status removed = RemoveTree(root / relative); !removed.Ok()) return removed;
    }
    return Status::Success();
}
