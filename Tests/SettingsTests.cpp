#include <catch2/catch_test_macros.hpp>
#include "Platform/Text.h"
#include "Settings/LauncherLayout.h"
#include "Settings/LauncherSettings.h"
#include "Settings/ProjectList.h"
#include "Support/TempFolder.h"

TEST_CASE("Settings survive a save and load; a missing file is a first start", "[settings]") {
    TempFolder temp;
    const std::filesystem::path file = temp.Path() / "Data/Settings.json";
    Result<LauncherSettings> first = LoadLauncherSettings(file);
    REQUIRE(first.Ok());
    CHECK(first.Value().mode == LauncherMode::User);
    CHECK(first.Value().projects.empty());

    LauncherSettings settings;
    settings.mode = LauncherMode::Developer;
    settings.uiScale = 1.25f;
    settings.projectsFolder = temp.Path() / Utf8ToPath("Projekte ğüş");
    settings.projects.push_back({temp.Path() / "Oyun", "2026-10-31T10:00:00Z"});
    REQUIRE(SaveLauncherSettings(file, settings).Ok());

    Result<LauncherSettings> loaded = LoadLauncherSettings(file);
    REQUIRE(loaded.Ok());
    CHECK(loaded.Value().mode == LauncherMode::Developer);
    CHECK(loaded.Value().uiScale == 1.25f);
    CHECK(loaded.Value().projectsFolder == settings.projectsFolder);
    REQUIRE(loaded.Value().projects.size() == 1);
    CHECK(loaded.Value().projects[0].root == settings.projects[0].root);
    CHECK(loaded.Value().projects[0].lastOpened == "2026-10-31T10:00:00Z");
}

TEST_CASE("A broken settings file is an error, not a silent reset", "[settings]") {
    TempFolder temp;
    temp.Write("Settings.json", "{ not json");
    CHECK_FALSE(LoadLauncherSettings(temp.Path() / "Settings.json").Ok());
}

TEST_CASE("Only project folders are added, once; removing leaves the disk alone", "[settings]") {
    TempFolder temp;
    temp.Write("Game/Project.json", R"({"version": 1})");
    LauncherSettings settings;
    CHECK_FALSE(AddProject(settings, temp.Path()).Ok()); // No Project.json
    REQUIRE(AddProject(settings, temp.Path() / "Game").Ok());
    CHECK_FALSE(AddProject(settings, temp.Path() / "Game" / ".").Ok()); // Same folder, other spelling
    REQUIRE(RemoveProject(settings, temp.Path() / "Game").Ok());
    CHECK(settings.projects.empty());
    CHECK(std::filesystem::exists(temp.Path() / "Game/Project.json"));
}

TEST_CASE("Recent projects: last opened first, never opened last", "[settings]") {
    LauncherSettings settings;
    settings.projects = {{"C:/B", ""}, {"C:/Old", "2026-01-01T00:00:00Z"}, {"C:/New", "2026-05-01T00:00:00Z"}};
    MarkProjectOpened(settings, "C:/Old", "2026-06-01T00:00:00Z");
    const std::vector<ProjectEntry> recent = RecentProjects(settings);
    CHECK(PathToUtf8(recent[0].root).ends_with("Old"));
    CHECK(PathToUtf8(recent[1].root).ends_with("New"));
    CHECK(PathToUtf8(recent[2].root).ends_with("B"));
    CHECK(CurrentTimeIso().size() == std::string("2026-10-31T18:20:00Z").size());
}

TEST_CASE("The layout is beside the executable unless a development layout file says otherwise", "[settings]") {
    TempFolder temp;
    Result<LauncherLayout> installed = ResolveLauncherLayout(temp.Path());
    REQUIRE(installed.Ok());
    CHECK(installed.Value().enginesFolder == temp.Path() / "Engines");
    CHECK(installed.Value().SettingsFile() == temp.Path() / "Settings.json");

    temp.Write("LauncherLayout.json",
               R"({"engines": "C:/Repo/Engines", "tools": "C:/Repo/ToolCache", "data": "C:/B"})");
    Result<LauncherLayout> development = ResolveLauncherLayout(temp.Path());
    REQUIRE(development.Ok());
    CHECK(development.Value().toolsFolder == std::filesystem::path("C:/Repo/ToolCache"));
}
