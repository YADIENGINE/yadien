#include "Update/VelopackUpdates.h"
#include <Velopack.h>
#include <windows.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include "Platform/Text.h"
#include "Update/ReleaseSource.h"

namespace {
constexpr const wchar_t* ReleasesOverrideVariable = L"LAUNCHER_UPDATE_RELEASES"; // Scripts/update_test.py
constexpr std::size_t MaxVelopackError = 1024;
constexpr int ProgressComplete = 100;
constexpr const char* CheckFailed = "Cannot check for updates";

std::string LastError(const char* fallback) {
    std::array<char, MaxVelopackError> text{};
    const std::size_t length = vpkc_get_last_error(text.data(), text.size());
    return length == 0 ? std::string(fallback) : std::string(text.data());
}

std::string ReleasesSource() {
    std::array<wchar_t, MAX_PATH * 4> value{};
    const DWORD length =
        GetEnvironmentVariableW(ReleasesOverrideVariable, value.data(), static_cast<DWORD>(value.size()));
    if (length > 0 && length < value.size()) return WideToUtf8(value.data());
    return ReleasesUrl(LAUNCHER_REPOSITORY);
}

std::filesystem::path ScratchFolder() {
    std::error_code error;
    return std::filesystem::temp_directory_path(error) /
           Utf8ToPath(std::string(LAUNCHER_EXECUTABLE_NAME) + "-update-" + std::to_string(GetCurrentProcessId()));
}

AvailableUpdate Describe(const vpkc_asset_t& asset, std::uint64_t downloadBytes) {
    return {asset.Version, downloadBytes, asset.NotesMarkdown != nullptr ? asset.NotesMarkdown : ""};
}

// Velopack's custom source: its callbacks call this with the object as user data. Velopack reports a failed callback
// only as its own generic error, so the bridge keeps the real one.
struct SourceBridge {
    SourceBridge(std::string releasesUrl, std::filesystem::path scratchFolder)
        : releases(std::move(releasesUrl), std::move(scratchFolder)) {}

    void Begin(const std::atomic<bool>* flag) {
        cancel = flag;
        const std::scoped_lock lock(mutex);
        error.clear();
    }
    void Remember(const std::string& text) {
        const std::scoped_lock lock(mutex);
        error = text;
    }
    // The callback's error when one failed, otherwise Velopack's own.
    [[nodiscard]] std::string Error(const char* fallback) {
        const std::scoped_lock lock(mutex);
        return error.empty() ? LastError(fallback) : error;
    }

    ReleaseSource releases;
    std::atomic<const std::atomic<bool>*> cancel = nullptr;
    std::mutex mutex; // Velopack may call from its own threads
    std::string error;
};

char* ReadFeed(void* bridge, const char* feedName) {
    auto& source = *static_cast<SourceBridge*>(bridge);
    const Result<std::string> feed = source.releases.Feed(feedName, source.cancel.load());
    if (!feed.Ok()) {
        source.Remember(feed.Error());
        return nullptr;
    }
    return _strdup(feed.Value().c_str());
}

void FreeFeed(void*, char* feed) { std::free(feed); }

bool DownloadAsset(void* bridge, const vpkc_asset_t* asset, const char* localPath, std::size_t progressId) {
    auto& source = *static_cast<SourceBridge*>(bridge);
    const DownloadProgress progress = [progressId](std::uint64_t done, std::uint64_t total) {
        if (total > 0)
            vpkc_source_report_progress(progressId, static_cast<std::int16_t>(done * ProgressComplete / total));
    };
    const Status downloaded = source.releases.DownloadAsset(asset->Version, asset->FileName, Utf8ToPath(localPath),
                                                            source.cancel.load(), progress);
    if (!downloaded.Ok()) source.Remember(downloaded.Error());
    return downloaded.Ok();
}

void ReportProgress(void* callback, std::size_t percent) {
    (*static_cast<const std::function<void(int)>*>(callback))(static_cast<int>(percent));
}

class VelopackUpdates final : public UpdateBackend {
public:
    VelopackUpdates(std::unique_ptr<SourceBridge> bridge, vpkc_update_source_t* source, vpkc_update_manager_t* manager)
        : _bridge(std::move(bridge))
        , _source(source)
        , _manager(manager) {}

