#include "Update/ReleaseSource.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <utility>
#include "Platform/FileSystem.h"

namespace {
constexpr std::string_view GitHubPrefix = "https://github.com/";
constexpr std::string_view GitSuffix = ".git";
constexpr const char* ReleasesFile = "releases.json";
constexpr const char* FeedFile = "feed.json";
// The newest releases are enough: an older delta only matters to an installation that many versions behind, and that
// one falls back to the full package.
constexpr const char* ReleaseLimit = "?per_page=10";

Result<std::string> DownloadText(const std::string& url, const std::filesystem::path& file,
                                 const std::atomic<bool>* cancel) {
    if (Status downloaded = DownloadFile(url, file, cancel); !downloaded.Ok())
        return Result<std::string>::Failure(downloaded.Error());
    return ReadTextFile(file);
}

const ReleaseAsset* FindAsset(const PublishedRelease& release, const std::string& name) {
    const auto found = std::ranges::find(release.assets, name, &ReleaseAsset::name);
    return found == release.assets.end() ? nullptr : &*found;
}
} // namespace

std::string ReleasesApiUrl(std::string_view repository) {
    if (!repository.starts_with(GitHubPrefix)) return {};
    std::string_view path = repository.substr(GitHubPrefix.size());
    while (path.ends_with('/'))
        path.remove_suffix(1);
    if (path.ends_with(GitSuffix)) path.remove_suffix(GitSuffix.size());
    if (path.empty()) return {};
    return "https://api.github.com/repos/" + std::string(path) + "/releases" + ReleaseLimit;
}

bool IncludesPrereleases(const EngineVersion& running) { return running.major == 0; }

Result<std::vector<PublishedRelease>> ParseReleases(const std::string& json, bool includePrereleases) {
    const nlohmann::json data = nlohmann::json::parse(json, nullptr, false);
    if (!data.is_array())
        return Result<std::vector<PublishedRelease>>::Failure("Unexpected answer from the update server");
    std::vector<PublishedRelease> releases;
    for (const nlohmann::json& item : data) {
        if (!item.is_object() || item.value("draft", false)) continue;
        if (item.value("prerelease", false) && !includePrereleases) continue;
        PublishedRelease release;
        if (const auto published = item.find("published_at"); published != item.end() && published->is_string())
            release.publishedAt = published->get<std::string>();
        for (const nlohmann::json& asset : item.value("assets", nlohmann::json::array())) {
            if (!asset.is_object()) continue;
            const std::string name = asset.value("name", "");
            const std::string url = asset.value("browser_download_url", "");
            if (!name.empty() && !url.empty()) release.assets.push_back({name, url});
        }
        releases.push_back(std::move(release));
    }
    std::ranges::stable_sort(releases, std::greater{}, &PublishedRelease::publishedAt);
    return releases;
}

std::string MergeFeeds(const std::vector<std::string>& feeds) {
    nlohmann::json assets = nlohmann::json::array();
    std::set<std::string> seen;
    for (const std::string& text : feeds) {
        const nlohmann::json feed = nlohmann::json::parse(text, nullptr, false);
        if (!feed.is_object() || !feed.contains("Assets") || !feed["Assets"].is_array()) continue;
        for (const nlohmann::json& asset : feed["Assets"])
            if (asset.is_object() && seen.insert(asset.value("FileName", "")).second) assets.push_back(asset);
    }
    return nlohmann::json{{"Assets", assets}}.dump();
}

ReleaseSource::ReleaseSource(std::string apiUrl, bool includePrereleases, std::filesystem::path scratchFolder)
    : _apiUrl(std::move(apiUrl))
    , _includePrereleases(includePrereleases)
    , _scratchFolder(std::move(scratchFolder)) {}

Result<std::string> ReleaseSource::Feed(const std::string& feedName, const std::atomic<bool>* cancel) {
    Result<std::string> list = DownloadText(_apiUrl, _scratchFolder / ReleasesFile, cancel);
    if (!list.Ok()) return list;
    Result<std::vector<PublishedRelease>> releases = ParseReleases(list.Value(), _includePrereleases);
    if (!releases.Ok()) return Result<std::string>::Failure(releases.Error());

    std::vector<std::string> feeds;
    for (const PublishedRelease& release : releases.Value()) {
        const ReleaseAsset* asset = FindAsset(release, feedName);
        if (asset == nullptr) continue;
        Result<std::string> feed = DownloadText(asset->url, _scratchFolder / FeedFile, cancel);
        if (cancel != nullptr && cancel->load()) return feed;
        if (feed.Ok()) feeds.push_back(std::move(feed).Value()); // A release without a readable feed is not an update
    }
    (void)RemoveTree(_scratchFolder);
    const std::scoped_lock lock(_mutex);
    _releases = std::move(releases).Value();
    return MergeFeeds(feeds);
}

Status ReleaseSource::DownloadAsset(const std::string& fileName, const std::filesystem::path& file,
                                    const std::atomic<bool>* cancel, const DownloadProgress& progress) {
    std::string url;
    {
        const std::scoped_lock lock(_mutex);
        for (const PublishedRelease& release : _releases) {
            if (const ReleaseAsset* asset = FindAsset(release, fileName)) {
                url = asset->url;
                break;
            }
        }
    }
    if (url.empty()) return Status::Failure(fileName + " is not in any release");
    return DownloadFile(url, file, cancel, progress);
}
