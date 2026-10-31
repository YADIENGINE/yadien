#include "Settings/ProjectList.h"
#include <algorithm>
#include <chrono>
#include <format>
#include "Platform/Text.h"
#include "Projects/ProjectInfo.h"

namespace {
// Windows paths ignore case and accept both separators.
std::string RootKey(std::filesystem::path root) {
    return ToLowerAscii(PathToUtf8(root.make_preferred().lexically_normal()));
}

bool SameRoot(const std::filesystem::path& a, const std::filesystem::path& b) { return RootKey(a) == RootKey(b); }

std::vector<ProjectEntry>::iterator FindEntry(LauncherSettings& settings, const std::filesystem::path& root) {
    return std::ranges::find_if(settings.projects,
                                [&](const ProjectEntry& entry) { return SameRoot(entry.root, root); });
}
} // namespace

std::filesystem::path CanonicalRoot(const std::filesystem::path& root) {
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(root, error), error);
    if (!absolute.has_filename() && absolute.has_parent_path()) absolute = absolute.parent_path();
    return absolute.lexically_normal();
}

Status AddProject(LauncherSettings& settings, const std::filesystem::path& root) {
    const std::filesystem::path canonical = CanonicalRoot(root);
    std::error_code error;
    if (!std::filesystem::is_regular_file(canonical / ProjectFile, error))
        return Status::Failure(PathToUtf8(canonical) + " has no " + ProjectFile + "; it is not a project folder");
    if (FindEntry(settings, canonical) != settings.projects.end())
        return Status::Failure(PathToUtf8(canonical) + " is already in the list");
    settings.projects.push_back({canonical, ""});
    return Status::Success();
}

Status RemoveProject(LauncherSettings& settings, const std::filesystem::path& root) {
    const auto entry = FindEntry(settings, CanonicalRoot(root));
    if (entry == settings.projects.end()) return Status::Failure(PathToUtf8(root) + " is not in the list");
    settings.projects.erase(entry);
    return Status::Success();
}

void MarkProjectOpened(LauncherSettings& settings, const std::filesystem::path& root, const std::string& now) {
    const std::filesystem::path canonical = CanonicalRoot(root);
    const auto entry = FindEntry(settings, canonical);
    if (entry == settings.projects.end())
        settings.projects.push_back({canonical, now});
    else
        entry->lastOpened = now;
}

std::vector<ProjectEntry> RecentProjects(const LauncherSettings& settings) {
    std::vector<ProjectEntry> sorted = settings.projects;
    std::ranges::stable_sort(sorted, [](const ProjectEntry& a, const ProjectEntry& b) {
        if (a.lastOpened.empty() != b.lastOpened.empty()) return b.lastOpened.empty();
        if (a.lastOpened != b.lastOpened) return a.lastOpened > b.lastOpened; // ISO 8601 sorts as text
        return a.root < b.root;
    });
    return sorted;
}

std::string CurrentTimeIso() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}
