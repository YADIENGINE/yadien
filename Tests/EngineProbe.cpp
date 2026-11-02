// Scripts/engine_test.py's driver: the launcher's real engine download, trial and switch without its window. Output
// lines are read by the script.
#include <atomic>
#include <iostream>
#include <string>
#include "Cli/EngineCommand.h"
#include "Engines/EngineCatalog.h"
#include "Jobs/Job.h"
#include "Platform/Text.h"
#include "Projects/EngineTrial.h"
#include "Projects/EngineUpdate.h"
#include "System/Toolchain.h"

namespace {
constexpr int ExitFailure = 1;
constexpr int ArgumentsBeforeCommand = 3; // Program, tools folder, data folder

struct ProbePaths {
    std::filesystem::path tools;
    std::filesystem::path data;
};

int Fail(const std::string& message) {
    std::cout << "error: " << message << "\n";
    return ExitFailure;
}

Result<std::vector<OnlineEngine>> ReadCatalog(const ProbePaths& paths) {
    return FetchEngineCatalog(EngineCatalogSource(""), true, paths.data / "EngineCatalog", nullptr);
}

Result<AvailableEngine> Download(const ProbePaths& paths, const std::string& version) {
    Result<std::vector<OnlineEngine>> catalog = ReadCatalog(paths);
    if (!catalog.Ok()) return Result<AvailableEngine>::Failure(catalog.Error());
    for (const OnlineEngine& engine : catalog.Value())
        if (engine.version.ToString() == version) return DownloadEngine(engine, paths.data / "Engines", nullptr);
    return Result<AvailableEngine>::Failure(version + " is not in the list");
}

Result<EngineUpdatePlan> Plan(const ProbePaths& paths, const std::string& root, const std::string& version) {
    Result<AvailableEngine> engine = Download(paths, version);
    if (!engine.Ok()) return Result<EngineUpdatePlan>::Failure(engine.Error());
    return PlanEngineUpdate(InspectProject(Utf8ToPath(root)), engine.Value());
}

int Trial(const ProbePaths& paths, const std::string& root, const std::string& version) {
    Result<EngineUpdatePlan> plan = Plan(paths, root, version);
    if (!plan.Ok()) return Fail(plan.Error());
    Result<Toolchain> toolchain = LoadToolchain(paths.tools);
    if (!toolchain.Ok()) return Fail(toolchain.Error());
    Job<EngineTrial> job("Trial", EngineTrialSteps, [&](JobContext& context) {
        return RunEngineTrial(plan.Value(), paths.data / "EngineTrial",
                              LauncherEngineRunner(toolchain.Value(), context.CancelFlag()), context);
    });
    while (!job.Finished())
        std::this_thread::yield();
    std::size_t next = 0;
    for (const std::string& line : job.LinesFrom(next))
        std::cout << line << "\n";
    if (job.State() != JobState::Succeeded) return Fail(job.Error());
    const EngineTrial& trial = job.Outcome()->Value();
    switch (trial.verdict) {
    case TrialVerdict::Passed   : std::cout << "passed\n"; return 0;
    case TrialVerdict::Failed   : std::cout << "failed " << trial.check << "\n"; return 0;
    case TrialVerdict::NotTested: return Fail("not tested: " + trial.reason);
    }
    return ExitFailure;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < ArgumentsBeforeCommand + 1) {
        std::cerr << "Usage: LauncherEngineProbe <tools> <data> list | download V | try-download V | trial ROOT V | "
                     "switch ROOT V\n";
        return ExitFailure;
    }
    const ProbePaths paths{Utf8ToPath(argv[1]), Utf8ToPath(argv[2])};
    const std::string command = argv[ArgumentsBeforeCommand];
    if (command == "list") {
        Result<std::vector<OnlineEngine>> catalog = ReadCatalog(paths);
        if (!catalog.Ok()) return Fail(catalog.Error());
        for (const OnlineEngine& engine : catalog.Value())
            std::cout << engine.version.ToString() << "\n";
        return 0;
    }
    if (argc < ArgumentsBeforeCommand + 2) return Fail("missing arguments");
    if (command == "download" || command == "try-download") {
        Result<AvailableEngine> engine = Download(paths, argv[ArgumentsBeforeCommand + 1]);
        if (engine.Ok()) {
            std::cout << "downloaded\n";
            return 0;
        }
        if (command == "download") return Fail(engine.Error());
        std::cout << "refused: " << engine.Error() << "\n";
        return 0;
    }
    if (argc < ArgumentsBeforeCommand + 3) return Fail("missing arguments");
    const std::string root = argv[ArgumentsBeforeCommand + 1];
    const std::string version = argv[ArgumentsBeforeCommand + 2];
    if (command == "trial") return Trial(paths, root, version);
    if (command == "switch") {
        Result<EngineUpdatePlan> plan = Plan(paths, root, version);
        if (!plan.Ok()) return Fail(plan.Error());
        if (Status applied = ApplyEngineUpdate(plan.Value()); !applied.Ok()) return Fail(applied.Error());
        std::cout << "switched\n";
        return 0;
    }
    return Fail("unknown command " + command);
}
