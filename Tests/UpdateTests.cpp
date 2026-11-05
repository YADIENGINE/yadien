#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include "Settings/LauncherSettings.h"
#include "Support/TempFolder.h"
#include "System/CommandActivity.h"
#include "Update/ReleaseSource.h"
#include "Update/UpdateController.h"
#include "Update/UpdateNotes.h"

namespace {
std::string FileUrl(const std::filesystem::path& file) { return "file:///" + file.generic_string(); }

// A scripted backend; Download blocks until released or canceled, as a real transfer does.
struct FakeState {
    std::optional<AvailableUpdate> pending;
    std::optional<AvailableUpdate> found;
    std::string checkError;
    std::string downloadError;
    std::string applyError;
    std::atomic<bool> releaseDownload = false;
    int applied = 0;
    bool appliedWithRestart = false;
};

class FakeBackend final : public UpdateBackend {
public:
    explicit FakeBackend(FakeState& state)
        : _state(state) {}
    std::optional<AvailableUpdate> Pending() override { return _state.pending; }
    Result<std::optional<AvailableUpdate>> Check(const std::atomic<bool>*) override {
        if (!_state.checkError.empty()) return Result<std::optional<AvailableUpdate>>::Failure(_state.checkError);
        return _state.found;
    }
    Status Download(const std::function<void(int)>& progress, const std::atomic<bool>* cancel) override {
        progress(40);
        while (!_state.releaseDownload && !cancel->load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (cancel->load()) return Status::Failure("canceled");
        if (!_state.downloadError.empty()) return Status::Failure(_state.downloadError);
        return Status::Success();
    }
    Status ApplyAfterExit(bool restart) override {
        ++_state.applied;
        _state.appliedWithRestart = restart;
        if (!_state.applyError.empty()) return Status::Failure(_state.applyError);
        return Status::Success();
    }

private:
    FakeState& _state;
};

const AvailableUpdate SampleUpdate{"9.9.9", 1234, "- Better"};

void TickUntil(UpdateController& controller, UpdatePhase phase) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (controller.Phase() != phase && std::chrono::steady_clock::now() < until) {
        controller.Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(controller.Phase() == phase);
}

// Waits for a check that may leave the phase where it was: nothing to wait on but the phase leaving Checking.
void TickUntilChecked(UpdateController& controller) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (controller.Phase() == UpdatePhase::Checking && std::chrono::steady_clock::now() < until) {
        controller.Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(controller.Phase() != UpdatePhase::Checking);
}

UpdateController MakeController(FakeState& state, const std::function<bool()>& busy = [] { return false; }) {
    return UpdateController(std::make_unique<FakeBackend>(state), busy);
}
} // namespace

TEST_CASE("Releases URL comes from the repository") {
    CHECK(ReleasesUrl("https://github.com/owner/repo") == "https://github.com/owner/repo/releases");
    CHECK(ReleasesUrl("https://github.com/owner/repo/") == ReleasesUrl("https://github.com/owner/repo"));
    CHECK(ReleasesUrl("https://github.com/owner/repo.git") == ReleasesUrl("https://github.com/owner/repo"));
    CHECK(ReleasesUrl("https://example.com/owner/repo").empty());
}

TEST_CASE("Release source reads the latest feed and each package from its version") {
    TempFolder temp;
    temp.Write("Releases/latest/download/releases.win.json", R"({"Assets":[{"FileName":"App-2-full.nupkg"}]})");
    temp.Write("Releases/download/v2.0.0/App-2-full.nupkg", "package");
    ReleaseSource source(FileUrl(temp.Path() / "Releases"), temp.Path() / "Scratch");
    const auto feed = source.Feed("releases.win.json", nullptr);
    REQUIRE(feed.Ok());
    CHECK(feed.Value() == R"({"Assets":[{"FileName":"App-2-full.nupkg"}]})");
    CHECK_FALSE(std::filesystem::exists(temp.Path() / "Scratch"));

    REQUIRE(source.DownloadAsset("2.0.0", "App-2-full.nupkg", temp.Path() / "Package.nupkg", nullptr, {}).Ok());
    CHECK(temp.Read("Package.nupkg") == "package");
    CHECK_FALSE(source.DownloadAsset("1.0.0", "App-2-full.nupkg", temp.Path() / "Missing.nupkg", nullptr, {}).Ok());
}

TEST_CASE("Release source reports an unreachable server") {
    TempFolder temp;
    ReleaseSource source(FileUrl(temp.Path() / "Missing"), temp.Path() / "Scratch");
    const auto feed = source.Feed("releases.win.json", nullptr);
    REQUIRE_FALSE(feed.Ok());
    CHECK(feed.Error().find("releases.win.json") != std::string::npos);
}

TEST_CASE("Update notes tell headings, items and breaking changes apart") {
    const auto notes =
        ParseUpdateNotes("Early preview.\r\n\nLauncher 1:\n\n- Faster\n* Smaller\n\n# Engine\n- Breaking: Renamed x\n");
    REQUIRE(notes.size() == 6);
    CHECK(notes[0].kind == UpdateNoteKind::Paragraph);
    CHECK(notes[0].text == "Early preview.");
    CHECK(notes[1].kind == UpdateNoteKind::Heading);
    CHECK(notes[1].text == "Launcher 1");
    CHECK(notes[2].kind == UpdateNoteKind::Item);
    CHECK(notes[3].text == "Smaller");
    CHECK(notes[4].kind == UpdateNoteKind::Heading);
    CHECK(notes[4].text == "Engine");
    CHECK(notes[5].kind == UpdateNoteKind::Breaking);
    CHECK(notes[5].text == "Renamed x");
    CHECK(ParseUpdateNotes("").empty());
}

TEST_CASE("Download sizes read as KB or MB") {
    CHECK(FormatDownloadSize(1) == "1 KB");
    CHECK(FormatDownloadSize(1024 * 1024 * 9) == "9.0 MB");
}

TEST_CASE("A running command marks its install folder until it ends") {
    TempFolder temp;
    const auto folder = temp.Path() / "Install";
    CHECK_FALSE(CommandsRunning(folder));
    {
        const CommandActivity first(folder);
        CHECK(CommandsRunning(folder));
        CHECK(CommandsRunning(temp.Path() / "INSTALL")); // Windows paths ignore case
        CHECK_FALSE(CommandsRunning(temp.Path() / "Other"));
        {
            const CommandActivity second(folder); // Several commands at once
        }
        CHECK(CommandsRunning(folder));
    }
    CHECK_FALSE(CommandsRunning(folder));
}

TEST_CASE("Without a backend nothing happens") {
    UpdateController controller(nullptr, [] { return false; });
    CHECK_FALSE(controller.Supported());
    controller.Check(true);
    CHECK(controller.Phase() == UpdatePhase::Idle);
}

TEST_CASE("An automatic check stays silent unless it finds something") {
    FakeState state;
    SECTION("up to date") {
        auto controller = MakeController(state);
        controller.Check(false);
        TickUntilChecked(controller);
        CHECK(controller.Phase() == UpdatePhase::Idle);
    }
    SECTION("offline") {
        state.checkError = "offline";
        auto controller = MakeController(state);
        controller.Check(false);
        TickUntilChecked(controller);
        CHECK(controller.Phase() == UpdatePhase::Idle);
        CHECK(controller.Error().empty());
    }
    SECTION("found") {
        state.found = SampleUpdate;
        auto controller = MakeController(state);
        controller.Check(false);
        TickUntil(controller, UpdatePhase::Available);
        CHECK(controller.Update()->version == SampleUpdate.version);
    }
}

TEST_CASE("A check the user asked for reports its result") {
    FakeState state;
    SECTION("up to date") {
        auto controller = MakeController(state);
        controller.Check(true);
        TickUntil(controller, UpdatePhase::UpToDate);
        controller.Dismiss();
        CHECK(controller.Phase() == UpdatePhase::Idle);
    }
    SECTION("failure") {
        state.checkError = "offline";
        auto controller = MakeController(state);
        controller.Check(true);
        TickUntil(controller, UpdatePhase::Failed);
        CHECK(controller.Error() == "offline");
        controller.Dismiss();
        CHECK(controller.Phase() == UpdatePhase::Idle);
    }
}

TEST_CASE("Asking during the automatic check reports its result") {
    FakeState state;
    auto controller = MakeController(state);
    controller.Check(false);
    controller.Check(true);
    TickUntil(controller, UpdatePhase::UpToDate);
}

TEST_CASE("An update downloaded earlier is ready at the next start") {
    FakeState state;
    state.pending = SampleUpdate;
    auto controller = MakeController(state);
    controller.Check(false);
    TickUntil(controller, UpdatePhase::Ready);
    CHECK(controller.Update()->version == SampleUpdate.version);
}

TEST_CASE("Download can be canceled, can fail and can be retried") {
    FakeState state;
    state.found = SampleUpdate;
    auto controller = MakeController(state);
    controller.Check(false);
    TickUntil(controller, UpdatePhase::Available);

    controller.Download();
    CHECK(controller.Phase() == UpdatePhase::Downloading);
    controller.CancelDownload();
    TickUntil(controller, UpdatePhase::Available);
    CHECK(controller.Error().empty());

    state.downloadError = "disk full";
    state.releaseDownload = true;
    controller.Download();
    TickUntil(controller, UpdatePhase::Failed);
    CHECK(controller.Error() == "disk full");
    CHECK(controller.Update().has_value()); // The notes stay on screen; Try again does not need a new check

    state.downloadError.clear();
    controller.Download();
    TickUntil(controller, UpdatePhase::Ready);
}

TEST_CASE("Closing during a download stops it") {
    FakeState state;
    state.found = SampleUpdate;
    {
        auto controller = MakeController(state);
        controller.Check(false);
        TickUntil(controller, UpdatePhase::Available);
        controller.Download();
    } // The destructor cancels and joins; a blocking download would hang here
    SUCCEED();
}

TEST_CASE("A restart waits until no command runs") {
    FakeState state;
    state.pending = SampleUpdate;
    std::atomic<bool> busy = true;
    auto controller = MakeController(state, [&busy] { return busy.load(); });
    controller.Check(false);
    TickUntil(controller, UpdatePhase::Ready);

    controller.Restart();
    controller.Tick();
    CHECK(controller.Phase() == UpdatePhase::WaitingForCommands);
    CHECK(state.applied == 0);
    CHECK_FALSE(controller.QuitRequested());

    controller.CancelRestart();
    CHECK(controller.Phase() == UpdatePhase::Ready);

    controller.Restart();
    busy = false;
    controller.Tick();
    CHECK(state.applied == 1);
    CHECK(state.appliedWithRestart);
    CHECK(controller.QuitRequested());
}

TEST_CASE("A failed apply keeps the launcher open and can be retried") {
    FakeState state;
    state.pending = SampleUpdate;
    state.applyError = "no updater";
    auto controller = MakeController(state);
    controller.Check(false);
    TickUntil(controller, UpdatePhase::Ready);
    controller.Restart();
    CHECK(controller.Phase() == UpdatePhase::Failed);
    CHECK_FALSE(controller.QuitRequested());
    controller.Dismiss();
    CHECK(controller.Phase() == UpdatePhase::Ready);
    state.applyError.clear();
    controller.Restart();
    CHECK(controller.QuitRequested());
}

TEST_CASE("Update settings survive a save") {
    TempFolder temp;
    const auto file = temp.Path() / "Settings.json";
    LauncherSettings settings;
    CHECK(settings.checkForUpdates);
    settings.checkForUpdates = false;
    settings.skippedUpdate = "1.2.3";
    REQUIRE(SaveLauncherSettings(file, settings).Ok());
    const auto loaded = LoadLauncherSettings(file);
    REQUIRE(loaded.Ok());
    CHECK_FALSE(loaded.Value().checkForUpdates);
    CHECK(loaded.Value().skippedUpdate == "1.2.3");
}
