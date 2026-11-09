#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "Engines/EngineLibrary.h"
#include "Projects/CopyIntegrity.h"
#include "Projects/EngineUpdate.h"
#include "Projects/ProjectEngineVersion.h"
#include "Projects/ProjectInfo.h"
#include "Support/FakeEngine.h"
#include "Support/TempFolder.h"

namespace {
bool Contains(const std::vector<std::string>& list, const std::string& item) {
    return std::ranges::find(list, item) != list.end();
}

AvailableEngine Stable(const std::filesystem::path& engines, const FakeEngine& engine) {
    WriteFakeEngine(engines / ("Engine" + engine.version), engine);
    const EngineLibrary library = EngineLibrary::Scan(engines);
    return *library.Find(*EngineVersion::Parse(engine.version));
}
} // namespace

TEST_CASE("The project's engine kind comes from its files", "[projects]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "Copy", "CopyGame", {});
    CHECK(InspectProject(temp.Path() / "Copy").kind == EngineKind::Copy);
    CHECK(InspectProject(temp.Path() / "Copy").name == "CopyGame");

    WriteFakeProject(temp.Path() / "Git", "GitGame", {});
    std::filesystem::remove(temp.Path() / "Git/Engine/EngineCopy.json");
    temp.Write("Git/Engine/.git", "gitdir: ../.git/modules/Engine\n");
    CHECK(InspectProject(temp.Path() / "Git").kind == EngineKind::Git);

    WriteFakeProject(temp.Path() / "Loose", "LooseGame", {});
    std::filesystem::remove(temp.Path() / "Loose/Engine/EngineCopy.json");
    CHECK(InspectProject(temp.Path() / "Loose").kind == EngineKind::Unregistered);

    temp.Write("Empty/Project.json", R"({"version": 1, "name": "EmptyGame"})");
    const ProjectInfo empty = InspectProject(temp.Path() / "Empty");
    CHECK(empty.kind == EngineKind::Missing);
    CHECK_FALSE(empty.problem.empty());
}

TEST_CASE("A copy reports edited, missing and added engine files", "[projects]") {
    TempFolder temp;
    WriteFakeProject(temp.Path(), "Game", {.files = {{"Core/A.h", "a\n"}, {"Core/B.h", "b\n"}}});
    const std::filesystem::path engine = temp.Path() / "Engine";
    Result<EngineCopyRecord> record = LoadEngineCopyRecord(engine);
    REQUIRE(record.Ok());
    REQUIRE(CheckCopyIntegrity(engine, record.Value()).Value().Clean());

    temp.Write("Engine/Core/A.h", "edited\n");
    std::filesystem::remove(engine / "Core/B.h");
    temp.Write("Engine/Core/Mine.h", "mine\n");
    temp.Write("Engine/Tools/__pycache__/cli.pyc", "generated");
    const CopyIntegrity integrity = CheckCopyIntegrity(engine, record.Value()).Value();
    CHECK(integrity.modified == std::vector<std::string>{"Core/A.h"});
    CHECK(integrity.missing == std::vector<std::string>{"Core/B.h"});
    CHECK(integrity.added == std::vector<std::string>{"Core/Mine.h"});
}

TEST_CASE("An update lists lost edits and user files, and the notes it brings", "[projects]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "Game", "Game",
                     {.version = "0.1.0", .files = {{"Core/A.h", "a1\n"}, {"Core/Old.h", "old\n"}}});
    temp.Write("Game/Engine/Core/A.h", "my edit\n");
    temp.Write("Game/Engine/Core/Mine.h", "mine\n");
    const AvailableEngine target = Stable(
        temp.Path() / "Engines",
        {.version = "0.2.0", .files = {{"Core/A.h", "a2\n"}}, .history = {"0.2.0", "0.1.0"}, .breakingIn = {"0.2.0"}});

    Result<EngineUpdatePlan> plan = PlanEngineUpdate(InspectProject(temp.Path() / "Game"), target);
    REQUIRE(plan.Ok());
    CHECK(plan.Value().from == EngineVersion{0, 1, 0});
    CHECK(plan.Value().overwritten == std::vector<std::string>{"Core/A.h"});
    CHECK(plan.Value().deleted == std::vector<std::string>{"Core/Mine.h"}); // Core/Old.h was the engine's
    REQUIRE(plan.Value().notes.size() == 1);
    CHECK(plan.Value().mayBreak);

    REQUIRE(ApplyEngineUpdate(plan.Value()).Ok());
    const ProjectInfo updated = InspectProject(temp.Path() / "Game");
    CHECK(updated.manifest->version == EngineVersion{0, 2, 0});
    CHECK(temp.Read("Game/Engine/Core/A.h") == "a2\n");
    CHECK_FALSE(std::filesystem::exists(temp.Path() / "Game/Engine/Core/Mine.h"));
    CHECK_FALSE(std::filesystem::exists(temp.Path() / "Game/.Engine.old"));
    Result<EngineCopyRecord> record = LoadEngineCopyRecord(updated.EngineFolder());
    CHECK(CheckCopyIntegrity(updated.EngineFolder(), record.Value()).Value().Clean());
}

