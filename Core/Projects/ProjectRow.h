#pragma once

#include "Engines/EngineLibrary.h"
#include "Engines/EngineVersion.h"
#include "Projects/ProjectInfo.h"
#include "Settings/LauncherSettings.h"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// One row of the Projects list, ready to show (design D15): everything comes from files, nothing is hashed or run.
// The slow facts (engine file check, git status) arrive later as ProjectDetails.
struct ProjectRow {
    ProjectEntry entry;
    ProjectInfo info;
    bool folderMissing = false;          // The project folder is gone: "Remove missing" removes such rows
    std::optional<EngineVersion> update; // Copy: the newest newer stable engine known
    bool updateMayBreak = false;         // MayBreak(version, update)
    bool contractSupported = true;       // False: this launcher cannot drive the project's engine (update it)

    // "0.1.2"; empty when the engine is missing.
    [[nodiscard]] std::string Version() const;
};

[[nodiscard]] ProjectRow InspectProjectRow(const ProjectEntry& entry, const EngineLibrary& engines);
// Every listed project, in settings order (VisibleRows sorts).
[[nodiscard]] std::vector<ProjectRow> InspectProjectRows(const LauncherSettings& settings,
                                                         const EngineLibrary& engines);

// Indices of the rows to show: the filter matches name or folder (case-insensitive ASCII; empty shows all), sorted by
// `sort` (LastOpened: as RecentProjects; ties by name).
[[nodiscard]] std::vector<std::size_t> VisibleRows(const std::vector<ProjectRow>& rows, std::string_view filter,
                                                   ProjectSort sort);

// Projects on exactly this engine version (Engines tab: "used by N projects").
[[nodiscard]] int ProjectsUsing(const std::vector<ProjectRow>& rows, const EngineVersion& version);

// What the action panel enables for a row (unavailable actions are shown disabled).
struct ProjectActions {
    bool open = false;          // Usable engine the launcher can drive
    bool run = false;           // Same as open
    bool showInFolder = false;  // Folder exists
    bool updateEngine = false;  // Copy with a newer stable engine (confirmation panel)
    bool changeEngine = false;  // Copy, and another version to choose from, newer or older (confirmation panel)
    bool repairEngine = false;  // Copy whose exact version is known (same version back; confirmation panel)
    bool replaceEngine = false; // Unregistered or missing engine, a version to offer (confirmation panel)
    bool engineGit = false;     // Developer mode and a git-managed engine: Update from remote, Publish changes
    bool remove = true;         // Remove from list (never touches the disk)
};
[[nodiscard]] ProjectActions ActionsFor(const ProjectRow& row, const EngineLibrary& engines, LauncherMode mode);

// The version Update / Repair / Replace moves the project to; nothing when that action is not available.
[[nodiscard]] std::optional<EngineVersion> UpdateTarget(const ProjectRow& row, const EngineLibrary& engines);
[[nodiscard]] std::optional<EngineVersion> RepairTarget(const ProjectRow& row, const EngineLibrary& engines);
[[nodiscard]] std::optional<EngineVersion> ReplaceTarget(const ProjectRow& row, const EngineLibrary& engines);
// The versions a project can be moved to by choice, newest first: everything known but its own.
[[nodiscard]] std::vector<EngineVersion> ChangeChoices(const ProjectRow& row, const EngineLibrary& engines);
