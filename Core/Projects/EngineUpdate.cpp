#include "Projects/EngineUpdate.h"
#include "Platform/Crypto.h"
#include "Platform/FileSystem.h"
#include "Platform/Text.h"
#include "Projects/CopyIntegrity.h"

namespace {
constexpr const char* StagingFolder = ".Engine.updating"; // Beside Engine/: same volume, so the swap is a rename
constexpr const char* RetiredFolder = ".Engine.old";
constexpr std::string_view HashPrefix = "sha256:";

// Without a record of its own, every file that is not byte-equal to the target's is reported as overwritten.
Result<std::vector<std::string>> DifferingFromTarget(const std::filesystem::path& engine,
                                                     const std::vector<std::string>& files,
                                                     const EngineCopyRecord& target) {
    std::vector<std::string> differing;
    for (const std::string& file : files) {
        const auto expected = target.files.find(file);
        if (expected == target.files.end()) continue;
        Result<std::string> hash = Sha256FileHex(engine / Utf8ToPath(file));
        if (!hash.Ok()) return Result<std::vector<std::string>>::Failure(hash.Error());
        if (std::string(HashPrefix) + hash.Value() != expected->second) differing.push_back(file);
    }
    return differing;
}
} // namespace

Result<EngineUpdatePlan> PlanEngineUpdate(const ProjectInfo& project, const AvailableEngine& target) {
    using Plan = Result<EngineUpdatePlan>;
    std::error_code error;
    if (!std::filesystem::is_regular_file(project.root / ProjectFile, error))
        return Plan::Failure(PathToUtf8(project.root) + " has no " + ProjectFile);
    if (project.kind == EngineKind::Git)
        return Plan::Failure("This project's engine is managed by git; update it in developer mode");

    EngineUpdatePlan plan{project.root, std::nullopt, target, {}, {}, {}, true};
    const std::filesystem::path engine = project.EngineFolder();
    std::vector<std::string> current;
    if (std::filesystem::exists(engine, error)) {
        Result<std::vector<std::string>> files = ListFiles(engine, {"__pycache__"});
        if (!files.Ok()) return Plan::Failure(files.Error());
        current = std::move(files).Value();
    }

    std::optional<EngineCopyRecord> own; // A copy's record: its files are the engine's, not the user's
    if (project.kind == EngineKind::Copy) {
        const EngineVersion from = project.manifest->version;
        if (target.manifest.version < from)
            return Plan::Failure("The project uses " + from.ToString() + "; " + target.manifest.version.ToString() +
                                 " is older");
        Result<EngineCopyRecord> record = LoadEngineCopyRecord(engine);
        if (!record.Ok()) return Plan::Failure(record.Error());
        Result<CopyIntegrity> integrity = CheckCopyIntegrity(engine, record.Value());
        if (!integrity.Ok()) return Plan::Failure(integrity.Error());
        plan.from = from;
        plan.overwritten = integrity.Value().modified;
        plan.notes = NotesSince(target.manifest, from);
        plan.mayBreak = MayBreak(from, target.manifest.version);
        own = std::move(record).Value();
    } else {
        Result<std::vector<std::string>> differing = DifferingFromTarget(engine, current, target.record);
        if (!differing.Ok()) return Plan::Failure(differing.Error());
        plan.overwritten = std::move(differing).Value();
        plan.notes = target.manifest.notes;
    }
    // Engine files the new version dropped go silently; anything else that disappears is listed
    for (const std::string& file : current) {
        const bool engineFile = file == EngineCopyFile || (own && own->files.contains(file));
        if (!engineFile && !target.record.files.contains(file)) plan.deleted.push_back(file);
    }
    return plan;
}

Status ApplyEngineUpdate(const EngineUpdatePlan& plan) {
    const std::filesystem::path engine = plan.projectRoot / ProjectInfo::EngineFolderName;
    const std::filesystem::path staging = plan.projectRoot / StagingFolder;
    const std::filesystem::path retired = plan.projectRoot / RetiredFolder;
    std::error_code error;
    for (const std::filesystem::path& leftover : {staging, retired})
        if (Status removed = RemoveTree(leftover); !removed.Ok()) return removed;

    if (Status copied = CopyTree(plan.target.folder, staging); !copied.Ok()) {
        (void)RemoveTree(staging);
        return copied;
    }
    const bool hadEngine = std::filesystem::exists(engine, error);
    if (hadEngine) {
        if (Status moved = MovePath(engine, retired); !moved.Ok()) {
            (void)RemoveTree(staging);
            return Status::Failure(moved.Error() + " (close the project's editor and game, then try again)");
        }
    }
    if (Status placed = MovePath(staging, engine); !placed.Ok()) {
        if (hadEngine) (void)MovePath(retired, engine);
        (void)RemoveTree(staging);
        return placed;
    }
    (void)RemoveTree(retired); // A leftover is removed by the next update
    return Status::Success();
}
