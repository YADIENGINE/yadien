#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include "Platform/Process.h"

namespace {
// cmd.exe is on every Windows machine; tests use it as the child program.
ProcessCommand Cmd(const std::string& script) { return {"cmd.exe", {"/d", "/c", script}, {}, {}, {}}; }
} // namespace

TEST_CASE("Arguments are quoted as Windows programs read them", "[process]") {
    const ProcessCommand command{
        "C:/Tools/py.exe", {"plain", "with space", "quote\"inside", "trail\\", ""}, {}, {}, {}};
    CHECK(BuildCommandLine(command) == LR"(C:/Tools/py.exe plain "with space" "quote\"inside" trail\ "")");
    const ProcessCommand spaced{"C:/Program Files/x.exe", {"a\\\"b"}, {}, {}, {}};
    CHECK(BuildCommandLine(spaced) == LR"("C:/Program Files/x.exe" "a\\\"b")");
}

TEST_CASE("Captured output arrives line by line with the exit code", "[process]") {
    std::vector<std::string> lines;
    const ProcessResult result = RunCaptured(Cmd("echo first& echo second 1>&2& exit /b 3"),
                                             [&](const std::string& line) { lines.push_back(line); });
    REQUIRE(result.Started());
    CHECK(result.exitCode == 3);
    CHECK_FALSE(result.Succeeded());
    CHECK(lines == std::vector<std::string>{"first", "second "});
}

TEST_CASE("Environment changes and PATH prefixes reach the child", "[process]") {
    ProcessCommand command = Cmd("echo %LAUNCHER_TEST%& echo %PATH%");
    command.environment = {{"LAUNCHER_TEST", "value"}};
    command.pathPrefix = {"C:\\LauncherPrefix"};
    std::vector<std::string> lines;
    REQUIRE(RunCaptured(command, [&](const std::string& line) { lines.push_back(line); }).Succeeded());
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "value");
    CHECK(lines[1].starts_with("C:\\LauncherPrefix;"));
}

TEST_CASE("A missing program reports why it did not start", "[process]") {
    const ProcessResult result = RunCaptured({"C:/does/not/exist.exe", {}, {}, {}, {}}, nullptr);
    CHECK_FALSE(result.Started());
    CHECK_FALSE(result.startError.empty());
}

TEST_CASE("Canceling ends the child", "[process]") {
    std::atomic<bool> cancel = false;
    std::thread canceler([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        cancel = true;
    });
    const ProcessResult result = RunCaptured(Cmd("ping -n 30 127.0.0.1 >nul"), nullptr, &cancel);
    canceler.join();
    CHECK(result.canceled);
}
