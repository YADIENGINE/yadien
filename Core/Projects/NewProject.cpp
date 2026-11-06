#include "Projects/NewProject.h"
#include <algorithm>
#include <string>
#include "Developer/DeveloperGit.h"
#include "Platform/FileSystem.h"
#include "Platform/Text.h"
#include "Projects/ProjectInfo.h"

namespace {
constexpr std::size_t MaxProjectNameLength = 64;

bool IsAsciiLetter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

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

std::string ProjectNameProblem(const std::string& name) {
    // The engine's `new --name` rule (engine Docs/ToolsContract.md): checked here so the form says it while typing
    if (name.empty()) return "Enter a project name";
    if (!IsAsciiLetter(name.front())) return "Start the name with a letter (A-Z)";
    if (name.find(' ') != std::string::npos) return "No spaces: write MyGame or My_Game";
    if (!std::ranges::all_of(name, [](char c) { return IsAsciiLetter(c) || (c >= '0' && c <= '9') || c == '_'; }))
        return "Use only letters A-Z, digits and _";
    if (name.size() > MaxProjectNameLength) return "At most " + std::to_string(MaxProjectNameLength) + " characters";
    return {};
}

Status CheckNewProjectFolder(const NewProjectRequest& request) {
    if (std::string problem = ProjectNameProblem(request.name); !problem.empty())
        return Status::Failure(std::move(problem));
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
    if (const auto* copy = std::get_if<AvailableEngine>(&request.engine);
        copy && !SupportsToolsContract(copy->manifest.toolsContract))
        return Result<std::filesystem::path>::Failure("Engine " + copy->manifest.version.ToString() +
                                                      " needs a newer launcher; update the launcher first");
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
