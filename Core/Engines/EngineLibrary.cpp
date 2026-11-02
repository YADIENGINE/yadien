#include "Engines/EngineLibrary.h"
#include <algorithm>
#include <utility>
#include "Platform/FileSystem.h"
#include "Platform/Text.h"

namespace {
constexpr std::string_view FolderPrefix = "Engine"; // Engines/Engine0.2.0

void ScanFolder(const std::filesystem::path& folder, std::vector<AvailableEngine>& engines,
                std::vector<std::string>& problems) {
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        const std::string name = PathToUtf8(entry.path().filename());
        if (!entry.is_directory() || !name.starts_with(FolderPrefix)) continue;
        Result<EngineManifest> manifest = LoadEngineManifest(entry.path());
        Result<EngineCopyRecord> record = LoadEngineCopyRecord(entry.path());
        if (!manifest.Ok() || !record.Ok()) {
            problems.push_back(name + ": " + (manifest.Ok() ? record.Error() : manifest.Error()));
            continue;
        }
        const EngineVersion version = manifest.Value().version;
        if (name != EngineFolderName(version)) {
            problems.push_back(name + " holds version " + version.ToString());
            continue;
        }
        const bool known = std::ranges::any_of(
            engines, [&](const AvailableEngine& engine) { return engine.manifest.version == version; });
        if (!known) engines.push_back({entry.path(), std::move(manifest).Value(), std::move(record).Value()});
    }
    if (error) problems.push_back("Cannot read " + PathToUtf8(folder) + ": " + error.message());
}
} // namespace

std::string EngineFolderName(const EngineVersion& version) { return std::string(FolderPrefix) + version.ToString(); }

Status RemoveDownloadedEngine(const AvailableEngine& engine) {
    if (!engine.downloaded)
        return Status::Failure("Engine " + engine.manifest.version.ToString() + " came with the launcher");
    return RemoveTree(engine.folder);
}

EngineLibrary EngineLibrary::Scan(const std::filesystem::path& bundledFolder,
                                  const std::filesystem::path& cacheFolder) {
    EngineLibrary library;
    std::error_code error;
    ScanFolder(bundledFolder, library._engines, library._problems);
    const std::size_t bundled = library._engines.size();
    if (!cacheFolder.empty() && std::filesystem::is_directory(cacheFolder, error))
        ScanFolder(cacheFolder, library._engines, library._problems);
    for (std::size_t index = bundled; index < library._engines.size(); ++index)
        library._engines[index].downloaded = true;
    std::ranges::sort(library._engines, [](const AvailableEngine& a, const AvailableEngine& b) {
        return a.manifest.version > b.manifest.version;
    });
    return library;
}

const AvailableEngine* EngineLibrary::Find(const EngineVersion& version) const {
    const auto found = std::ranges::find_if(
        _engines, [&](const AvailableEngine& engine) { return engine.manifest.version == version; });
    return found == _engines.end() ? nullptr : &*found;
}

void EngineLibrary::SetOnline(std::vector<OnlineEngine> online) {
    std::erase_if(online, [&](const OnlineEngine& engine) { return Find(engine.version) != nullptr; });
    _online = std::move(online);
}

const OnlineEngine* EngineLibrary::FindOnline(const EngineVersion& version) const {
    const auto found = std::ranges::find(_online, version, &OnlineEngine::version);
    return found == _online.end() ? nullptr : &*found;
}

std::vector<EngineVersion> EngineLibrary::KnownVersions() const {
    std::vector<EngineVersion> versions;
    for (const AvailableEngine& engine : _engines)
        versions.push_back(engine.manifest.version);
    for (const OnlineEngine& engine : _online)
        versions.push_back(engine.version);
    std::ranges::sort(versions, std::greater{});
    return versions;
}

std::optional<EngineVersion> EngineLibrary::UpdateFor(const EngineVersion& version) const {
    const std::vector<EngineVersion> known = KnownVersions();
    if (known.empty() || known.front() <= version) return std::nullopt;
    return known.front();
}
