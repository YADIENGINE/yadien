#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <thread>
#include "Jobs/Job.h"
#include "Projects/ProjectDetails.h"
#include "Projects/ProjectLaunch.h"
#include "Projects/ProjectRow.h"
#include "Projects/ProjectScan.h"
#include "Settings/ProjectList.h"
#include "Support/FakeEngine.h"
#include "Support/TempFolder.h"
#include "System/UserPath.h"

namespace {
bool WaitFinished(const BackgroundJob& job) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!job.Finished() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return job.Finished();
}

// Records the engine commands and answers with `exitCode`.
EngineRunner FakeRunner(int exitCode, std::vector<std::vector<std::string>>& calls) {
    return [exitCode, &calls](const std::filesystem::path&, const std::vector<std::string>& arguments,
                              const OutputLine& onLine) {
        calls.push_back(arguments);
        onLine("ninja: building");
        return ProcessResult{exitCode, false, ""};
    };
}

ProjectEntry Entry(const std::filesystem::path& root, std::string lastOpened = "") {
    return {CanonicalRoot(root), std::move(lastOpened)};
}

std::vector<std::string> Names(const std::vector<ProjectRow>& rows, const std::vector<std::size_t>& visible) {
    std::vector<std::string> names;
    for (std::size_t index : visible)
        names.push_back(rows[index].info.name);
    return names;
}
} // namespace

TEST_CASE("Opening checks a copy, builds the editor, then starts it", "[projects]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "MyGame", "MyGame", {});
    temp.Write("MyGame/Engine/Tools/cli.py", "edited\n");
    const ProjectInfo project = InspectProject(temp.Path() / "MyGame");
    std::vector<std::vector<std::string>> calls;
    std::vector<ProcessCommand> started;
    const ProcessStarter start = [&](const ProcessCommand& command) {
        started.push_back(command);
        return std::string();
    };

    Job<void> job("Open", OpenProjectSteps, [&](JobContext& context) {
        return OpenProject(project, Toolchain{}, FakeRunner(0, calls), start, context);
    });
    REQUIRE(WaitFinished(job));
    CHECK(job.State() == JobState::Succeeded);
    CHECK(job.StepIndex() == OpenProjectSteps);
    CHECK(calls == std::vector<std::vector<std::string>>{{"editor", "--build-only"}});
    REQUIRE(started.size() == 1);
    CHECK(started[0].arguments.back() == "editor");
    std::size_t next = 0;
    const std::vector<std::string> lines = job.LinesFrom(next);
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.front().starts_with("warning: the engine copy differs")); // Edited copy: warned, still opened
}

TEST_CASE("A failed editor build does not start the editor", "[projects]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "MyGame", "MyGame", {});
    const ProjectInfo project = InspectProject(temp.Path() / "MyGame");
    std::vector<std::vector<std::string>> calls;
    bool started = false;
    Job<void> job("Open", OpenProjectSteps, [&](JobContext& context) {
        return OpenProject(
            project, Toolchain{}, FakeRunner(1, calls),
            [&](const ProcessCommand&) {
                started = true;
                return std::string();
            },
            context);
    });
    REQUIRE(WaitFinished(job));
    CHECK(job.State() == JobState::Failed);
    CHECK(job.Error() == "The editor build failed (exit code 1)");
    CHECK_FALSE(started);
}

TEST_CASE("Running hands the engine's run command over", "[projects]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "MyGame", "MyGame", {});
    std::vector<std::vector<std::string>> calls;
    Job<void> job("Run", RunProjectSteps, [&](JobContext& context) {
        return RunProject(InspectProject(temp.Path() / "MyGame"), FakeRunner(0, calls), context);
    });
    REQUIRE(WaitFinished(job));
    CHECK(job.State() == JobState::Succeeded);
    CHECK(calls == std::vector<std::vector<std::string>>{{"run"}});
}

TEST_CASE("Rows offer updates and actions by engine kind and mode", "[projects]") {
    TempFolder temp;
    WriteFakeEngine(temp.Path() / "Engines/Engine0.1", {.version = "0.1.1"});
    WriteFakeEngine(temp.Path() / "Engines/Engine0.2", {.version = "0.2.0"});
    const EngineLibrary engines = EngineLibrary::Scan(temp.Path() / "Engines");
    WriteFakeProject(temp.Path() / "Old", "Old", {.version = "0.1.1"});
    WriteFakeProject(temp.Path() / "Current", "Current", {.version = "0.2.0"});
    WriteFakeProject(temp.Path() / "Unknown", "Unknown", {});
    std::filesystem::remove(temp.Path() / "Unknown/Engine/EngineCopy.json");

    const ProjectRow old = InspectProjectRow(Entry(temp.Path() / "Old"), engines);
    CHECK(old.Version() == "0.1.1");
    REQUIRE(old.update);
    CHECK(old.update->ToString() == "0.2.0");
    CHECK(old.updateMayBreak); // 0.x: a minor step may break
    ProjectActions actions = ActionsFor(old, engines, LauncherMode::User);
    CHECK(actions.open);
    CHECK(actions.updateEngine);
    CHECK(actions.repairEngine);
    CHECK_FALSE(actions.replaceEngine);
    CHECK_FALSE(actions.engineGit);

    const ProjectRow current = InspectProjectRow(Entry(temp.Path() / "Current"), engines);
    CHECK_FALSE(current.update);
    CHECK_FALSE(ActionsFor(current, engines, LauncherMode::User).updateEngine);

    const ProjectRow unknown = InspectProjectRow(Entry(temp.Path() / "Unknown"), engines);
    CHECK(unknown.info.kind == EngineKind::Unregistered);
    actions = ActionsFor(unknown, engines, LauncherMode::Developer);
    CHECK(actions.replaceEngine);
    CHECK(ReplaceTarget(unknown, engines) == engines.Newest());
    CHECK_FALSE(actions.repairEngine);

    const ProjectRow gone = InspectProjectRow(Entry(temp.Path() / "Gone"), engines);
    CHECK(gone.folderMissing);
    actions = ActionsFor(gone, engines, LauncherMode::User);
    CHECK_FALSE(actions.open);
    CHECK_FALSE(actions.showInFolder);
    CHECK_FALSE(actions.replaceEngine);
    CHECK(actions.remove);
}

