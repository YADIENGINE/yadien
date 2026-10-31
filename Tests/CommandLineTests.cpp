#include <catch2/catch_test_macros.hpp>
#include "Cli/CommandLine.h"
#include "Cli/EngineCommand.h"
#include "Platform/Text.h"
#include "Support/FakeEngine.h"
#include "Support/TempFolder.h"

TEST_CASE("--project is the launcher's; everything after it goes to the engine unchanged", "[cli]") {
    const ParsedCommandLine spaced = ParseCommandLine({"--project", "C:/Game", "build", "--release"});
    REQUIRE(spaced.project.has_value());
    CHECK(spaced.arguments == std::vector<std::string>{"build", "--release"});

    const ParsedCommandLine joined = ParseCommandLine({"--project=C:/Game", "test", "--project", "x"});
    CHECK(*joined.project == std::filesystem::path("C:/Game"));
    CHECK(joined.arguments == std::vector<std::string>{"test", "--project", "x"});

    CHECK_FALSE(ParseCommandLine({"--project"}).error.empty());
    CHECK_FALSE(ParseCommandLine({"build"}).project.has_value());
}

TEST_CASE("The project is found from a subfolder, or a folder about to run new", "[cli]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "Game", "Game", {});
    std::filesystem::create_directories(temp.Path() / "Game/Game/Scenes");
    CHECK(FindProjectRoot(temp.Path() / "Game/Game/Scenes") == CanonicalRoot(temp.Path() / "Game"));

    WriteFakeEngine(temp.Path() / "Fresh/Engine", {});
    CHECK(FindProjectRoot(temp.Path() / "Fresh") == CanonicalRoot(temp.Path() / "Fresh"));
}

TEST_CASE("Engine commands run the project's engine with the launcher's Python", "[cli]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "Game", "Game", {});
    const Toolchain toolchain{{"3.13.1", "C:/Tools/python/python.exe"},
                              {"3.31.2", "C:/Tools/cmake/bin/cmake.exe"},
                              {"1.12.1", "C:/Tools/ninja/ninja.exe"}};
    Result<ProcessCommand> command = EngineCommand(toolchain, InspectProject(temp.Path() / "Game"), {"build"});
    REQUIRE(command.Ok());
    CHECK(command.Value().executable == toolchain.python.executable);
    CHECK(command.Value().arguments.back() == "build");
    CHECK(command.Value().arguments[2] == PathToUtf8(temp.Path() / "Game" / "Engine"));
    CHECK(command.Value().pathPrefix.size() == 3);

    WriteFakeProject(temp.Path() / "Future", "Future", {.toolsContract = 99});
    CHECK_FALSE(EngineCommand(toolchain, InspectProject(temp.Path() / "Future"), {"build"}).Ok());
    CHECK_FALSE(SupportsToolsContract(0));
}
