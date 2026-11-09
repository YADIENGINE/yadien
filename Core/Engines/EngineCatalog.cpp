#include "Engines/EngineCatalog.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <utility>
#include "Engines/EngineCopyRecord.h"
#include "Platform/Crypto.h"
#include "Platform/Download.h"
#include "Platform/FileSystem.h"
#include "Platform/KnownFolders.h"
#include "Platform/Process.h"
#include "Platform/Text.h"
#include "Projects/CopyIntegrity.h"

namespace {
constexpr std::string_view GitHubPrefix = "https://github.com/";
constexpr std::string_view GitSuffix = ".git";
constexpr const wchar_t* ApiOverrideVariable = L"LAUNCHER_ENGINE_API";
constexpr const char* ReleasesFile = "releases.json";
constexpr const char* ArchiveFile = "engine.tar.gz";
constexpr const char* FilesFolder = "files";
constexpr std::string_view TagPrefix = "v";
constexpr const char* DownloadSuffix = ".downloading";
constexpr const char* PrebuiltAsset = "EnginePrebuilt.zip"; // Scripts/package.py PREBUILT_ASSET
constexpr const char* NotesAsset = "ReleaseNotes.json";     // Scripts/release.py NOTES_ASSET
constexpr const char* PrebuiltDescription = "Prebuilt.json";
constexpr std::string_view DigestPrefix = "sha256:";

Result<AvailableEngine> Fail(const std::string& message) { return Result<AvailableEngine>::Failure(message); }

Result<AvailableEngine> LoadAvailable(const std::filesystem::path& folder) {
    Result<EngineManifest> manifest = LoadEngineManifest(folder);
    if (!manifest.Ok()) return Fail(manifest.Error());
    Result<EngineCopyRecord> record = LoadEngineCopyRecord(folder);
    if (!record.Ok()) return Fail(record.Error());
    return AvailableEngine{folder, std::move(manifest).Value(), std::move(record).Value()};
}

std::string Describe(const CopyIntegrity& found) {
    const std::vector<std::string>& first = !found.modified.empty()  ? found.modified
                                            : !found.missing.empty() ? found.missing
                                                                     : found.added;
    return std::to_string(found.modified.size()) + " changed, " + std::to_string(found.missing.size()) + " missing, " +
           std::to_string(found.added.size()) + " extra, e.g. " + first.front();
}

// The source archive's one top folder (owner-repo-commit) is dropped.
Status Extract(const std::filesystem::path& archive, const std::filesystem::path& folder,
               const std::atomic<bool>* cancel) {
    const ProcessCommand tar{SystemFolder() / "tar.exe",
                             {"--strip-components", "1", "-xzf", PathToUtf8(archive), "-C", PathToUtf8(folder)}};
    const ProcessResult extracted = RunCaptured(tar, [](const std::string&) {}, cancel);
    if (extracted.canceled) return Status::Failure("The download was canceled");
    if (!extracted.Succeeded())
        return Status::Failure("Cannot extract the engine" + (extracted.Started() ? "" : ": " + extracted.startError));
    return Status::Success();
}
} // namespace

bool IncludesPrereleases(const EngineVersion& newest) { return newest.major == 0; }

std::string EngineCatalogUrl(std::string_view repository) {
    if (!repository.starts_with(GitHubPrefix)) return {};
    std::string_view path = repository.substr(GitHubPrefix.size());
    while (path.ends_with('/'))
        path.remove_suffix(1);
    if (path.ends_with(GitSuffix)) path.remove_suffix(GitSuffix.size());
    if (path.empty()) return {};
    return "https://api.github.com/repos/" + std::string(path) + "/releases?per_page=100";
}

std::string EngineCatalogSource(std::string_view repository) {
    std::array<wchar_t, MAX_PATH * 4> value{};
    const DWORD length = GetEnvironmentVariableW(ApiOverrideVariable, value.data(), static_cast<DWORD>(value.size()));
    if (length > 0 && length < value.size()) return WideToUtf8(value.data());
    return EngineCatalogUrl(repository);
}

