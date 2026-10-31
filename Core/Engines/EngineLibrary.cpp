#include "Engines/EngineLibrary.h"
#include <algorithm>
#include "Platform/Text.h"

namespace {
constexpr std::string_view FolderPrefix = "Engine"; // Engines/Engine0.2
} // namespace

EngineLibrary EngineLibrary::Scan(const std::filesystem::path& enginesFolder) {
    EngineLibrary library;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(enginesFolder, error)) {
        const std::string name = PathToUtf8(entry.path().filename());
        if (!entry.is_directory() || !name.starts_with(FolderPrefix)) continue;
        Result<EngineManifest> manifest = LoadEngineManifest(entry.path());
        Result<EngineCopyRecord> record = LoadEngineCopyRecord(entry.path());
        if (!manifest.Ok() || !record.Ok()) {
            library._problems.push_back(name + ": " + (manifest.Ok() ? record.Error() : manifest.Error()));
            continue;
        }
        if (name != std::string(FolderPrefix) + manifest.Value().version.Series()) {
            library._problems.push_back(name + " holds version " + manifest.Value().version.ToString());
            continue;
        }
        library._engines.push_back({entry.path(), std::move(manifest).Value(), std::move(record).Value()});
    }
    if (error) library._problems.push_back("Cannot read " + PathToUtf8(enginesFolder) + ": " + error.message());
    std::ranges::sort(library._engines, [](const AvailableEngine& a, const AvailableEngine& b) {
        return a.manifest.version > b.manifest.version;
    });
    return library;
}

const AvailableEngine* EngineLibrary::FindSeries(const std::string& series) const {
    const auto found = std::ranges::find_if(
        _engines, [&](const AvailableEngine& engine) { return engine.manifest.version.Series() == series; });
    return found == _engines.end() ? nullptr : &*found;
}

const AvailableEngine* EngineLibrary::UpdateFor(const EngineVersion& version) const {
    const AvailableEngine* newest = Newest();
    return newest != nullptr && newest->manifest.version > version ? newest : nullptr;
}