TEST_CASE("Updates never touch git engines", "[projects]") {
    TempFolder temp;
    WriteFakeProject(temp.Path() / "Git", "Git", {});
    std::filesystem::remove(temp.Path() / "Git/Engine/EngineCopy.json");
    temp.Write("Git/Engine/.git", "gitdir: x\n");
    const AvailableEngine newer = Stable(temp.Path() / "Engines", {.version = "0.4.0"});
    CHECK_FALSE(PlanEngineUpdate(InspectProject(temp.Path() / "Git"), newer).Ok());
}

TEST_CASE("The same version repairs an edited copy", "[projects]") {
    TempFolder temp;
    const FakeEngine engine{.version = "0.2.0", .files = {{"Core/A.h", "a\n"}}};
    WriteFakeProject(temp.Path() / "Game", "Game", engine);
    temp.Write("Game/Engine/Core/A.h", "broken\n");
    const AvailableEngine same = Stable(temp.Path() / "Engines", engine);
    Result<EngineUpdatePlan> plan = PlanEngineUpdate(InspectProject(temp.Path() / "Game"), same);
    REQUIRE(plan.Ok());
    CHECK_FALSE(plan.Value().mayBreak);
    REQUIRE(ApplyEngineUpdate(plan.Value()).Ok());
    CHECK(temp.Read("Game/Engine/Core/A.h") == "a\n");
}

TEST_CASE("An unregistered or missing engine can be replaced with a stable copy", "[projects]") {
    TempFolder temp;
    temp.Write("Game/Project.json", R"({"version": 1, "name": "Game"})");
    const AvailableEngine target = Stable(temp.Path() / "Engines", {.version = "0.2.0"});
    Result<EngineUpdatePlan> plan = PlanEngineUpdate(InspectProject(temp.Path() / "Game"), target);
    REQUIRE(plan.Ok());
    CHECK_FALSE(plan.Value().from.has_value());
    CHECK(plan.Value().mayBreak);
    REQUIRE(ApplyEngineUpdate(plan.Value()).Ok());
    CHECK(InspectProject(temp.Path() / "Game").kind == EngineKind::Copy);
    CHECK(InspectProject(temp.Path() / "Game").recorded == EngineVersion{0, 2, 0}); // A clone can restore it
}

TEST_CASE("A missing engine that Project.json names starts from that version", "[projects]") {
    TempFolder temp;
    temp.Write("Game/Project.json", R"({"version": 1, "name": "Game", "engine": {"version": "0.2.0"}})");
    const AvailableEngine same = Stable(temp.Path() / "Engines", {.version = "0.2.0", .breakingIn = {"0.2.0"}});
    Result<EngineUpdatePlan> plan = PlanEngineUpdate(InspectProject(temp.Path() / "Game"), same);
    REQUIRE(plan.Ok());
    CHECK(plan.Value().from == EngineVersion{0, 2, 0});
    CHECK_FALSE(plan.Value().mayBreak);
    CHECK(plan.Value().notes.empty());
}

TEST_CASE("Recording the engine version changes only that value in Project.json", "[projects]") {
    TempFolder temp;
    const EngineVersion version{0, 12, 0};
    const auto recorded = [&](const std::string& before) {
        temp.Write("Game/Project.json", before);
        REQUIRE(RecordProjectEngine(temp.Path() / "Game", version).Ok());
        return temp.Read("Game/Project.json");
    };
    // The template's inline objects stay inline; a new section goes last with the file's indent
    CHECK(recorded("{\n  \"version\": 1,\n  \"game\": { \"buildPresets\": { \"Debug\": \"debug\" } }\n}\n") ==
          "{\n  \"version\": 1,\n  \"game\": { \"buildPresets\": { \"Debug\": \"debug\" } },\n"
          "  \"engine\": { \"version\": \"0.12.0\" }\n}\n");
    CHECK(recorded("{\n  \"name\": \"A\",\n  \"engine\": { \"version\": \"0.11.1\" }\n}\n") ==
          "{\n  \"name\": \"A\",\n  \"engine\": { \"version\": \"0.12.0\" }\n}\n");
    CHECK(recorded("{ \"engine\": { \"build\": \"source\" } }") ==
          "{ \"engine\": { \"version\": \"0.12.0\", \"build\": \"source\" } }");
    CHECK(recorded("{ \"engine\": {} }") == "{ \"engine\": { \"version\": \"0.12.0\" } }");
    CHECK(recorded("{ \"engine\": \"x\" }") == "{ \"engine\": { \"version\": \"0.12.0\" } }");
    CHECK(recorded("{}") == "{\n  \"engine\": { \"version\": \"0.12.0\" }\n}");
    CHECK(recorded("{\r\n  \"name\": \"A\"\r\n}\r\n") ==
          "{\r\n  \"name\": \"A\",\r\n  \"engine\": { \"version\": \"0.12.0\" }\r\n}\r\n");
    CHECK(recorded("{ \"name\": \"a \\\" engine\", \"engine\": { \"version\": \"0.12.0\" } }") ==
          "{ \"name\": \"a \\\" engine\", \"engine\": { \"version\": \"0.12.0\" } }"); // Already named: kept

    temp.Write("Broken/Project.json", "[1]");
    CHECK_FALSE(RecordProjectEngine(temp.Path() / "Broken", version).Ok());
}
