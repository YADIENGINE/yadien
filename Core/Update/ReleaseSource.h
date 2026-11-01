#pragma once

#include "Engines/EngineVersion.h"
#include "Platform/Download.h"
#include "Platform/Result.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// Where launcher updates come from: the GitHub releases of the launcher's repository, downloaded with DownloadFile so
// that a transfer can be canceled and uses the system's proxy settings. Velopack reads through it (Velopack's own
// GitHub source cannot be canceled). Verifying a package against the feed's SHA-256 stays Velopack's job.
struct ReleaseAsset {
    std::string name;
    std::string url;
};

struct PublishedRelease {
    std::string publishedAt;
    std::vector<ReleaseAsset> assets;
};

// "https://github.com/owner/repo" → its releases API URL; empty when `repository` is not a github.com repository.
[[nodiscard]] std::string ReleasesApiUrl(std::string_view repository);
// Before 1.0 every release is a pre-release (Docs/Packaging.md), so a 0.x launcher reads them; from 1.0 only stable.
[[nodiscard]] bool IncludesPrereleases(const EngineVersion& running);
// The API's answer, newest first, without drafts and (unless asked) pre-releases.
[[nodiscard]] Result<std::vector<PublishedRelease>> ParseReleases(const std::string& json, bool includePrereleases);
// One feed from several releases' feeds; an asset listed twice is kept once and unreadable feeds are skipped.
[[nodiscard]] std::string MergeFeeds(const std::vector<std::string>& feeds);

class ReleaseSource {
public:
    // `apiUrl`: https, or file for tests. `scratchFolder` holds what is read and is removed afterwards.
    ReleaseSource(std::string apiUrl, bool includePrereleases, std::filesystem::path scratchFolder);

    // Reads the release list and merges every release's `feedName` (Velopack's releases.win.json).
    [[nodiscard]] Result<std::string> Feed(const std::string& feedName, const std::atomic<bool>* cancel);
    // Downloads a file named in the feed; the releases are the ones the last Feed() read.
    [[nodiscard]] Status DownloadAsset(const std::string& fileName, const std::filesystem::path& file,
                                       const std::atomic<bool>* cancel, const DownloadProgress& progress);

private:
    const std::string _apiUrl;
    const bool _includePrereleases;
    const std::filesystem::path _scratchFolder;
    std::mutex _mutex; // Velopack may call from its own threads
    std::vector<PublishedRelease> _releases;
};
