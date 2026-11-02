#include <catch2/catch_test_macros.hpp>
#include "Engines/EngineLibrary.h"
#include "Engines/EngineManifest.h"
#include "Engines/EngineVersion.h"
#include "Platform/FileSystem.h"
#include "Support/FakeEngine.h"
#include "Support/TempFolder.h"

TEST_CASE("A downloaded engine can be removed, the bundled one cannot", "[engines]") {
    TempFolder temp;
    WriteFakeEngine(temp.Path() / "Bundled" / "Engine0.1.0", FakeEngine{});
    WriteFakeEngine(temp.Path() / "Cache" / "Engine0.3.0",
                    FakeEngine{.version = "0.3.0", .history = {"0.3.0", "0.1.0"}});
    const EngineLibrary library = EngineLibrary::Scan(temp.Path() / "Bundled", temp.Path() / "Cache");
    const AvailableEngine* bundled = library.Find({0, 1, 0});
    const AvailableEngine* downloaded = library.Find({0, 3, 0});
    REQUIRE(bundled);
    REQUIRE(downloaded);
    CHECK_FALSE(bundled->downloaded);
    CHECK(downloaded->downloaded);
    CHECK_FALSE(RemoveDownloadedEngine(*bundled).Ok());
    CHECK(std::filesystem::exists(bundled->folder));
    CHECK(RemoveDownloadedEngine(*downloaded).Ok());
    CHECK_FALSE(std::filesystem::exists(downloaded->folder));
    CHECK_FALSE(EngineLibrary::Scan(temp.Path() / "Bundled", temp.Path() / "Cache").Find({0, 3, 0}));
}

TEST_CASE("A folder's size adds up the files in its whole tree", "[engines]") {
    TempFolder temp;
    temp.Write("Tree/a.txt", "123");
    temp.Write("Tree/Nested/b.txt", "4567");
    const Result<std::uintmax_t> size = FolderSize(temp.Path() / "Tree");
    REQUIRE(size.Ok());
    CHECK(size.Value() == 7);
    CHECK_FALSE(FolderSize(temp.Path() / "Missing").Ok());
}

TEST_CASE("Versions parse as three numbers without leading zeros", "[engines]") {
    REQUIRE(EngineVersion::Parse("0.10.2") == EngineVersion{0, 10, 2});
    for (const char* text : {"1.2", "01.2.3", "1.2.3-beta", "", "1..3", "a.b.c"})
        CHECK_FALSE(EngineVersion::Parse(text).has_value());
    CHECK(EngineVersion{0, 9, 0} < EngineVersion{0, 10, 0});
    CHECK(EngineVersion{0, 2, 3}.Series() == "0.2");
}

TEST_CASE("Before 1.0 a minor step may break, after it a major step", "[engines]") {
    CHECK(MayBreak({0, 1, 0}, {0, 2, 0}));
    CHECK_FALSE(MayBreak({0, 2, 0}, {0, 2, 5}));
    CHECK(MayBreak({0, 9, 0}, {1, 0, 0}));
    CHECK_FALSE(MayBreak({1, 0, 0}, {1, 4, 0}));
    CHECK(MayBreak({1, 4, 0}, {2, 0, 0}));
    CHECK_FALSE(MayBreak({1, 4, 0}, {1, 4, 0}));
}

TEST_CASE("An update shows the notes of every version it brings, skipped ones included", "[engines]") {
    TempFolder temp;
    WriteFakeEngine(temp.Path(), {.version = "0.3.0", .history = {"0.3.0", "0.2.1", "0.2.0", "0.1.0"}});
    Result<EngineManifest> manifest = LoadEngineManifest(temp.Path());
    REQUIRE(manifest.Ok());
    const std::vector<EngineNote> notes = NotesSince(manifest.Value(), {0, 2, 0});
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].version == EngineVersion{0, 3, 0});
    CHECK(notes[1].version == EngineVersion{0, 2, 1});
}

TEST_CASE("Notes come from one file per version, newest first by number", "[engines]") {
    TempFolder temp;
    WriteFakeEngine(temp.Path(), {.version = "0.10.0"});
    temp.Write("ReleaseNotes/0.9.0.json", R"({"date": "2026-01-01", "changes": ["Older"], "breaking": []})");
    temp.Write("ReleaseNotes/0.10.0.json",
               R"({"date": "2026-01-02", "changes": ["Newer"],
                   "breaking": [{"change": "Renamed X", "migrate": "Use Y"}]})");
    temp.Write("ReleaseNotes/draft.json", R"({"changes": ["Not a version"]})");
    Result<EngineManifest> manifest = LoadEngineManifest(temp.Path());
    REQUIRE(manifest.Ok());
    REQUIRE(manifest.Value().notes.size() == 2);
    CHECK(manifest.Value().notes[0].version == EngineVersion{0, 10, 0});
    CHECK(manifest.Value().notes[0].changes == std::vector<std::string>{"Newer"});
    REQUIRE(manifest.Value().notes[0].breaking.size() == 1);
    CHECK(manifest.Value().notes[0].breaking[0].migrate == "Use Y");
    CHECK(manifest.Value().notes[1].version == EngineVersion{0, 9, 0});
}

TEST_CASE("A manifest without identity is rejected", "[engines]") {
    TempFolder temp;
    temp.Write("EngineManifest.json", R"({"name": "X", "version": "1.0"})");
    CHECK_FALSE(LoadEngineManifest(temp.Path()).Ok());
}

TEST_CASE("The library lists valid versions newest first and reports broken folders", "[engines]") {
    TempFolder temp;
    WriteFakeEngine(temp.Path() / "Engine0.1.2", {.version = "0.1.2"});
    WriteFakeEngine(temp.Path() / "Engine0.2.0", {.version = "0.2.0"});
    WriteFakeEngine(temp.Path() / "Engine0.3.0", {.version = "0.4.0"}); // Folder and version disagree
    temp.Write("Engine0.5.0/EngineManifest.json", "{}");
    const EngineLibrary library = EngineLibrary::Scan(temp.Path());
    REQUIRE(library.Engines().size() == 2);
    CHECK(library.Newest()->manifest.version == EngineVersion{0, 2, 0});
    CHECK(library.Find({0, 1, 2}) != nullptr);
    CHECK(library.Find({0, 3, 0}) == nullptr);
    CHECK(library.Problems().size() == 2);
    CHECK(library.UpdateFor({0, 1, 2}) == EngineVersion{0, 2, 0});
    CHECK_FALSE(library.UpdateFor({0, 2, 0}).has_value());
}