    ~VelopackUpdates() override {
        if (_info != nullptr) vpkc_free_update_info(_info);
        vpkc_free_update_manager(_manager);
        vpkc_free_source(_source);
    }

    std::optional<AvailableUpdate> Pending() override {
        vpkc_asset_t* asset = nullptr;
        if (!vpkc_update_pending_restart(_manager, &asset) || asset == nullptr) return std::nullopt;
        AvailableUpdate pending = Describe(*asset, asset->Size);
        vpkc_free_asset(asset);
        return pending;
    }

    Result<std::optional<AvailableUpdate>> Check(const std::atomic<bool>* cancel) override {
        using Outcome = Result<std::optional<AvailableUpdate>>;
        _bridge->Begin(cancel);
        if (_info != nullptr) {
            vpkc_free_update_info(_info);
            _info = nullptr;
        }
        switch (vpkc_check_for_updates(_manager, &_info)) {
        case UPDATE_AVAILABLE   : break;
        case NO_UPDATE_AVAILABLE:
        case REMOTE_IS_EMPTY    : return Outcome(std::optional<AvailableUpdate>{});
        default                 : return Outcome::Failure(_bridge->Error(CheckFailed));
        }
        std::uint64_t bytes = 0;
        for (std::size_t index = 0; index < _info->DeltasToTargetCount; ++index)
            bytes += _info->DeltasToTarget[index]->Size;
        if (bytes == 0) bytes = _info->TargetFullRelease->Size;
        return Outcome(std::optional<AvailableUpdate>(Describe(*_info->TargetFullRelease, bytes)));
    }

    Status Download(const std::function<void(int)>& progress, const std::atomic<bool>* cancel) override {
        if (_info == nullptr) return Status::Failure("No update to download");
        _bridge->Begin(cancel);
        std::function<void(int)> report = progress;
        if (!vpkc_download_updates(_manager, _info, &ReportProgress, &report))
            return Status::Failure(_bridge->Error("Cannot download the update"));
        return Status::Success();
    }

    Status ApplyAfterExit(bool restart) override {
        vpkc_asset_t* asset = nullptr;
        if (!vpkc_update_pending_restart(_manager, &asset) || asset == nullptr)
            return Status::Failure("The downloaded update is gone; check for updates again");
        std::string argument = UpdatedArgument;
        std::array<char*, 1> arguments{argument.data()};
        // With a restart the user is watching: Velopack's own progress and any error of the updater stay visible.
        const bool started = vpkc_wait_exit_then_apply_updates(_manager, asset, !restart, restart, arguments.data(),
                                                               restart ? arguments.size() : 0);
        vpkc_free_asset(asset);
        if (!started) return Status::Failure(LastError("Cannot start the updater"));
        return Status::Success();
    }

private:
    std::unique_ptr<SourceBridge> _bridge; // The source's callbacks use it; freed after the source (destructor)
    vpkc_update_source_t* _source;
    vpkc_update_manager_t* _manager;
    vpkc_update_info_t* _info = nullptr;
};
} // namespace

std::unique_ptr<UpdateBackend> CreateVelopackUpdates() {
    auto bridge = std::make_unique<SourceBridge>(ReleasesSource(), ScratchFolder());
    vpkc_update_source_t* source = vpkc_new_source_custom_callback(&ReadFeed, &FreeFeed, &DownloadAsset, bridge.get());
    if (source == nullptr) return nullptr;
    vpkc_update_manager_t* manager = nullptr;
    if (!vpkc_new_update_manager_with_source(source, nullptr, nullptr, &manager)) { // Not installed by Velopack
        vpkc_free_source(source);
        return nullptr;
    }
    return std::make_unique<VelopackUpdates>(std::move(bridge), source, manager);
}
