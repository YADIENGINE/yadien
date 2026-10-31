#include "Projects/ProjectScan.h"
#include <algorithm>
#include <array>
#include <string>
#include "Platform/Text.h"
#include "Projects/ProjectInfo.h"
#include "Settings/ProjectList.h"

namespace {
// Folders that never hold projects of their own: build trees, packages, dependencies.
constexpr std::array<const char*, 5> SkippedFolders = {"build", "release", "thirdparty", "node_modules", "__pycache__"};

bool Skipped(const std::filesystem::path& folder) {
    const std::string name = ToLowerAscii(PathToUtf8(folder.filename()));
    return name.empty() || name.front() == '.' || std::ranges::find(SkippedFolders, name) != SkippedFolders.end();
}

void Scan(const std::filesystem::path& folder, int depth, const std::atomic<bool>* cancel,
          std::vector<std::filesystem::path>& found) {
    if (cancel && *cancel) return;
    std::error_code error;
    if (std::filesystem::is_regular_file(folder / ProjectFile, error)) {
        found.push_back(CanonicalRoot(folder));
        return;
    }
    if (depth >= MaxScanDepth) return;
    std::filesystem::directory_iterator entries(folder, std::filesystem::directory_options::skip_permission_denied,
                                                error);
    if (error) return;
    for (const std::filesystem::directory_entry& entry : entries) {
        std::error_code entryError;
        if (!entry.is_directory(entryError) || entry.is_symlink(entryError) || Skipped(entry.path())) continue;
        Scan(entry.path(), depth + 1, cancel, found);
    }
}
} // namespace

std::vector<std::filesystem::path> FindProjects(const std::filesystem::path& folder, const std::atomic<bool>* cancel) {
    std::vector<std::filesystem::path> found;
    Scan(folder, 0, cancel, found);
    std::ranges::sort(found);
    return found;
}
