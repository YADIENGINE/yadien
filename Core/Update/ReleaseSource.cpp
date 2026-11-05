#include "Update/ReleaseSource.h"
#include <utility>
#include "Platform/FileSystem.h"

namespace {
constexpr std::string_view GitHubPrefix = "https://github.com/";
constexpr std::string_view GitSuffix = ".git";
constexpr const char* FeedFile = "feed.json";
} // namespace

std::string ReleasesUrl(std::string_view repository) {
    if (!repository.starts_with(GitHubPrefix)) return {};
    std::string_view path = repository.substr(GitHubPrefix.size());
    while (path.ends_with('/'))
        path.remove_suffix(1);
    if (path.ends_with(GitSuffix)) path.remove_suffix(GitSuffix.size());
    if (path.empty()) return {};
    return std::string(GitHubPrefix) + std::string(path) + "/releases";
}

ReleaseSource::ReleaseSource(std::string releasesUrl, std::filesystem::path scratchFolder)
    : _releasesUrl(std::move(releasesUrl))
    , _scratchFolder(std::move(scratchFolder)) {}

Result<std::string> ReleaseSource::Feed(const std::string& feedName, const std::atomic<bool>* cancel) const {
    const std::filesystem::path file = _scratchFolder / FeedFile;
    if (Status downloaded = DownloadFile(_releasesUrl + "/latest/download/" + feedName, file, cancel); !downloaded.Ok())
        return Result<std::string>::Failure(downloaded.Error());
    Result<std::string> feed = ReadTextFile(file);
    (void)RemoveTree(_scratchFolder);
    return feed;
}

Status ReleaseSource::DownloadAsset(const std::string& version, const std::string& fileName,
                                    const std::filesystem::path& file, const std::atomic<bool>* cancel,
                                    const DownloadProgress& progress) const {
    return DownloadFile(_releasesUrl + "/download/v" + version + "/" + fileName, file, cancel, progress);
}
