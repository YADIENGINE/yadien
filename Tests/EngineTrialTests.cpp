#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <thread>
#include "Engines/EngineCatalog.h"
#include "Engines/EngineLibrary.h"
#include "Jobs/Job.h"
#include "Platform/Crypto.h"
#include "Platform/FileSystem.h"
#include "Platform/KnownFolders.h"
#include "Platform/Process.h"
#include "Platform/Text.h"
#include "Projects/EngineTrial.h"
#include "Projects/ProjectRow.h"
#include "Support/FakeEngine.h"
#include "Support/TempFolder.h"

namespace {
constexpr const char* FullProject =
    R"({"version": 1, "name": "Game", "game": {"buildDirectory": "build"}, "editor": {"buildDirectory": "build/editor"},
        "tests": {"buildDirectory": "build/tests"}, "release": {"directory": "Release"}})";

AvailableEngine Stable(const std::filesystem::path& engines, const std::string& version) {
    WriteFakeEngine(engines / ("Engine" + version), {.version = version, .files = {{"Core/New.h", "new\n"}}});
    return *EngineLibrary::Scan(engines).Find(*EngineVersion::Parse(version));
}

// Answers each engine command with the exit code for its first argument; records what the copy looked like.
struct FakeTools {
    int editorExit = 0;
    std::vector<std::string> ran;
    bool sawTargetEngine = false;
    bool sawOldBuild = false;

    EngineRunner Runner() {
        return [this](const std::filesystem::path& root, const std::vector<std::string>& arguments,
                      const OutputLine& onLine) {
            ran.push_back(arguments.front());
            sawTargetEngine = std::filesystem::exists(root / "Engine/Core/New.h");
            sawOldBuild = std::filesystem::exists(root / "build/Old.obj");
            if (arguments.front() == "editor" && editorExit != 0) {
                onLine("configuring");
                onLine("Errors (1):");
                onLine("ninja: error: unknown target 'GameEditorWorker'");
            }
            return ProcessResult{arguments.front() == "editor" ? editorExit : 0, false, ""};
        };
    }
};

Result<EngineTrial> RunTrial(const EngineUpdatePlan& plan, const std::filesystem::path& folder, FakeTools& tools) {
    Job<EngineTrial> job("Trial", EngineTrialSteps,
                         [&](JobContext& context) { return RunEngineTrial(plan, folder, tools.Runner(), context); });
    while (!job.Finished())
        std::this_thread::yield();
    return *job.Outcome();
}

struct TrialProject {
    TempFolder temp;
    std::filesystem::path root = temp.Path() / "Game";
    EngineUpdatePlan plan;

    TrialProject() {
        WriteFakeProject(root, "Game", {.version = "0.2.0"});
        temp.Write("Game/Project.json", FullProject);
        temp.Write("Game/Game/Main.cpp", "game\n");
        temp.Write("Game/build/Old.obj", "object\n");
        temp.Write("Game/Release/Game.zip", "zip\n");
        plan = PlanEngineUpdate(InspectProject(root), Stable(temp.Path() / "Engines", "0.3.0")).Value();
    }
};
} // namespace

TEST_CASE("A trial builds game, editor and tests in a copy with the new engine and no old build", "[engine-trial]") {
    TrialProject project;
    FakeTools tools;
    Result<EngineTrial> trial = RunTrial(project.plan, project.temp.Path() / "Trial", tools);
    REQUIRE(trial.Ok());
    CHECK(trial.Value().verdict == TrialVerdict::Passed);
    CHECK(tools.ran == std::vector<std::string>{"build", "editor", "test"});
    CHECK(tools.sawTargetEngine);
    CHECK_FALSE(tools.sawOldBuild);
    CHECK_FALSE(std::filesystem::exists(project.temp.Path() / "Trial"));
    CHECK(InspectProject(project.root).manifest->version == EngineVersion{0, 2, 0});
}

TEST_CASE("A trial that fails stops there, reports the errors and leaves the project alone", "[engine-trial]") {
    TrialProject project;
    FakeTools tools{.editorExit = 1};
    Result<EngineTrial> trial = RunTrial(project.plan, project.temp.Path() / "Trial", tools);
    REQUIRE(trial.Ok());
    CHECK(trial.Value().verdict == TrialVerdict::Failed);
    CHECK(trial.Value().check == "Editor build");
    CHECK(trial.Value().errors == std::vector<std::string>{"ninja: error: unknown target 'GameEditorWorker'"});
    CHECK(tools.ran == std::vector<std::string>{"build", "editor"});
    CHECK(std::filesystem::exists(project.root / "build/Old.obj"));
    CHECK(InspectProject(project.root).manifest->version == EngineVersion{0, 2, 0});
    const std::string report = EngineTrialReport(project.plan, trial.Value(), "Game");
    CHECK(report.find("0.2.0") != std::string::npos);
    CHECK(report.find("unknown target") != std::string::npos);
}

