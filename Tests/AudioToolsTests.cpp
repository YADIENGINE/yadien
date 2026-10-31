#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>
#include "Platform/Crypto.h"
#include "Platform/KnownFolders.h"
#include "Platform/Process.h"
#include "Platform/Text.h"
#include "Support/TempFolder.h"
#include "System/AudioTools.h"
#include "System/Toolchain.h"

namespace {
// A wheel is a zip: Windows' tar.exe writes one from a folder (as the installer extracts it with the same tool).
AudioPackage MakeWheel(const TempFolder& temp, const std::string& name, const std::string& version) {
    temp.Write("source-" + name + "/" + name + "/__init__.py", "VERSION = '" + version + "'\n");
    const std::filesystem::path wheel = temp.Path() / (name + ".zip");
    const ProcessCommand zip{
        SystemFolder() / "tar.exe",
        {"-a", "-c", "-f", PathToUtf8(wheel), "-C", PathToUtf8(temp.Path() / ("source-" + name)), name}};
    REQUIRE(RunCaptured(zip, [](const std::string&) {}).Succeeded());
    return {name, version, "file:///" + wheel.generic_string(), Sha256FileHex(wheel).Value()};
}
} // namespace

TEST_CASE("Audio tools are installed, recognised and removed", "[audio]") {
    TempFolder temp;
    const std::filesystem::path folder = temp.Path() / "AudioTools";
    const std::vector<AudioPackage> packages{MakeWheel(temp, "alpha", "1.0"), MakeWheel(temp, "beta", "2.0")};
    CHECK(InspectAudioTools(folder, {}) == AudioToolsState::NotOffered);
    CHECK(InspectAudioTools(folder, packages) == AudioToolsState::NotInstalled);

    std::vector<std::string> lines;
    REQUIRE(InstallAudioTools(folder, packages, [&](const std::string& line) { lines.push_back(line); }).Ok());
    CHECK(InspectAudioTools(folder, packages) == AudioToolsState::Installed);
    CHECK(std::filesystem::is_regular_file(folder / "site" / "alpha" / "__init__.py"));
    CHECK(std::filesystem::is_regular_file(folder / "site" / "beta" / "__init__.py"));
    CHECK_FALSE(std::filesystem::exists(folder / "downloads"));
    CHECK_FALSE(lines.empty());

    std::vector<AudioPackage> newer = packages;
    newer[1].version = "3.0";
    CHECK(InspectAudioTools(folder, newer) == AudioToolsState::Outdated);

    REQUIRE(RemoveAudioTools(folder).Ok());
    CHECK(InspectAudioTools(folder, packages) == AudioToolsState::NotInstalled);
}

TEST_CASE("A download that does not match its hash leaves the installed tools alone", "[audio]") {
    TempFolder temp;
    const std::filesystem::path folder = temp.Path() / "AudioTools";
    const std::vector<AudioPackage> packages{MakeWheel(temp, "alpha", "1.0")};
    REQUIRE(InstallAudioTools(folder, packages, [](const std::string&) {}).Ok());

    std::vector<AudioPackage> tampered = packages;
    tampered[0].version = "1.1";
    tampered[0].sha256 = std::string(64, '0');
    CHECK_FALSE(InstallAudioTools(folder, tampered, [](const std::string&) {}).Ok());
    CHECK(InspectAudioTools(folder, packages) == AudioToolsState::Installed);
    std::filesystem::path staging = folder;
    staging += L".installing";
    CHECK_FALSE(std::filesystem::exists(staging));
}

TEST_CASE("Python scripts run with the bundled Python and the audio tools on sys.path", "[audio]") {
    Toolchain toolchain;
    toolchain.python.executable = "C:/Tools/python/python.exe";
    toolchain.cmake.executable = "C:/Tools/cmake/bin/cmake.exe";
    toolchain.ninja.executable = "C:/Tools/ninja/ninja.exe";
    const ProcessCommand command = PythonScriptCommand(toolchain, "C:/Data/AudioTools", {"Sounds/Robot.py", "--quick"});
    CHECK(command.executable == toolchain.python.executable);
    const std::vector<std::string>& arguments = command.arguments;
    REQUIRE(arguments.size() == 7);
    CHECK(arguments[2] == "-c");
    CHECK(Utf8ToPath(arguments[4]) == std::filesystem::path("C:/Data/AudioTools") / "site");
    CHECK(arguments[5] == "Sounds/Robot.py");
    CHECK(arguments[6] == "--quick");
}

TEST_CASE("The toolchain lists the audio packages it offers", "[audio]") {
    TempFolder temp;
    temp.Write("Toolchain.json", R"({"format": 1,
        "tools": {"python": {"version": "3.13.1", "executable": "python/python.exe"},
                  "cmake": {"version": "3.31.2", "executable": "cmake/bin/cmake.exe"},
                  "ninja": {"version": "1.12.1", "executable": "ninja/ninja.exe"}},
        "audio": {"numpy": {"version": "2.5.4", "url": "https://example.invalid/numpy.whl", "sha256": "ab"}}})");
    Result<Toolchain> toolchain = LoadToolchain(temp.Path());
    REQUIRE(toolchain.Ok());
    REQUIRE(toolchain.Value().audio.size() == 1);
    CHECK(toolchain.Value().audio[0].name == "numpy");
    CHECK(toolchain.Value().audio[0].version == "2.5.4");

    temp.Write("Toolchain.json", R"({"format": 1,
        "tools": {"python": {"version": "3.13.1", "executable": "python/python.exe"},
                  "cmake": {"version": "3.31.2", "executable": "cmake/bin/cmake.exe"},
                  "ninja": {"version": "1.12.1", "executable": "ninja/ninja.exe"}},
        "audio": {"numpy": {"version": "2.5.4"}}})");
    CHECK_FALSE(LoadToolchain(temp.Path()).Ok());
}
