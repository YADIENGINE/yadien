#include "Projects/ProjectInfo.h"
#include <nlohmann/json.hpp>
#include "Engines/EngineCopyRecord.h"
#include "Platform/FileSystem.h"
#include "Platform/Text.h"

ProjectInfo InspectProject(const std::filesystem::path& root) {
    ProjectInfo info;
    info.root = root;
    info.name = PathToUtf8(root.filename());
    std::error_code error;

    Result<nlohmann::json> project = ReadJsonFile(root / ProjectFile);
    if (!project.Ok()) {
        info.problem = project.Error();
    } else if (project.Value().is_object()) {
        info.name = project.Value().value("name", info.name);
    }

    const std::filesystem::path engine = info.EngineFolder();
    Result<EngineManifest> manifest = LoadEngineManifest(engine);
    if (!manifest.Ok()) {
        info.kind = EngineKind::Missing;
        if (info.problem.empty()) info.problem = manifest.Error();
        return info;
    }
    info.manifest = std::move(manifest).Value();
    if (std::filesystem::exists(engine / EngineCopyFile, error))
        info.kind = EngineKind::Copy;
    else if (std::filesystem::exists(engine / ".git", error))
        info.kind = EngineKind::Git; // A submodule's .git is a file
    else
        info.kind = EngineKind::Unregistered;
    return info;
}
