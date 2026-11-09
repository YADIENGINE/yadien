#include "Projects/ProjectRow.h"
#include <algorithm>
#include "Cli/EngineCommand.h"
#include "Platform/Text.h"

namespace {
bool Matches(const ProjectRow& row, const std::string& filter) {
    if (filter.empty()) return true;
    return ToLowerAscii(row.info.name).find(filter) != std::string::npos ||
           ToLowerAscii(PathToUtf8(row.entry.root)).find(filter) != std::string::npos;
}

// LastOpened order (RecentProjects): newest first, never opened last.
bool OpenedBefore(const ProjectRow& left, const ProjectRow& right) {
    if (left.entry.lastOpened.empty() != right.entry.lastOpened.empty()) return right.entry.lastOpened.empty();
    return left.entry.lastOpened > right.entry.lastOpened;
}
} // namespace

std::string ProjectRow::Version() const { return info.manifest ? info.manifest->version.ToString() : std::string(); }

ProjectRow InspectProjectRow(const ProjectEntry& entry, const EngineLibrary& engines) {
    ProjectRow row;
    row.entry = entry;
    row.info = InspectProject(entry.root);
    std::error_code error;
    row.folderMissing = !std::filesystem::is_directory(entry.root, error);
    if (row.info.manifest) {
        row.contractSupported = SupportsToolsContract(row.info.manifest->toolsContract);
        if (row.info.kind == EngineKind::Copy) {
            if (const std::optional<EngineVersion> update = engines.UpdateFor(row.info.manifest->version)) {
                row.update = update;
                row.updateMayBreak = MayBreak(row.info.manifest->version, *update);
            }
        }
    }
    return row;
}

std::vector<ProjectRow> InspectProjectRows(const LauncherSettings& settings, const EngineLibrary& engines) {
    std::vector<ProjectRow> rows;
    rows.reserve(settings.projects.size());
    for (const ProjectEntry& entry : settings.projects)
        rows.push_back(InspectProjectRow(entry, engines));
    return rows;
}

std::vector<std::size_t> VisibleRows(const std::vector<ProjectRow>& rows, std::string_view filter, ProjectSort sort) {
    const std::string needle = ToLowerAscii(filter);
    std::vector<std::size_t> visible;
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (Matches(rows[i], needle)) visible.push_back(i);
    const auto byName = [&](std::size_t left, std::size_t right) {
        const std::string a = ToLowerAscii(rows[left].info.name);
        const std::string b = ToLowerAscii(rows[right].info.name);
        return a != b ? a < b : rows[left].entry.root < rows[right].entry.root;
    };
    std::ranges::stable_sort(visible, [&](std::size_t left, std::size_t right) {
        switch (sort) {
        case ProjectSort::LastOpened:
            if (OpenedBefore(rows[left], rows[right])) return true;
            if (OpenedBefore(rows[right], rows[left])) return false;
            return byName(left, right);
        case ProjectSort::Name: return byName(left, right);
        case ProjectSort::Path: return rows[left].entry.root < rows[right].entry.root;
        }
        return false;
    });
    return visible;
}

int ProjectsUsing(const std::vector<ProjectRow>& rows, const EngineVersion& version) {
    return static_cast<int>(std::ranges::count_if(
        rows, [&](const ProjectRow& row) { return row.info.manifest && row.info.manifest->version == version; }));
}

std::optional<EngineVersion> UpdateTarget(const ProjectRow& row, const EngineLibrary& engines) {
    if (row.folderMissing || row.info.kind != EngineKind::Copy || !row.info.manifest) return std::nullopt;
    return engines.UpdateFor(row.info.manifest->version);
}

std::optional<EngineVersion> RepairTarget(const ProjectRow& row, const EngineLibrary& engines) {
    if (row.folderMissing || row.info.kind != EngineKind::Copy || !row.info.manifest) return std::nullopt;
    const EngineVersion version = row.info.manifest->version;
    if (engines.Find(version) == nullptr && engines.FindOnline(version) == nullptr) return std::nullopt;
    return version;
}

std::optional<EngineVersion> RestoreTarget(const ProjectRow& row, const EngineLibrary& engines) {
    if (row.folderMissing || !row.info.recorded) return std::nullopt;
    const EngineVersion version = *row.info.recorded;
    const bool missing = row.info.kind == EngineKind::Missing;
    const bool otherCopy =
        row.info.kind == EngineKind::Copy && row.info.manifest && row.info.manifest->version != version;
    if (!missing && !otherCopy) return std::nullopt;
    if (engines.Find(version) == nullptr && engines.FindOnline(version) == nullptr) return std::nullopt;
    return version;
}

std::optional<EngineVersion> ReplaceTarget(const ProjectRow& row, const EngineLibrary& engines) {
    if (row.folderMissing) return std::nullopt;
    if (row.info.kind != EngineKind::Unregistered && row.info.kind != EngineKind::Missing) return std::nullopt;
    if (row.info.kind == EngineKind::Missing && RestoreTarget(row, engines)) return std::nullopt;
    const std::vector<EngineVersion> known = engines.KnownVersions();
    if (known.empty()) return std::nullopt;
    return known.front();
}

std::vector<EngineVersion> ChangeChoices(const ProjectRow& row, const EngineLibrary& engines) {
    std::vector<EngineVersion> choices = engines.KnownVersions();
    if (row.info.kind == EngineKind::Copy && row.info.manifest) std::erase(choices, row.info.manifest->version);
    return choices;
}

ProjectActions ActionsFor(const ProjectRow& row, const EngineLibrary& engines, LauncherMode mode) {
    ProjectActions actions;
    const bool usable = !row.folderMissing && row.info.manifest && row.contractSupported && row.info.problem.empty();
    actions.open = usable;
    actions.run = usable;
    actions.showInFolder = !row.folderMissing;
    actions.updateEngine = UpdateTarget(row, engines).has_value();
    actions.changeEngine =
        !row.folderMissing && row.info.kind == EngineKind::Copy && !ChangeChoices(row, engines).empty();
    actions.repairEngine = RepairTarget(row, engines).has_value();
    actions.restoreEngine = RestoreTarget(row, engines).has_value();
    actions.replaceEngine = ReplaceTarget(row, engines).has_value();
    actions.engineGit = mode == LauncherMode::Developer && !row.folderMissing && row.info.kind == EngineKind::Git;
    return actions;
}