TEST_CASE("A tool problem is not the project's failure", "[engine-trial]") {
    TrialProject project;
    FakeTools tools{.editorExit = 2}; // Engine docs: usage or infrastructure, e.g. a missing compiler
    Result<EngineTrial> trial = RunTrial(project.plan, project.temp.Path() / "Trial", tools);
    REQUIRE(trial.Ok());
    CHECK(trial.Value().verdict == TrialVerdict::NotTested);
    CHECK_FALSE(trial.Value().reason.empty());
}

TEST_CASE("The steps shown are the ones the project will go through", "[engine-trial]") {
    TempFolder temp;
    temp.Write("Project.json", R"({"version": 1, "name": "Game", "game": {"buildDirectory": "build"}, "tests": {}})");
    CHECK(EngineTrialStepNames(temp.Path()) ==
          std::vector<std::string>{"Copying the project", "Game build", "Project tests"});
    temp.Write("Project.json", R"({"version": 1, "name": "Game"})");
    CHECK(EngineTrialStepNames(temp.Path()) == std::vector<std::string>{"Copying the project"});
    CHECK(EngineTrialStepNames(temp.Path() / "Missing") == std::vector<std::string>{"Copying the project"});
}

TEST_CASE("Only what Project.json names is tried", "[engine-trial]") {
    TrialProject project;
    project.temp.Write("Game/Project.json", R"({"version": 1, "name": "Game", "game": {"buildDirectory": "build"}})");
    FakeTools tools;
    REQUIRE(RunTrial(project.plan, project.temp.Path() / "Trial", tools).Ok());
    CHECK(tools.ran == std::vector<std::string>{"build"});

    project.temp.Write("Game/Project.json", R"({"version": 1, "name": "Game"})");
    Result<EngineTrial> nothing = RunTrial(project.plan, project.temp.Path() / "Trial", tools);
    REQUIRE(nothing.Ok());
    CHECK(nothing.Value().verdict == TrialVerdict::NotTested);
}

TEST_CASE("Changing the engine removes the build folders and keeps everything else", "[engine-trial]") {
    TrialProject project;
    REQUIRE(ApplyEngineUpdate(project.plan).Ok());
    CHECK(InspectProject(project.root).manifest->version == EngineVersion{0, 3, 0});
    CHECK_FALSE(std::filesystem::exists(project.root / "build"));
    CHECK(project.temp.Read("Game/Game/Main.cpp") == "game\n");
    CHECK(project.temp.Read("Game/Release/Game.zip") == "zip\n");
}

TEST_CASE("Folders in Project.json cannot point outside the project or at its engine", "[engine-trial]") {
    TempFolder temp;
    temp.Write("Game/Project.json", R"({"game": {"buildDirectory": "../Other"}, "tests": {"buildDirectory": "C:/Win"},
        "editor": {"buildDirectory": "Engine/build"}, "release": {"directory": "."}})");
    CHECK(ProjectBuildFolders(temp.Path() / "Game").empty());
    CHECK_FALSE(ProjectReleaseFolder(temp.Path() / "Game").has_value());
}

TEST_CASE("A project can go back to an older engine, and sees what it loses", "[engine-trial]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "New", "New", {.version = "0.3.0", .history = {"0.3.0", "0.2.0"}});
    Result<EngineUpdatePlan> plan =
        PlanEngineUpdate(InspectProject(temp.Path() / "New"), Stable(temp.Path() / "Engines", "0.2.0"));
    REQUIRE(plan.Ok());
    CHECK(plan.Value().wentBack);
    CHECK(plan.Value().mayBreak);
    REQUIRE(plan.Value().notes.size() == 1);
    CHECK(plan.Value().notes[0].version == EngineVersion{0, 3, 0});
}

