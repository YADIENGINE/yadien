#include "Projects/EngineTrial.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include "Platform/FileSystem.h"

namespace {
constexpr const char* CanceledError = "Canceled";
constexpr std::string_view ErrorSummaryStart = "Errors ("; // Engine Docs/ToolsContract.md: the end of a failed build
constexpr std::size_t MaxErrorLines = 40;
constexpr int ProjectWorkFailed = 1; // Engine Docs/ToolsContract.md: exit codes

struct TrialCheck {
    const char* section; // Project.json section the step needs
    const char* name;
    std::vector<std::string> arguments;
};

const std::array<TrialCheck, EngineTrialSteps - 1>& TrialChecks() {
    static const std::array<TrialCheck, EngineTrialSteps - 1> Checks{
        {{"game", "Game build", {"build"}},
         {"editor", "Editor build", {"editor", "--build-only"}},
         {"tests", "Project tests", {"test"}}}};
    return Checks;
}

std::vector<std::string> ErrorLines(const std::vector<std::string>& lines) {
    const auto summary = std::find_if(lines.rbegin(), lines.rend(),
                                      [](const std::string& line) { return line.starts_with(ErrorSummaryStart); });
    if (summary != lines.rend()) return {summary.base(), lines.end()};
    const std::size_t kept = std::min(lines.size(), MaxErrorLines);
    return {lines.end() - static_cast<std::ptrdiff_t>(kept), lines.end()};
}

Status PrepareCopy(const EngineUpdatePlan& plan, const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> skipped = ProjectBuildFolders(plan.projectRoot);
    if (std::optional<std::filesystem::path> release = ProjectReleaseFolder(plan.projectRoot))
        skipped.push_back(*release);
    for (const char* name : {ProjectInfo::EngineFolderName, ".git", ".Engine.updating", ".Engine.old"})
        skipped.emplace_back(name);
    if (Status copied = CopyTreeExcept(plan.projectRoot, folder, skipped); !copied.Ok()) return copied;
    return CopyTree(plan.target.folder, folder / ProjectInfo::EngineFolderName);
}

Result<EngineTrial> RunChecks(const std::filesystem::path& folder, const nlohmann::json& project,
                              const EngineRunner& runEngine, JobContext& job) {
    bool ranAny = false;
    for (const TrialCheck& check : TrialChecks()) {
        job.Step(check.name);
        if (!project.is_object() || !project.contains(check.section)) continue;
        ranAny = true;
        std::vector<std::string> lines;
        const OutputLine forward = job.Lines();
        const ProcessResult result = runEngine(folder, check.arguments, [&](const std::string& line) {
            lines.push_back(line);
            forward(line);
        });
        if (result.canceled || job.CancelRequested()) return Result<EngineTrial>::Failure(CanceledError);
        if (result.Succeeded()) continue;
        if (result.Started() && result.exitCode == ProjectWorkFailed)
            return EngineTrial{TrialVerdict::Failed, check.name, ErrorLines(lines), ""};
        const std::string reason =
            !result.Started() ? result.startError
                              : std::string(check.name) + " stopped with exit code " + std::to_string(result.exitCode);
        return EngineTrial{TrialVerdict::NotTested, check.name, ErrorLines(lines), reason};
    }
    if (!ranAny) return EngineTrial{TrialVerdict::NotTested, "", {}, "Project.json names nothing to build"};
    return EngineTrial{TrialVerdict::Passed, "", {}, ""};
}
} // namespace

Result<EngineTrial> RunEngineTrial(const EngineUpdatePlan& plan, const std::filesystem::path& folder,
                                   const EngineRunner& runEngine, JobContext& job) {
    using Answer = Result<EngineTrial>;
    job.Step("Copying the project");
    if (Status removed = RemoveTree(folder); !removed.Ok()) return Answer::Failure(removed.Error());
    if (Status prepared = PrepareCopy(plan, folder); !prepared.Ok()) {
        (void)RemoveTree(folder);
        return Answer::Failure(prepared.Error());
    }
    Result<nlohmann::json> project = ReadJsonFile(folder / ProjectFile);
    if (!project.Ok()) {
        (void)RemoveTree(folder);
        return Answer::Failure(project.Error());
    }
    Answer outcome = RunChecks(folder, project.Value(), runEngine, job);
    (void)RemoveTree(folder);
    return outcome;
}

std::string EngineTrialReport(const EngineUpdatePlan& plan, const EngineTrial& trial, const std::string& projectName) {
    const std::string to = plan.target.manifest.version.ToString();
    std::string text = "I am moving the project " + projectName + " from engine " +
                       (plan.from ? plan.from->ToString() : std::string("(unknown version)")) + " to " + to + ".\n";
    if (!trial.check.empty()) text += trial.check + " failed:\n";
    for (const std::string& line : trial.errors)
        text += "  " + line + "\n";
    std::string steps;
    for (const EngineNote& note : plan.notes)
        for (const BreakingChange& change : note.breaking)
            steps += "- " + note.version.ToString() + ": " + change.change + " Migrate: " + change.migrate + "\n";
    if (!steps.empty()) text += "\nBreaking changes on the way:\n" + steps;
    text += "\nThe engine's documents are in Engine/Docs (migration tables under Engine/Docs/Editor/Development). "
            "Change the project, not the engine.\n";
    return text;
}
