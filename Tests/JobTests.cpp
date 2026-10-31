#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include "Jobs/Job.h"

namespace {
// Polls like the UI does each frame; a generous limit keeps a broken job from hanging the test run.
bool WaitFinished(const BackgroundJob& job) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!job.Finished() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return job.Finished();
}
} // namespace

TEST_CASE("A job reports steps, output in order and its value", "[jobs]") {
    Job<int> job("Count", 2, [](JobContext& context) -> Result<int> {
        context.Step("First");
        for (int i = 0; i < 3; ++i)
            context.Lines()("line " + std::to_string(i));
        context.Step("Second");
        context.Output("done");
        return 42;
    });
    REQUIRE(WaitFinished(job));
    CHECK(job.State() == JobState::Succeeded);
    CHECK(job.Title() == "Count");
    CHECK(job.StepCount() == 2);
    CHECK(job.StepIndex() == 2);
    CHECK(job.StepText() == "Second");
    REQUIRE(job.Outcome());
    REQUIRE(job.Outcome()->Ok());
    CHECK(job.Outcome()->Value() == 42);

    std::size_t next = 0;
    CHECK(job.LinesFrom(next) == std::vector<std::string>{"line 0", "line 1", "line 2", "done"});
    CHECK(next == 4);
    CHECK(job.LinesFrom(next).empty()); // A reader gets each line once
}

TEST_CASE("A failing job keeps the core's message", "[jobs]") {
    Job<void> job("Fail", 1, [](JobContext&) { return Status::Failure("The editor build failed"); });
    REQUIRE(WaitFinished(job));
    CHECK(job.State() == JobState::Failed);
    CHECK(job.Error() == "The editor build failed");
    REQUIRE(job.Outcome());
    CHECK_FALSE(job.Outcome()->Ok());
}

TEST_CASE("An exception from the body fails the job instead of escaping", "[jobs]") {
    Job<void> job("Throw", 0, [](JobContext&) -> Status { throw std::runtime_error("Disk is full"); });
    REQUIRE(WaitFinished(job));
    CHECK(job.State() == JobState::Failed);
    CHECK(job.Error() == "Disk is full");
    CHECK_FALSE(job.Outcome());
}

TEST_CASE("Canceling stops the body and the job ends canceled", "[jobs]") {
    std::atomic<bool> started = false;
    Job<void> job("Wait", 1, [&](JobContext& context) {
        started = true;
        while (!*context.CancelFlag())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return Status::Failure("Canceled");
    });
    while (!started)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(job.State() == JobState::Running);
    job.Cancel();
    REQUIRE(WaitFinished(job));
    CHECK(job.State() == JobState::Canceled);
    CHECK(job.Error().empty());
}

TEST_CASE("A job that succeeds despite a late cancel still succeeds", "[jobs]") {
    std::atomic<bool> release = false;
    Job<void> job("Finish", 1, [&](JobContext&) {
        while (!release)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return Status::Success();
    });
    job.Cancel();
    release = true;
    REQUIRE(WaitFinished(job));
    CHECK(job.State() == JobState::Succeeded);
}

TEST_CASE("Destroying a running job cancels it and waits for its thread", "[jobs]") {
    std::atomic<bool> ended = false;
    {
        auto job = std::make_unique<Job<void>>("Long", 1, [&](JobContext& context) {
            while (!context.CancelRequested())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ended = true;
            return Status::Success();
        });
    }
    CHECK(ended);
}

TEST_CASE("Only the newest output lines are kept; a late reader starts at the oldest", "[jobs]") {
    const std::size_t total = BackgroundJob::MaxLines + 10;
    Job<void> job("Flood", 1, [&](JobContext& context) {
        for (std::size_t i = 0; i < total; ++i)
            context.Output(std::to_string(i));
        return Status::Success();
    });
    REQUIRE(WaitFinished(job));
    CHECK(job.LineCount() == total);
    std::size_t next = 0;
    const std::vector<std::string> lines = job.LinesFrom(next);
    REQUIRE(lines.size() == BackgroundJob::MaxLines);
    CHECK(lines.front() == "10");
    CHECK(lines.back() == std::to_string(total - 1));
    CHECK(next == total);
}
