#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include "Engines/EngineLibrary.h"
#include "Projects/NewProject.h"
#include "Projects/ProjectInfo.h"
#include "Support/FakeEngine.h"
#include "Support/TempFolder.h"

namespace {
// Stands in for the engine's `new`: writes Project.json, or fails when told to.
EngineRunner FakeNew(bool succeed, std::vector<std::string>& calls) {
    return [succeed, &calls](const std::filesystem::path& root, const std::vector<std::string>& arguments,
                             const OutputLine&) {
        calls.insert(calls.end(), arguments.begin(), arguments.end());
        if (succeed) std::ofstream(root / "Project.json") << R"({"version": 1, "name": ")" + arguments.back() + "\"}";
        return ProcessResult{succeed ? 0 : 1, false, ""};
    };
}

AvailableEngine Stable(const std::filesystem::path& engines) {
    WriteFakeEngine(engines / "Engine0.2", {.version = "0.2.0"});
    return *EngineLibrary::Scan(engines).Newest();
}
} // namespace

TEST_CASE("A new project gets a copy of the engine, then the engine's own new", "[projects]") {
    TempFolder temp;
    std::vector<std::string> calls;
    const NewProjectRequest request{"MyGame", temp.Path() / "Projects", Stable(temp.Path() / "Engines")};
    Result<std::filesystem::path> root = CreateProject(request, FakeNew(true, calls), nullptr);
    REQUIRE(root.Ok());
    CHECK(calls == std::vector<std::string>{"new", "--name", "MyGame"});
    const ProjectInfo info = InspectProject(root.Value());
    CHECK(info.kind == EngineKind::Copy);
    CHECK(info.name == "MyGame");
    CHECK_FALSE(std::filesystem::exists(root.Value() / ".git")); // User mode never touches git
}

TEST_CASE("A failed new project leaves the location as it was", "[projects]") {
    TempFolder temp;
    std::vector<std::string> calls;
    const NewProjectRequest request{"MyGame", temp.Path(), Stable(temp.Path() / "Engines")};
    CHECK_FALSE(CreateProject(request, FakeNew(false, calls), nullptr).Ok());
    CHECK_FALSE(std::filesystem::exists(temp.Path() / "MyGame"));

    std::filesystem::create_directories(temp.Path() / "Empty");
    const NewProjectRequest intoEmpty{"Empty", temp.Path(), Stable(temp.Path() / "Engines")};
    CHECK_FALSE(CreateProject(intoEmpty, FakeNew(false, calls), nullptr).Ok());
    CHECK(std::filesystem::is_empty(temp.Path() / "Empty"));
}

TEST_CASE("New projects need an empty or new folder with a usable name", "[projects]") {
    TempFolder temp;
    temp.Write("Taken/file.txt", "x");
    const AvailableEngine engine = Stable(temp.Path() / "Engines");
    CHECK_FALSE(CheckNewProjectFolder({"Taken", temp.Path(), engine}).Ok());
    CHECK_FALSE(CheckNewProjectFolder({"a/b", temp.Path(), engine}).Ok());
    CHECK_FALSE(CheckNewProjectFolder({"", temp.Path(), engine}).Ok());
    CHECK(CheckNewProjectFolder({"Fresh", temp.Path(), engine}).Ok());
}