TEST_CASE("An engine the launcher cannot drive blocks opening", "[projects]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "Future", "Future", {.toolsContract = 99});
    const ProjectRow row = InspectProjectRow(Entry(temp.Path() / "Future"), EngineLibrary{});
    CHECK_FALSE(row.contractSupported);
    CHECK_FALSE(ActionsFor(row, EngineLibrary{}, LauncherMode::User).open);
}

TEST_CASE("The list filters by name or folder and sorts three ways", "[projects]") {
    TempFolder temp;
    LauncherSettings settings;
    for (const char* name : {"beta", "Alpha", "gamma"})
        WriteFakeProject(temp.Path() / name, name, {});
    settings.projects = {Entry(temp.Path() / "beta", "2026-10-30T10:00:00Z"), Entry(temp.Path() / "Alpha"),
                         Entry(temp.Path() / "gamma", "2026-10-31T10:00:00Z")};
    const std::vector<ProjectRow> rows = InspectProjectRows(settings, EngineLibrary{});

    CHECK(Names(rows, VisibleRows(rows, "", ProjectSort::LastOpened)) ==
          std::vector<std::string>{"gamma", "beta", "Alpha"});
    CHECK(Names(rows, VisibleRows(rows, "", ProjectSort::Name)) == std::vector<std::string>{"Alpha", "beta", "gamma"});
    CHECK(Names(rows, VisibleRows(rows, "", ProjectSort::Path)) == std::vector<std::string>{"Alpha", "beta", "gamma"});
    // Name or folder, case-insensitive; the text must not occur in the shared temp path above the projects
    CHECK(Names(rows, VisibleRows(rows, "LPH", ProjectSort::Name)) == std::vector<std::string>{"Alpha"});
    CHECK(VisibleRows(rows, "nothing", ProjectSort::Name).empty());
    CHECK(ProjectsUsing(rows, *EngineVersion::Parse("0.1.0")) == 3);
}

TEST_CASE("Details report edited copies; clean ones are clean", "[projects]") {
    TempFolder temp;
    LauncherSettings settings;
    WriteFakeProject(temp.Path() / "Clean", "Clean", {});
    WriteFakeProject(temp.Path() / "Edited", "Edited", {});
    temp.Write("Edited/Engine/Tools/cli.py", "edited\n");
    settings.projects = {Entry(temp.Path() / "Clean"), Entry(temp.Path() / "Edited"), Entry(temp.Path() / "Gone")};
    const std::vector<ProjectDetails> details =
        InspectProjectDetails(InspectProjectRows(settings, EngineLibrary{}), LauncherMode::User);
    REQUIRE(details.size() == 2); // The missing folder has nothing to check
    REQUIRE(details[0].integrity);
    CHECK(details[0].integrity->Clean());
    REQUIRE(details[1].integrity);
    CHECK(details[1].integrity->modified == std::vector<std::string>{"Tools/cli.py"});
}

TEST_CASE("Scan finds project folders without entering projects or build output", "[projects]") {
    TempFolder temp;
    temp.Write("Games/One/Project.json", "{}");
    temp.Write("Games/One/Sub/Project.json", "{}"); // Inside a project: not entered
    temp.Write("Games/Group/Two/Project.json", "{}");
    temp.Write("Games/build/Three/Project.json", "{}");
    temp.Write("Games/.hidden/Four/Project.json", "{}");
    CHECK(FindProjects(temp.Path() / "Games") ==
          std::vector<std::filesystem::path>{CanonicalRoot(temp.Path() / "Games/Group/Two"),
                                             CanonicalRoot(temp.Path() / "Games/One")});
    CHECK(FindProjects(temp.Path() / "Games/One") ==
          std::vector<std::filesystem::path>{CanonicalRoot(temp.Path() / "Games/One")});
    std::atomic<bool> cancel = true;
    CHECK(FindProjects(temp.Path() / "Games", &cancel).empty());
}

TEST_CASE("PATH entries are added once and removed in any spelling", "[system]") {
    const std::filesystem::path folder = "C:\\Tools\\Engine";
    CHECK(PathListWith("", folder) == "C:\\Tools\\Engine");
    CHECK(PathListWith("C:\\A", folder) == "C:\\A;C:\\Tools\\Engine");
    CHECK(PathListWith("C:\\A;", folder) == "C:\\A;C:\\Tools\\Engine");
    CHECK(PathListWith("c:\\tools\\engine\\;C:\\A", folder) == "c:\\tools\\engine\\;C:\\A");
    CHECK(PathListContains("C:\\A;\"C:\\Tools\\Engine\"", folder));
    CHECK_FALSE(PathListContains("C:\\Tools\\Engine2", folder));
    CHECK(PathListWithout("C:\\A;C:/tools/engine;%USERPROFILE%\\bin", folder) == "C:\\A;%USERPROFILE%\\bin");
    CHECK(PathListWithout("C:\\A", folder) == "C:\\A");
}
