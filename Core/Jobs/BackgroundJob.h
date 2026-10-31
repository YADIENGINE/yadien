#pragma once

#include "Platform/Process.h"
#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// A long launcher operation on its own thread (design D15 → UI hazırlığı). The UI thread only reads its state each
// frame and may cancel it. Job<T> (Jobs/Job.h) adds the typed result.
enum class JobState { Running, Succeeded, Failed, Canceled };

class BackgroundJob;

// What the operation's body sees: progress steps, output lines and the cancel flag. Callable from the job's thread
// (and from threads the body waits on, e.g. RunCaptured's reader).
class JobContext {
public:
    // Starts the next step ("Building editor"); steps are counted from 1.
    void Step(std::string text);
    void Output(const std::string& line);
    // Output as a callback for core operations (RunCaptured, CreateProject ...).
    [[nodiscard]] OutputLine Lines();
    [[nodiscard]] const std::atomic<bool>* CancelFlag() const;
    [[nodiscard]] bool CancelRequested() const;

private:
    friend class BackgroundJob;
    explicit JobContext(BackgroundJob& job)
        : _job(job) {}

    BackgroundJob& _job;
};

class BackgroundJob {
public:
    virtual ~BackgroundJob();

    [[nodiscard]] const std::string& Title() const { return _title; }
    [[nodiscard]] JobState State() const;
    [[nodiscard]] bool Finished() const { return State() != JobState::Running; }
    // The current step's text and number (1-based; 0 before the first step) out of StepCount (0: not known).
    [[nodiscard]] std::string StepText() const;
    [[nodiscard]] int StepIndex() const;
    [[nodiscard]] int StepCount() const { return _stepCount; }
    // Failed: one sentence (the core's error message).
    [[nodiscard]] std::string Error() const;

    // Output lines are numbered from 0 in arrival order; only the newest MaxLines are kept. LinesFrom(next) returns
    // the lines from number `next` on (from the oldest kept one when `next` was dropped) and moves `next` past them:
    // a reader keeps its own `next` and gets every new line once.
    [[nodiscard]] std::vector<std::string> LinesFrom(std::size_t& next) const;
    [[nodiscard]] std::size_t LineCount() const;

    // Asks the body to stop; a running child process tree is ended (RunCaptured). The state becomes Canceled when the
    // body returns without success.
    void Cancel() { _cancel = true; }
    [[nodiscard]] bool CancelRequested() const { return _cancel; }

    static constexpr std::size_t MaxLines = 5000;

protected:
    BackgroundJob(std::string title, int stepCount);
    // Runs `body` on the job's thread; called once by the derived constructor. The body returns the error message,
    // empty on success.
    void Start(std::function<std::string(JobContext&)> body);
    // Cancels and joins; the derived destructor calls it before its members (the result) go away.
    void Stop();

private:
    friend class JobContext;
    void AddLine(const std::string& line);
    void SetStep(std::string text);

    const std::string _title;
    const int _stepCount;
    std::atomic<bool> _cancel = false;
    mutable std::mutex _mutex;
    JobState _state = JobState::Running;
    std::string _stepText;
    int _stepIndex = 0;
    std::string _error;
    std::deque<std::string> _lines;
    std::size_t _firstLine = 0; // Number of _lines.front()
    std::thread _thread;        // Last: started after every other member exists

public:
    BackgroundJob(const BackgroundJob&) = delete;
    BackgroundJob& operator=(const BackgroundJob&) = delete;
};
