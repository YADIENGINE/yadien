#include "Projects/ProjectDetails.h"
#include "Engines/EngineCopyRecord.h"

std::vector<ProjectDetails> InspectProjectDetails(const std::vector<ProjectRow>& rows, LauncherMode mode,
                                                  const std::atomic<bool>* cancel) {
    std::vector<ProjectDetails> details;
    for (const ProjectRow& row : rows) {
        if (cancel && *cancel) break;
        if (row.folderMissing) continue;
        if (row.info.kind == EngineKind::Copy) {
            ProjectDetails entry{row.entry.root};
            Result<EngineCopyRecord> record = LoadEngineCopyRecord(row.info.EngineFolder());
            if (!record.Ok()) {
                entry.error = record.Error();
            } else if (Result<CopyIntegrity> integrity = CheckCopyIntegrity(row.info.EngineFolder(), record.Value());
                       integrity.Ok()) {
                entry.integrity = std::move(integrity).Value();
            } else {
                entry.error = integrity.Error();
            }
            details.push_back(std::move(entry));
        } else if (row.info.kind == EngineKind::Git && mode == LauncherMode::Developer) {
            ProjectDetails entry{row.entry.root};
            if (Result<EngineGitStatus> status = ReadEngineGitStatus(row.entry.root); status.Ok())
                entry.git = std::move(status).Value();
            else
                entry.error = status.Error();
            details.push_back(std::move(entry));
        }
    }
    return details;
}
