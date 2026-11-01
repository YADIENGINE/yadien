#include "Update/UpdateController.h"
#include <utility>

UpdateController::UpdateController(std::unique_ptr<UpdateBackend> backend, std::function<bool()> commandsRunning)
    : _backend(std::move(backend))
    , _commandsRunning(std::move(commandsRunning)) {}

UpdateController::~UpdateController() = default;

void UpdateController::Tick() {
    FinishCheck();
    FinishDownload();
    if (_phase == UpdatePhase::WaitingForCommands && !_commandsRunning()) Apply();
}

void UpdateController::Check(bool userAsked) {
    if (_checkJob) { // The user asking during the automatic check gets its result
        _userAsked = _userAsked || userAsked;
        return;
    }
    if (!_backend || _downloadJob || _phase == UpdatePhase::Ready || _phase == UpdatePhase::WaitingForCommands) return;
    _userAsked = userAsked;
    _error.clear();
    _phase = UpdatePhase::Checking;
    _checkJob = std::make_unique<Job<CheckOutcome>>("Check for updates", 1, [this](JobContext& context) {
        if (std::optional<AvailableUpdate> pending = _backend->Pending())
            return Result<CheckOutcome>(CheckOutcome{std::move(pending), true});
        Result<std::optional<AvailableUpdate>> found = _backend->Check(context.CancelFlag());
        if (!found.Ok()) return Result<CheckOutcome>::Failure(found.Error());
        return Result<CheckOutcome>(CheckOutcome{std::move(found).Value(), false});
    });
}

void UpdateController::Download() {
    if (!_backend || !_update || _checkJob || _downloadJob ||
        (_phase != UpdatePhase::Available && _phase != UpdatePhase::Failed))
        return;
    _error.clear();
    _progress = 0;
    _phase = UpdatePhase::Downloading;
    _downloadJob = std::make_unique<Job<void>>("Download update", 1, [this](JobContext& context) {
        return _backend->Download([this](int percent) { _progress = percent; }, context.CancelFlag());
    });
}

void UpdateController::CancelDownload() {
    if (_downloadJob) _downloadJob->Cancel();
}

void UpdateController::Restart() {
    if (!_backend || (_phase != UpdatePhase::Ready && !(_phase == UpdatePhase::Failed && _downloaded))) return;
    _error.clear();
    _phase = UpdatePhase::WaitingForCommands;
    if (!_commandsRunning()) Apply();
}

void UpdateController::CancelRestart() {
    if (_phase == UpdatePhase::WaitingForCommands) _phase = UpdatePhase::Ready;
}

void UpdateController::Dismiss() {
    if (_phase != UpdatePhase::Failed && _phase != UpdatePhase::UpToDate) return;
    _error.clear();
    _phase = _downloaded ? UpdatePhase::Ready : _update ? UpdatePhase::Available : UpdatePhase::Idle;
}

void UpdateController::FinishCheck() {
    if (!_checkJob || !_checkJob->Finished()) return;
    if (_checkJob->State() == JobState::Succeeded) {
        CheckOutcome outcome = _checkJob->Outcome()->Value();
        _update = std::move(outcome.update);
        _downloaded = outcome.ready;
        _phase = outcome.ready ? UpdatePhase::Ready : _update ? UpdatePhase::Available : UpdatePhase::UpToDate;
        if (_phase == UpdatePhase::UpToDate && !_userAsked) _phase = UpdatePhase::Idle;
    } else if (_userAsked) {
        Fail(_checkJob->Error());
    } else {
        _phase = _update ? UpdatePhase::Available : UpdatePhase::Idle;
    }
    _checkJob.reset();
}

void UpdateController::FinishDownload() {
    if (!_downloadJob || !_downloadJob->Finished()) return;
    switch (_downloadJob->State()) {
    case JobState::Succeeded:
        _downloaded = true;
        _phase = UpdatePhase::Ready;
        break;
    case JobState::Canceled: _phase = UpdatePhase::Available; break;
    case JobState::Failed  :
    case JobState::Running : Fail(_downloadJob->Error()); break;
    }
    _downloadJob.reset();
}

void UpdateController::Apply() {
    if (Status applied = _backend->ApplyAfterExit(true); applied.Ok())
        _quit = true;
    else
        Fail(applied.Error());
}

void UpdateController::Fail(std::string error) {
    _error = std::move(error);
    _phase = UpdatePhase::Failed;
}
