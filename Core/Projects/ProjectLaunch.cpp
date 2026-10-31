#include "Projects/ProjectLaunch.h"
#include "Engines/EngineCopyRecord.h"
#include "Projects/CopyIntegrity.h"

namespace {
constexpr const char* CanceledError = "Canceled";

void ReportCopyIntegrity(const ProjectInfo& project, JobContext& job) {
    Result<EngineCopyRecord> record = LoadEngineCopyRecord(project.EngineFolder());
    if (!record.Ok()) {
        job.Output("warning: " + record.Error());
        return;
    }
    Result<CopyIntegrity> integrity = CheckCopyIntegrity(project.EngineFolder(), record.Value());
    if (!integrity.Ok()) {
        job.Output("warning: " + integrity.Error());
        return;
    }
    const CopyIntegrity& found = integrity.Value();
    if (found.Clean()) return;
    job.Output("warning: the engine copy differs from " + record.Value().tag + " (" +
               std::to_string(found.modified.size()) + " modified, " + std::to_string(found.missing.size()) +
               " missing, " + std::to_string(found.added.size()) +
               " added); the engine is read-only, repair it from the project list");
}

Status ProcessFailure(const ProcessResult& result, const std::string& what) {
    if (result.canceled) return Status::Failure(CanceledError);
    if (!result.Started()) return Status::Failure(result.startError);
    return Status::Failure(what + " (exit code " + std::to_string(result.exitCode) + ")");
}
} // namespace

Status OpenProject(const ProjectInfo& project, const Toolchain& toolchain, const EngineRunner& runEngine,
                   const ProcessStarter& start, JobContext& job) {
    job.Step("Checking engine files");
    if (project.kind == EngineKind::Copy) ReportCopyIntegrity(project, job);
    if (job.CancelRequested()) return Status::Failure(CanceledError);

    job.Step("Building editor");
    const ProcessResult build = runEngine(project.root, {"editor", "--build-only"}, job.Lines());
    if (!build.Succeeded()) return ProcessFailure(build, "The editor build failed");
    if (job.CancelRequested()) return Status::Failure(CanceledError);

    job.Step("Starting editor");
    Result<ProcessCommand> command = EngineCommand(toolchain, project, {"editor"});
    if (!command.Ok()) return Status::Failure(command.Error());
    if (std::string error = start(command.Value()); !error.empty()) return Status::Failure(error);
    return Status::Success();
}

Status RunProject(const ProjectInfo& project, const EngineRunner& runEngine, JobContext& job) {
    job.Step("Building and running the game");
    const ProcessResult run = runEngine(project.root, {"run"}, job.Lines());
    if (!run.Succeeded()) return ProcessFailure(run, "The game build or run failed");
    return Status::Success();
}
