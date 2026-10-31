#include "Jobs/BackgroundJob.h"
#include <algorithm>
#include <exception>
#include <utility>

void JobContext::Step(std::string text) { _job.SetStep(std::move(text)); }
void JobContext::Output(const std::string& line) { _job.AddLine(line); }
OutputLine JobContext::Lines() {
    return [this](const std::string& line) { _job.AddLine(line); };
}
const std::atomic<bool>* JobContext::CancelFlag() const { return &_job._cancel; }
bool JobContext::CancelRequested() const { return _job._cancel; }

BackgroundJob::BackgroundJob(std::string title, int stepCount)
    : _title(std::move(title))
    , _stepCount(stepCount) {}

BackgroundJob::~BackgroundJob() { Stop(); }

void BackgroundJob::Start(std::function<std::string(JobContext&)> body) {
    _thread = std::thread([this, body = std::move(body)] {
        JobContext context(*this);
        std::string error;
        try {
            error = body(context);
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "Unexpected error";
        }
        const std::scoped_lock lock(_mutex);
        if (error.empty())
            _state = JobState::Succeeded;
        else if (_cancel)
            _state = JobState::Canceled;
        else {
            _state = JobState::Failed;
            _error = std::move(error);
        }
    });
}

void BackgroundJob::Stop() {
    _cancel = true;
    if (_thread.joinable()) _thread.join();
}

JobState BackgroundJob::State() const {
    const std::scoped_lock lock(_mutex);
    return _state;
}

std::string BackgroundJob::StepText() const {
    const std::scoped_lock lock(_mutex);
    return _stepText;
}

int BackgroundJob::StepIndex() const {
    const std::scoped_lock lock(_mutex);
    return _stepIndex;
}

std::string BackgroundJob::Error() const {
    const std::scoped_lock lock(_mutex);
    return _error;
}

std::vector<std::string> BackgroundJob::LinesFrom(std::size_t& next) const {
    const std::scoped_lock lock(_mutex);
    next = std::clamp(next, _firstLine, _firstLine + _lines.size());
    std::vector<std::string> lines(_lines.begin() + static_cast<std::ptrdiff_t>(next - _firstLine), _lines.end());
    next = _firstLine + _lines.size();
    return lines;
}

std::size_t BackgroundJob::LineCount() const {
    const std::scoped_lock lock(_mutex);
    return _firstLine + _lines.size();
}

void BackgroundJob::AddLine(const std::string& line) {
    const std::scoped_lock lock(_mutex);
    _lines.push_back(line);
    if (_lines.size() > MaxLines) {
        _lines.pop_front();
        ++_firstLine;
    }
}

void BackgroundJob::SetStep(std::string text) {
    const std::scoped_lock lock(_mutex);
    _stepText = std::move(text);
    ++_stepIndex;
}
