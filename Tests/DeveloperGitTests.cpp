#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <string>
#include "Developer/DeveloperGit.h"
#include "Platform/Text.h"
#include "Support/TempFolder.h"

namespace {
// Local repositories only: an "origin" bare repository stands in for the engine's remote.
void Git(const std::filesystem::path& folder, const std::vector<std::string>& arguments) {
    std::string output;
    const ProcessResult result = RunGit(folder, arguments, [&](const std::string& line) { output += line + "\n"; });
    INFO(output);
    REQUIRE(result.Succeeded());
}

std::string GitOutput(const std::filesystem::path& folder, const std::vector<std::string>& arguments) {
    std::string output;
    (void)RunGit(folder, arguments, [&](const std::string& line) { output += line; });
    return output;
}

// Identity and local submodule transport for the git children of this test process.
void PrepareGitEnvironment() {
    for (const auto& [name, value] :
         {std::pair{L"GIT_AUTHOR_NAME", L"Test"}, std::pair{L"GIT_AUTHOR_EMAIL", L"t@t"},
          std::pair{L"GIT_COMMITTER_NAME", L"Test"}, std::pair{L"GIT_COMMITTER_EMAIL", L"t@t"},
          std::pair{L"GIT_CONFIG_COUNT", L"2"}, std::pair{L"GIT_CONFIG_KEY_0", L"protocol.file.allow"},
          std::pair{L"GIT_CONFIG_VALUE_0", L"always"}, std::pair{L"GIT_CONFIG_KEY_1", L"commit.gpgsign"},
          std::pair{L"GIT_CONFIG_VALUE_1", L"false"}})
        SetEnvironmentVariableW(name, value);
}

struct Repositories {
    TempFolder temp;
    std::filesystem::path origin = temp.Path() / "engine.git";
    std::filesystem::path seed = temp.Path() / "seed"; // Another engine developer's clone
    std::filesystem::path project = temp.Path() / "Game";

    Repositories() {
        PrepareGitEnvironment();
        std::filesystem::create_directories(origin);
        Git(origin, {"init", "--bare", "-b", "main"});
        std::filesystem::create_directories(seed);
        Git(seed, {"init", "-b", "main"});
        temp.Write("seed/EngineManifest.json", R"({"name": "TestEngine", "version": "0.1.0", "toolsContract": 1})");
        Git(seed, {"add", "-A"});
        Git(seed, {"commit", "-m", "Engine"});
        Git(seed, {"remote", "add", "origin", PathToUtf8(origin)});
        Git(seed, {"push", "origin", "main"});

        std::filesystem::create_directories(project);
        REQUIRE(InitDevelopmentRepository(project, PathToUtf8(origin), "main", nullptr).Ok());
        temp.Write("Game/Project.json", R"({"version": 1, "name": "Game"})");
        REQUIRE(CommitNewProject(project, "Game", nullptr).Ok());
    }

    void PushFromSeed(const std::string& content) {
        temp.Write("seed/Change.txt", content);
        Git(seed, {"add", "-A"});
        Git(seed, {"commit", "-m", "Seed change"});
        Git(seed, {"pull", "--rebase", "origin", "main"});
        Git(seed, {"push", "origin", "main"});
    }
};

bool GitAvailable() { return RunGit(std::filesystem::temp_directory_path(), {"--version"}, nullptr).Succeeded(); }
} // namespace

TEST_CASE("A developer project starts with the engine submodule on its branch", "[git]") {
    if (!GitAvailable()) SKIP("git is not installed");
    Repositories repositories;
    Result<EngineGitStatus> status = ReadEngineGitStatus(repositories.project);
    REQUIRE(status.Ok());
    CHECK(status.Value().branch == "main");
    CHECK(status.Value().trackedBranch == "main");
    CHECK(status.Value().changedFiles == 0);
    CHECK_FALSE(status.Value().pointerChanged);
    CHECK(GitOutput(repositories.project, {"log", "-1", "--format=%s"}) == "Create Game");
}

TEST_CASE("Publishing commits and pushes the engine, then commits the pointer", "[git]") {
    if (!GitAvailable()) SKIP("git is not installed");
    Repositories repositories;
    repositories.temp.Write("Game/Engine/New.txt", "engine work\n");
    REQUIRE(ReadEngineGitStatus(repositories.project).Value().changedFiles == 1);

    REQUIRE(PublishEngineChanges(repositories.project, "Add new file", nullptr).Ok());
    CHECK(GitOutput(repositories.origin, {"log", "-1", "--format=%s", "main"}) == "Add new file");
    CHECK(GitOutput(repositories.project, {"log", "-1", "--format=%s"}) == "Update Engine: Add new file");
    const EngineGitStatus after = ReadEngineGitStatus(repositories.project).Value();
    CHECK(after.changedFiles == 0);
    CHECK_FALSE(after.pointerChanged);
    CHECK_FALSE(PublishEngineChanges(repositories.project, "  ", nullptr).Ok());
}

TEST_CASE("Updating fixes a detached engine and commits the new pointer", "[git]") {
    if (!GitAvailable()) SKIP("git is not installed");
    Repositories repositories;
    const std::filesystem::path engine = repositories.project / "Engine";
    Git(engine, {"checkout", "--detach"});
    repositories.PushFromSeed("remote work\n");

    REQUIRE(UpdateEngineFromRemote(repositories.project, nullptr).Ok());
    const EngineGitStatus status = ReadEngineGitStatus(repositories.project).Value();
    CHECK(status.branch == "main");
    CHECK(status.head == GitOutput(repositories.origin, {"rev-parse", "main"}));
    CHECK(GitOutput(repositories.project, {"log", "-1", "--format=%s"}) == "Update Engine");
}

TEST_CASE("Updating refuses uncommitted engine changes", "[git]") {
    if (!GitAvailable()) SKIP("git is not installed");
    Repositories repositories;
    repositories.temp.Write("Game/Engine/EngineManifest.json", "{}");
    CHECK_FALSE(UpdateEngineFromRemote(repositories.project, nullptr).Ok());
}
