#pragma once

#include "Jobs/Job.h"
#include "Platform/Result.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

// Updating the launcher itself (Docs/CoreApi.md → Launcher update). Idle: nothing known (also after a failed automatic
// check: being offline is not an error). UpToDate and Failed are only shown when the user asked.
enum class UpdatePhase { Idle, Checking, UpToDate, Available, Downloading, Ready, WaitingForCommands, Failed };

struct AvailableUpdate {
    std::string version;
    std::uint64_t downloadBytes = 0; // The deltas when they apply, otherwise the full package
    std::string notes;               // Markdown; may be empty
};

// What the update package tool does; Apps/Ui/Update implements it with Velopack so that the core does not link it.
// Calls run one at a time on a job's thread, except ApplyAfterExit (UI thread, no job running).
class UpdateBackend {
public:
    virtual ~UpdateBackend() = default;
    // An update already downloaded and waiting for a restart.
    [[nodiscard]] virtual std::optional<AvailableUpdate> Pending() = 0;
    // A newer version than the running one; nullopt when up to date.
    [[nodiscard]] virtual Result<std::optional<AvailableUpdate>> Check(const std::atomic<bool>* cancel) = 0;
    // Downloads what the last Check found; `progress` gets 0-100.
    [[nodiscard]] virtual Status Download(const std::function<void(int)>& progress,
                                          const std::atomic<bool>* cancel) = 0;
    // Starts the updater, which waits for this process to exit (60 s, then kills it), installs and optionally restarts.
    [[nodiscard]] virtual Status ApplyAfterExit(bool restart) = 0;
};

// The update's state machine. `commandsRunning` says whether anything an update would kill is running
// (System/CommandActivity.h): Velopack closes every program in the install folder without asking.
class UpdateController {
public:
    UpdateController(std::unique_ptr<UpdateBackend> backend, std::function<bool()> commandsRunning);
    ~UpdateController();

    // No backend: a development build, never installed by Velopack.
    [[nodiscard]] bool Supported() const { return _backend != nullptr; }
    [[nodiscard]] UpdatePhase Phase() const { return _phase; }
    // The update known so far: from Available on, and kept through a failed download.
    [[nodiscard]] const std::optional<AvailableUpdate>& Update() const { return _update; }
    [[nodiscard]] int Progress() const { return _progress; } // 0-100 while Downloading
    [[nodiscard]] const std::string& Error() const { return _error; }
    // The updater has started: the launcher must exit now.
    [[nodiscard]] bool QuitRequested() const { return _quit; }

    // Collects finished work and starts a waiting restart once nothing runs. Once per frame.
    void Tick();
    // Ignored during a download and once an update is ready. `userAsked`: report UpToDate and failures, also of a check
    // already running.
    void Check(bool userAsked);
    void Download(); // From Available or Failed
    void CancelDownload();
    void Restart(); // From Ready or Failed; waits for running commands first
    void CancelRestart();
    // Failed / UpToDate have been seen: back to what is known.
    void Dismiss();

private:
    struct CheckOutcome {
        std::optional<AvailableUpdate> update;
        bool ready = false;
    };

    void FinishCheck();
    void FinishDownload();
    void Apply();
    void Fail(std::string error);

    std::unique_ptr<UpdateBackend> _backend;
    std::function<bool()> _commandsRunning;
    UpdatePhase _phase = UpdatePhase::Idle;
    std::optional<AvailableUpdate> _update;
    std::string _error;
    std::atomic<int> _progress = 0;
    bool _userAsked = false;
    bool _downloaded = false; // Failed: after the download, so retrying means restarting
    bool _quit = false;
    // Jobs last: destroyed (canceled and joined) before what they use.
    std::unique_ptr<Job<CheckOutcome>> _checkJob;
    std::unique_ptr<Job<void>> _downloadJob;

public:
    UpdateController(const UpdateController&) = delete;
    UpdateController& operator=(const UpdateController&) = delete;
};