Result<std::vector<OnlineEngine>> ParseEngineCatalog(const std::string& json, bool includePrereleases) {
    const nlohmann::json data = nlohmann::json::parse(json, nullptr, false);
    if (!data.is_array()) return Result<std::vector<OnlineEngine>>::Failure("Unexpected answer from the engine list");
    std::vector<OnlineEngine> engines;
    for (const nlohmann::json& item : data) {
        if (!item.is_object() || item.value("draft", false)) continue;
        if (item.value("prerelease", false) && !includePrereleases) continue;
        const std::string tag = item.value("tag_name", "");
        const std::string archive = item.value("tarball_url", "");
        if (!tag.starts_with(TagPrefix) || archive.empty()) continue;
        const auto version = EngineVersion::Parse(std::string_view(tag).substr(TagPrefix.size()));
        if (!version) continue;
        OnlineEngine engine{*version, tag, {}, archive};
        for (const nlohmann::json& asset : item.value("assets", nlohmann::json::array())) {
            if (!asset.is_object()) continue;
            const std::string name = asset.value("name", "");
            if (name == EngineCopyFile) engine.recordUrl = asset.value("browser_download_url", "");
            if (name == NotesAsset) engine.notesUrl = asset.value("browser_download_url", "");
            if (const std::string digest = asset.value("digest", "");
                name == PrebuiltAsset && digest.starts_with(DigestPrefix)) {
                engine.prebuiltUrl = asset.value("browser_download_url", "");
                engine.prebuiltSha256 = digest.substr(DigestPrefix.size());
            }
        }
        if (!engine.recordUrl.empty()) engines.push_back(std::move(engine));
    }
    std::ranges::sort(engines, std::greater{}, &OnlineEngine::version);
    return engines;
}

Result<std::vector<OnlineEngine>> FetchEngineCatalog(const std::string& apiUrl, bool includePrereleases,
                                                     const std::filesystem::path& scratchFolder,
                                                     const std::atomic<bool>* cancel) {
    using Answer = Result<std::vector<OnlineEngine>>;
    const std::filesystem::path file = scratchFolder / ReleasesFile;
    if (Status downloaded = DownloadFile(apiUrl, file, cancel); !downloaded.Ok())
        return Answer::Failure(downloaded.Error());
    Result<std::string> text = ReadTextFile(file);
    (void)RemoveTree(scratchFolder);
    if (!text.Ok()) return Answer::Failure(text.Error());
    return ParseEngineCatalog(text.Value(), includePrereleases);
}

Result<std::vector<EngineNote>> FetchEngineNotes(const std::vector<OnlineEngine>& engines,
                                                 const std::filesystem::path& scratchFolder,
                                                 const std::atomic<bool>* cancel) {
    using Answer = Result<std::vector<EngineNote>>;
    const auto newest =
        std::ranges::find_if(engines, [](const OnlineEngine& engine) { return !engine.notesUrl.empty(); });
    if (newest == engines.end()) return Answer(std::vector<EngineNote>{});
    const std::filesystem::path file = scratchFolder / NotesAsset;
    const Status downloaded = DownloadFile(newest->notesUrl, file, cancel);
    Result<std::string> text = downloaded.Ok() ? ReadTextFile(file) : Result<std::string>::Failure(downloaded.Error());
    (void)RemoveTree(scratchFolder);
    if (!text.Ok()) return Answer::Failure(text.Error());
    return ParseEngineNotes(text.Value());
}