TEST_CASE("An engine this launcher cannot drive is refused", "[engine-trial]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "Game", "Game", {.version = "0.2.0"});
    WriteFakeEngine(temp.Path() / "Engines/Engine0.3.0", {.version = "0.3.0", .toolsContract = 99});
    const EngineLibrary library = EngineLibrary::Scan(temp.Path() / "Engines");
    Result<EngineUpdatePlan> plan = PlanEngineUpdate(InspectProject(temp.Path() / "Game"), *library.Newest());
    REQUIRE_FALSE(plan.Ok());
    CHECK(plan.Error().find("newer launcher") != std::string::npos);
}

TEST_CASE("The catalog lists releases with a record, newest version first", "[engine-catalog]") {
    const std::string answer = R"([
      {"tag_name": "v0.2.0", "prerelease": true, "tarball_url": "a2",
       "assets": [{"name": "EngineCopy.json", "browser_download_url": "r2"}]},
      {"tag_name": "v0.10.0", "tarball_url": "a10",
       "assets": [{"name": "Other.zip", "browser_download_url": "x"},
                  {"name": "EngineCopy.json", "browser_download_url": "r10"}]},
      {"tag_name": "v0.3.0", "tarball_url": "a3", "assets": []},
      {"tag_name": "v0.4.0", "draft": true, "tarball_url": "a4",
       "assets": [{"name": "EngineCopy.json", "browser_download_url": "r4"}]},
      {"tag_name": "nightly", "tarball_url": "n", "assets": [{"name": "EngineCopy.json", "browser_download_url": "n"}]}
    ])";
    Result<std::vector<OnlineEngine>> all = ParseEngineCatalog(answer, true);
    REQUIRE(all.Ok());
    REQUIRE(all.Value().size() == 2);
    CHECK(all.Value()[0].tag == "v0.10.0");
    CHECK(all.Value()[0].recordUrl == "r10");
    CHECK(all.Value()[0].archiveUrl == "a10");
    CHECK(all.Value()[1].tag == "v0.2.0");
    CHECK(ParseEngineCatalog(answer, false).Value().size() == 1);
    CHECK_FALSE(ParseEngineCatalog("{}", true).Ok());
}

TEST_CASE("A release's compiled engine is listed only with its digest", "[engine-catalog]") {
    constexpr const char* answer = R"([
      {"tag_name": "v0.2.0", "tarball_url": "a",
       "assets": [{"name": "EngineCopy.json", "browser_download_url": "r"},
                  {"name": "EnginePrebuilt.zip", "browser_download_url": "p", "digest": "sha256:abc"}]},
      {"tag_name": "v0.1.0", "tarball_url": "a",
       "assets": [{"name": "EngineCopy.json", "browser_download_url": "r"},
                  {"name": "EnginePrebuilt.zip", "browser_download_url": "p"}]}
    ])";
    Result<std::vector<OnlineEngine>> all = ParseEngineCatalog(answer, true);
    REQUIRE(all.Ok());
    REQUIRE(all.Value().size() == 2);
    CHECK(all.Value()[0].prebuiltUrl == "p");
    CHECK(all.Value()[0].prebuiltSha256 == "abc");
    CHECK(all.Value()[1].prebuiltUrl.empty());
}

TEST_CASE("A compiled engine enters its folder only when its digest matches", "[engine-catalog]") {
    TempFolder temp;
    temp.Write("Package/Prebuilt.json", "{}");
    const std::filesystem::path zip = temp.Path() / "EnginePrebuilt.zip";
    const ProcessCommand pack{
        SystemFolder() / "tar.exe",
        {"-a", "-cf", PathToUtf8(zip), "-C", PathToUtf8(temp.Path() / "Package"), "Prebuilt.json"}};
    REQUIRE(RunCaptured(pack, [](const std::string&) {}, nullptr).Succeeded());
    OnlineEngine engine{*EngineVersion::Parse("1.2.3"), "v1.2.3", "r", "a"};
    const std::filesystem::path folder = temp.Path() / "EnginePrebuilt";

    CHECK(DownloadPrebuiltEngine(engine, folder, nullptr).Ok());
    CHECK_FALSE(std::filesystem::exists(folder / "1.2.3"));

    engine.prebuiltUrl = "file:///" + zip.generic_string();
    engine.prebuiltSha256 = "wrong";
    CHECK_FALSE(DownloadPrebuiltEngine(engine, folder, nullptr).Ok());
    CHECK_FALSE(std::filesystem::exists(folder / "1.2.3"));

    engine.prebuiltSha256 = Sha256FileHex(zip).Value();
    REQUIRE(DownloadPrebuiltEngine(engine, folder, nullptr).Ok());
    CHECK(std::filesystem::is_regular_file(folder / "1.2.3/Prebuilt.json"));

    engine.prebuiltSha256 = "wrong"; // Already there: nothing to download or check
    CHECK(DownloadPrebuiltEngine(engine, folder, nullptr).Ok());
}

