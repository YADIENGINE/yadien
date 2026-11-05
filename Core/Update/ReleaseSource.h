#pragma once

#include "Platform/Download.h"
#include "Platform/Result.h"
#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>

// Where launcher updates come from: the launcher repository's GitHub release files, downloaded with DownloadFile so
// that a transfer can be canceled and uses the system's proxy settings. Velopack reads through it (Velopack's own
// GitHub source cannot be canceled). Verifying a package against the feed's SHA-256 stays Velopack's job.
//
// Only github.com download addresses are read, never the API: the API allows 60 requests an hour per address, which a
// shared network uses up. The latest release's feed lists every version's packages (the release workflow packs on top
// of the previous release), and each package lies in its own version's release.

// "https://github.com/owner/repo" → "https://github.com/owner/repo/releases"; empty when it is not a github.com
// repository.
[[nodiscard]] std::string ReleasesUrl(std::string_view repository);

class ReleaseSource {
public:
    // `releasesUrl`: https, or file for tests, holding <releasesUrl>/latest/download/<feed> and
    // <releasesUrl>/download/v<version>/<package>. `scratchFolder` holds what is read and is removed afterwards.
    ReleaseSource(std::string releasesUrl, std::filesystem::path scratchFolder);

    // The latest release's `feedName` (Velopack's releases.win.json).
    [[nodiscard]] Result<std::string> Feed(const std::string& feedName, const std::atomic<bool>* cancel) const;
    // Downloads a package the feed names from the release of its version.
    [[nodiscard]] Status DownloadAsset(const std::string& version, const std::string& fileName,
                                       const std::filesystem::path& file, const std::atomic<bool>* cancel,
                                       const DownloadProgress& progress) const;

private:
    const std::string _releasesUrl;
    const std::filesystem::path _scratchFolder;
};