Result<AvailableEngine> DownloadEngine(const OnlineEngine& engine, const std::filesystem::path& cacheFolder,
                                       const std::atomic<bool>* cancel) {
    const std::string name = EngineFolderName(engine.version);
    const std::filesystem::path destination = cacheFolder / name;
    std::error_code error;
    if (std::filesystem::exists(destination, error)) return LoadAvailable(destination);

    const std::filesystem::path staging = cacheFolder / ("." + name + DownloadSuffix);
    const std::filesystem::path files = staging / FilesFolder;
    const auto failed = [&](const std::string& message) {
        (void)RemoveTree(staging);
        return Fail(message);
    };
    if (Status removed = RemoveTree(staging); !removed.Ok()) return Fail(removed.Error());
    std::filesystem::create_directories(files, error);
    if (error) return failed("Cannot create " + PathToUtf8(files) + ": " + error.message());

    if (Status record = DownloadFile(engine.recordUrl, files / EngineCopyFile, cancel); !record.Ok())
        return failed(record.Error());
    if (Status archive = DownloadFile(engine.archiveUrl, staging / ArchiveFile, cancel); !archive.Ok())
        return failed(archive.Error());
    if (Status extracted = Extract(staging / ArchiveFile, files, cancel); !extracted.Ok())
        return failed(extracted.Error());

    Result<AvailableEngine> loaded = LoadAvailable(files);
    if (!loaded.Ok()) return failed(loaded.Error());
    if (loaded.Value().manifest.version != engine.version || loaded.Value().record.tag != engine.tag)
        return failed(engine.tag + " holds engine " + loaded.Value().manifest.version.ToString() + " (" +
                      loaded.Value().record.tag + ")");
    Result<CopyIntegrity> integrity = CheckCopyIntegrity(files, loaded.Value().record);
    if (!integrity.Ok()) return failed(integrity.Error());
    if (!integrity.Value().Clean())
        return failed("The download of " + engine.tag + " does not match its record: " + Describe(integrity.Value()));

    if (Status moved = MovePath(files, destination); !moved.Ok()) return failed(moved.Error());
    (void)RemoveTree(staging);
    return LoadAvailable(destination);
}

Status DownloadPrebuiltEngine(const OnlineEngine& engine, const std::filesystem::path& prebuiltFolder,
                              const std::atomic<bool>* cancel) {
    if (engine.prebuiltUrl.empty() || engine.prebuiltSha256.empty()) return Status::Success();
    const std::string name = engine.version.ToString();
    const std::filesystem::path destination = prebuiltFolder / name;
    std::error_code error;
    if (std::filesystem::exists(destination, error)) return Status::Success();

    const std::filesystem::path staging = prebuiltFolder / ("." + name + DownloadSuffix);
    const std::filesystem::path files = staging / FilesFolder;
    const std::filesystem::path archive = staging / PrebuiltAsset;
    const auto failed = [&](const std::string& message) {
        (void)RemoveTree(staging);
        return Status::Failure(message);
    };
    if (Status removed = RemoveTree(staging); !removed.Ok()) return removed;
    std::filesystem::create_directories(files, error);
    if (error) return failed("Cannot create " + PathToUtf8(files) + ": " + error.message());

    if (Status downloaded = DownloadFile(engine.prebuiltUrl, archive, cancel); !downloaded.Ok())
        return failed(downloaded.Error());
    Result<std::string> digest = Sha256FileHex(archive);
    if (!digest.Ok()) return failed(digest.Error());
    if (digest.Value() != engine.prebuiltSha256)
        return failed("The compiled engine of " + engine.tag + " does not match its release");
    const ProcessCommand tar{SystemFolder() / "tar.exe", {"-xf", PathToUtf8(archive), "-C", PathToUtf8(files)}};
    const ProcessResult extracted = RunCaptured(tar, [](const std::string&) {}, cancel);
    if (extracted.canceled) return failed("The download was canceled");
    if (!extracted.Succeeded() || !std::filesystem::is_regular_file(files / PrebuiltDescription, error))
        return failed("Cannot extract the compiled engine of " + engine.tag);

    if (Status moved = MovePath(files, destination); !moved.Ok()) return failed(moved.Error());
    (void)RemoveTree(staging);
    return Status::Success();
}