TEST_CASE("Only a GitHub repository has a catalog", "[engine-catalog]") {
    CHECK(EngineCatalogUrl("https://github.com/owner/engine.git") ==
          "https://api.github.com/repos/owner/engine/releases?per_page=100");
    CHECK(EngineCatalogUrl("https://example.com/engine.git").empty());
    CHECK(EngineCatalogUrl("").empty());
}

TEST_CASE("The library joins what is here with what can be downloaded", "[engine-catalog]") {
    TempFolder temp;
    WriteFakeEngine(temp.Path() / "Engines/Engine0.3.0", {.version = "0.3.0"});
    WriteFakeEngine(temp.Path() / "Cache/Engine0.2.0", {.version = "0.2.0"});
    WriteFakeEngine(temp.Path() / "Cache/Engine0.3.0", {.version = "0.3.0"}); // The bundled one wins
    EngineLibrary library = EngineLibrary::Scan(temp.Path() / "Engines", temp.Path() / "Cache");
    REQUIRE(library.Engines().size() == 2);
    CHECK(library.Find({0, 3, 0})->folder == temp.Path() / "Engines/Engine0.3.0");

    library.SetOnline(
        {{{0, 4, 0}, "v0.4.0", "r", "a"}, {{0, 3, 0}, "v0.3.0", "r", "a"}, {{0, 1, 0}, "v0.1.0", "r", "a"}});
    REQUIRE(library.Online().size() == 2); // 0.3.0 is here
    CHECK_FALSE(library.FindOnline({0, 3, 0}));
    CHECK(library.FindPublished({0, 3, 0})); // But its compiled engine may still have to be downloaded
    CHECK(library.KnownVersions() == std::vector<EngineVersion>{{0, 4, 0}, {0, 3, 0}, {0, 2, 0}, {0, 1, 0}});
    CHECK(library.UpdateFor({0, 3, 0}) == EngineVersion{0, 4, 0});
    CHECK_FALSE(library.UpdateFor({0, 4, 0}).has_value());
    CHECK(library.Newest()->manifest.version == EngineVersion{0, 3, 0});
}

TEST_CASE("A project can choose any other version; rows offer the newest known", "[engine-catalog]") {
    TempFolder temp;
    WriteFakeEngine(temp.Path() / "Engines/Engine0.3.0", {.version = "0.3.0"});
    EngineLibrary library = EngineLibrary::Scan(temp.Path() / "Engines");
    WriteFakeProject(temp.Path() / "Game", "Game", {.version = "0.3.0"});
    const ProjectEntry entry{temp.Path() / "Game", ""};
    ProjectRow row = InspectProjectRow(entry, library);
    CHECK_FALSE(ActionsFor(row, library, LauncherMode::User).changeEngine);

    library.SetOnline({{{0, 4, 0}, "v0.4.0", "r", "a"}, {{0, 2, 0}, "v0.2.0", "r", "a"}});
    row = InspectProjectRow(entry, library);
    CHECK(row.update == EngineVersion{0, 4, 0});
    CHECK(ActionsFor(row, library, LauncherMode::User).changeEngine);
    CHECK(ChangeChoices(row, library) == std::vector<EngineVersion>{{0, 4, 0}, {0, 2, 0}});
}

TEST_CASE("A copy excludes what it is told to, at any depth given", "[files]") {
    TempFolder temp;
    temp.Write("From/Keep/A.txt", "a\n");
    temp.Write("From/build/B.txt", "b\n");
    temp.Write("From/Keep/build/C.txt", "c\n");
    temp.Write("From/out/deep/D.txt", "d\n");
    REQUIRE(CopyTreeExcept(temp.Path() / "From", temp.Path() / "To", {"build", "out/deep"}).Ok());
    CHECK(temp.Read("To/Keep/A.txt") == "a\n");
    CHECK(temp.Read("To/Keep/build/C.txt") == "c\n"); // Only that path, not every folder of the name
    CHECK_FALSE(std::filesystem::exists(temp.Path() / "To/build"));
    CHECK_FALSE(std::filesystem::exists(temp.Path() / "To/out/deep"));
}
