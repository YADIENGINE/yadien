#include "Projects/NewProject.h"
#include "Developer/DeveloperGit.h"
#include "Platform/FileSystem.h"
#include "Platform/Text.h"
#include "Projects/ProjectInfo.h"

namespace {
bool IsEmptyFolder(const std::filesystem::path& folder) {
    std::error_code error;
    return std::filesystem::is_directory(folder, error) && std::filesystem::is_empty(folder, error);
}

Status RunEngineNew(const std::filesystem::path& root, const std::string& name, const EngineRunner& runEngine,
                    const OutputLine& onLine) {
    const ProcessResult result = runEngine(root, {"new", "--name", name}, onLine);
    if (!result.Started()) return Status::Failure(result.startError);
    if (result.canceled) return Status::Failure("Canceled");
    if (result.exitCode != 0)
        return Status::Failure("The engine's new command failed (exit code " + std::to_string(result.exitCode) + ")");
    return Status::Success();
}

Status Populate(const NewProjectRequest& request, const std::filesystem::path& root, const EngineRunner& runEngine,
                const OutputLine& onLine) {
    if (const auto* copy = std::get_if<AvailableEngine>(&request.engine)) {
        if (onLine) onLine("Copying engine " + copy->manifest.version.ToString());
        if (Status copied = CopyTree(copy->folder, root / ProjectInfo::EngineFolderName); !copied.Ok()) return copied;
        return RunEngineNew(root, request.name, runEngine, onLine);
    }
    const auto& development = std::get<DevelopmentEngine>(request.engine);
    if (Status initialized = InitDevelopmentRepository(root, development.repository, development.branch, onLine);
        !initialized.Ok())
        return initialized;
    if (Status created = RunEngineNew(root, request.name, runEngine, onLine); !created.Ok()) return created;
    return CommitNewProject(root, request.name, onLine);
}
} // namespace

std::filesystem::path NewProjectFolder(const NewProjectRequest& request) {
    return request.location / Utf8ToPath(request.name);
}

Status CheckNewProjectFolder(const NewProjectRequest& request) {
    if (request.name.empty()) return Status::Failure("Enter a project name");
    // Only what keeps the folder a single, valid Windows name; the engine checks its own naming rules
    if (request.name.find_first_of("<>:\"/\\|?*") != std::string::npos || request.name.back() == '.' ||
        request.name.back() == ' ')
        return Status::Failure("The name cannot be used as a folder name");
    if (request.location.empty()) return Status::Failure("Choose a location");
    const std::filesystem::path folder = NewProjectFolder(request);
    std::error_code error;
    if (std::filesystem::exists(folder, error) && !IsEmptyFolder(folder))
        return Status::Failure(PathToUtf8(folder) + " already exists and is not empty");
    return Status::Success();
}

Result<std::filesystem::path> CreateProject(const NewProjectRequest& request, const EngineRunner& runEngine,
                                            const OutputLine& onLine) {
    if (Status usable = CheckNewProjectFolder(request); !usable.Ok())
        return Result<std::filesystem::path>::Failure(usable.Error());
    const std::filesystem::path root = NewProjectFolder(request);
    std::error_code error;
    const bool existed = std::filesystem::exists(root, error);
    std::filesystem::create_directories(root, error);
    if (error)
        return Result<std::filesystem::path>::Failure("Cannot create " + PathToUtf8(root) + ": " + error.message());

    if (Status populated = Populate(request, root, runEngine, onLine); !populated.Ok()) {
        // Leave the location as it was: the folder goes if this created it, its contents otherwise
        if (existed) {
            for (const auto& entry : std::filesystem::directory_iterator(root, error))
                (void)RemoveTree(entry.path());
        } else {
            (void)RemoveTree(root);
        }
        return Result<std::filesystem::path>::Failure(populated.Error());
    }
    return root;
}
