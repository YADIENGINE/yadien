#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <sstream>
#include "Cli/CommandLine.h"
#include "Cli/EngineCommand.h"
#include "Platform/Text.h"
#include "Settings/ProjectList.h"
#include "Support/FakeEngine.h"
#include "Support/TempFolder.h"

TEST_CASE("The project option is the launcher's; everything after it goes to the engine unchanged", "[cli]") {
    const ParsedCommandLine spaced = ParseCommandLine({"--project", "C:/Game", "build", "--release"});
    REQUIRE(spaced.project.has_value());
    CHECK(spaced.arguments == std::vector<std::string>{"build", "--release"});

    const ParsedCommandLine joined = ParseCommandLine({"--project=C:/Game", "test", "--project", "x"});
    CHECK(*joined.project == std::filesystem::path("C:/Game"));
    CHECK(joined.arguments == std::vector<std::string>{"test", "--project", "x"});

    CHECK_FALSE(ParseCommandLine({"--project"}).error.empty());
    CHECK_FALSE(ParseCommandLine({"build"}).project.has_value());
}

TEST_CASE("The windowed program opens only without a command", "[cli]") {
    constexpr std::string_view WindowArgument = "--window-only";
    CHECK_FALSE(WantsConsoleProgram({}, WindowArgument));
    CHECK_FALSE(WantsConsoleProgram({"--window-only"}, WindowArgument));
    CHECK(WantsConsoleProgram({"build"}, WindowArgument));
    CHECK(WantsConsoleProgram({"--help"}, WindowArgument));
    CHECK(WantsConsoleProgram({"--window-only", "build"}, WindowArgument));
}

TEST_CASE("A command goes unchanged to the console program beside the window", "[cli]") {
    const ProcessCommand command = ConsoleProgramCommand("C:/Tools/engine.exe", {"test", "--label", "^session"});
    CHECK(command.executable == std::filesystem::path("C:/Tools/engine.com"));
    CHECK(command.arguments == std::vector<std::string>{"test", "--label", "^session"});
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

    WriteFakeProject(temp.Path() / "Current", "Current", {.toolsContract = 2});
    CHECK(EngineCommand(toolchain, InspectProject(temp.Path() / "Current"), {"build"}).Ok());

    WriteFakeProject(temp.Path() / "Future", "Future", {.toolsContract = 99});
    CHECK_FALSE(EngineCommand(toolchain, InspectProject(temp.Path() / "Future"), {"build"}).Ok());
    CHECK_FALSE(SupportsToolsContract(0));
}

TEST_CASE("A project on an engine copy finds the compiled engine of its version", "[cli]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "Game", "Game", {.version = "1.2.3"});
    ProjectInfo project = InspectProject(temp.Path() / "Game");
    project.kind = EngineKind::Copy;
    const std::vector<std::filesystem::path> roots{temp.Path() / "Beside", temp.Path() / "Data"};
    CHECK(PrebuiltEngineFolder(roots, project).empty());

    std::filesystem::create_directories(temp.Path() / "Beside/9.9.9");
    CHECK(PrebuiltEngineFolder(roots, project).empty());

    std::filesystem::create_directories(temp.Path() / "Data/1.2.3");
    CHECK(PrebuiltEngineFolder(roots, project) == temp.Path() / "Data/1.2.3");

    std::filesystem::create_directories(temp.Path() / "Beside/1.2.3");
    CHECK(PrebuiltEngineFolder(roots, project) == temp.Path() / "Beside/1.2.3");

    project.kind = EngineKind::Git;
    CHECK(PrebuiltEngineFolder(roots, project).empty());
}

TEST_CASE("launcher version lists every engine the launcher carries", "[cli]") {
    TempFolder temp;
    WriteFakeEngine(temp.Path() / "Engines/Engine0.1.1", {.version = "0.1.1"});
    WriteFakeEngine(temp.Path() / "Engines/Engine0.2.0", {.version = "0.2.0"});
    const LauncherLayout layout{temp.Path() / "Engines", temp.Path() / "Tools", temp.Path()};
    std::ostringstream captured;
    std::streambuf* const original = std::cout.rdbuf(captured.rdbuf());
    const int exitCode = RunCommandLine({"launcher", "version"}, layout, temp.Path());
    std::cout.rdbuf(original);
    CHECK(exitCode == 0);
    CHECK(captured.str().find("engine 0.1.1") != std::string::npos);
    CHECK(captured.str().find("engine 0.2.0") != std::string::npos);
}
