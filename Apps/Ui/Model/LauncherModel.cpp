#include "Model/LauncherModel.h"
#include <algorithm>
#include <utility>
#include "Cli/EngineCommand.h"
#include "Developer/DeveloperAccess.h"
#include "Developer/DeveloperGit.h"
#include "Dialogs/FolderDialog.h"
#include "Platform/Process.h"
#include "Platform/Shell.h"
#include "Platform/Text.h"
#include "Projects/NewProject.h"
#include "Projects/ProjectLaunch.h"
#include "Projects/ProjectScan.h"
#include "Settings/ProjectList.h"
#include "System/UserPath.h"

namespace {
constexpr const char* DevelopmentBranch = "main";

std::string ProjectName(const ProjectRow& row) { return row.info.name; }
} // namespace

LauncherModel::LauncherModel(LauncherLayout layout)
    : _layout(std::move(layout))
    , _engines(EngineLibrary::Scan(_layout.enginesFolder)) {
    std::error_code error;
    const bool firstStart = !std::filesystem::exists(_layout.SettingsFile(), error);
    if (Result<LauncherSettings> settings = LoadLauncherSettings(_layout.SettingsFile()); settings.Ok())
        _settings = std::move(settings).Value();
    else
        _settingsError = settings.Error();
    if (Result<Toolchain> toolchain = LoadToolchain(_layout.toolsFolder); toolchain.Ok())
        _toolchain = std::move(toolchain).Value();
    else
        _toolchainError = toolchain.Error();
    _newProject.location = PathToUtf8(ProjectsFolderOf(_settings));
    ReloadRows();
    ReadPathState();
    StartSystemCheck();
    if (firstStart) { // D15: the first start opens the System check; saving makes the next start a normal one
        Save();
        _panel = LauncherPanel::SystemCheck;
    }
}

LauncherModel::~LauncherModel() = default;

const char* LauncherModel::ProductName() { return LAUNCHER_PRODUCT_NAME; }
const char* LauncherModel::LauncherVersion() { return LAUNCHER_VERSION; }

void LauncherModel::Tick() {
    std::vector<std::function<void()>> requests;
    requests.swap(_requests);
    for (const std::function<void()>& request : requests)
        request();
    FinishJobs();
    if (_visibleFilter != _filter) {
        _visibleFilter = _filter;
        _visible = ::VisibleRows(_rows, _filter, _settings.projectSort);
    }
}

void LauncherModel::Queue(std::function<void()> action) { _requests.push_back(std::move(action)); }

// --- Navigation ---

void LauncherModel::SelectTab(LauncherTab tab) {
    Queue([this, tab] { _tab = tab; });
}

void LauncherModel::OpenPanel(LauncherPanel panel) {
    Queue([this, panel] {
        if (_job || (panel != LauncherPanel::NewProject && panel != LauncherPanel::Settings &&
                     panel != LauncherPanel::SystemCheck))
            return;
        _panel = panel;
        if (panel == LauncherPanel::Settings) ReadPathState();
    });
}

void LauncherModel::ClosePanel() {
    Queue([this] {
        if (_panel == LauncherPanel::Job) return; // DismissJob / CancelJob own it
        if (_panel == LauncherPanel::EngineUpdate) {
            _planJob.reset();
            _plan.reset();
        }
        _panel = LauncherPanel::None;
    });
}

void LauncherModel::DismissNotice() {
    Queue([this] { _notice.reset(); });
}

void LauncherModel::StartWithDefaultSettings() {
    Queue([this] {
        _settings = LauncherSettings{};
        _settingsError.clear();
        Save();
        ReloadRows();
    });
}

// --- Projects ---

ProjectActions LauncherModel::ActionsFor(std::size_t row) const {
    const ProjectRow* found = RowAt(row);
    if (!found) return ProjectActions{.remove = false};
    ProjectActions actions = ::ActionsFor(*found, _engines, _settings.mode);
    if (_job) actions = ProjectActions{.remove = actions.remove}; // One foreground job at a time
    return actions;
}

const ProjectDetails* LauncherModel::DetailsFor(std::size_t row) const {
    const ProjectRow* found = RowAt(row);
    if (!found) return nullptr;
    const auto details = _detailsByRoot.find(found->entry.root);
    return details == _detailsByRoot.end() ? nullptr : &details->second;
}

void LauncherModel::Select(std::size_t row) {
    Queue([this, row] {
        if (row < _rows.size()) _selected = row;
    });
}

void LauncherModel::SetSort(ProjectSort sort) {
    Queue([this, sort] {
        _settings.projectSort = sort;
        _visible = ::VisibleRows(_rows, _filter, sort);
        Save();
    });
}

void LauncherModel::Create() { OpenPanel(LauncherPanel::NewProject); }

void LauncherModel::Import() {
    Queue([this] {
        const std::optional<std::filesystem::path> folder = PickFolder("Import project", ProjectsFolderOf(_settings));
        if (!folder) return;
        if (Status added = AddProject(_settings, *folder); !added.Ok()) {
            ShowNotice(NoticeLevel::Error, "Could not import the project", added.Error());
            return;
        }
        Save();
        ReloadRows();
        if (const ProjectRow* row = RowFor(CanonicalRoot(*folder)))
            _selected = static_cast<std::size_t>(row - _rows.data());
    });
}

void LauncherModel::BrowseProjectsFolder() {
    Queue([this] {
        if (_job) return;
        if (const auto folder = PickFolder("Projects folder", ProjectsFolderOf(_settings))) SetProjectsFolder(*folder);
    });
}

void LauncherModel::SetProjectsFolder(std::filesystem::path folder) {
    Queue([this, folder = std::move(folder)] {
        if (_job) return;
        std::error_code error;
        if (folder.empty() || !folder.is_absolute() || !std::filesystem::is_directory(folder, error)) {
            ShowNotice(NoticeLevel::Error, "Could not set the projects folder",
                       "Choose an existing folder with an absolute path.");
            return;
        }
        _settings.projectsFolder = CanonicalRoot(folder);
        _newProject.location = PathToUtf8(_settings.projectsFolder);
        Save();
        StartProjectScan(_settings.projectsFolder);
    });
}

void LauncherModel::StartProjectScan(const std::filesystem::path& folder) {
    auto job = std::make_unique<Job<std::vector<std::filesystem::path>>>(
        "Scanning " + PathToUtf8(folder), 1,
        [folder](JobContext& context) -> Result<std::vector<std::filesystem::path>> {
            context.Step("Looking for projects");
            std::vector<std::filesystem::path> found = FindProjects(folder, context.CancelFlag());
            if (context.CancelRequested()) return Result<std::vector<std::filesystem::path>>::Failure("Canceled");
            for (const std::filesystem::path& root : found)
                context.Output(PathToUtf8(root));
            return found;
        });
    auto* scan = job.get();
    StartJob(std::move(job), "Could not scan for projects", [this, scan] {
        int added = 0;
        for (const std::filesystem::path& root : scan->Outcome()->Value())
            if (AddProject(_settings, root).Ok()) ++added;
        Save();
        ReloadRows();
        _panel = LauncherPanel::None;
        ShowNotice(NoticeLevel::Info, "Scan finished",
                   added == 1 ? "Added 1 project" : "Added " + std::to_string(added) + " projects");
    });
}

void LauncherModel::Open(std::size_t row) {
    Queue([this, row] {
        const ProjectRow* found = RowAt(row);
        if (!found || _job || !::ActionsFor(*found, _engines, _settings.mode).open) return;
        const std::string failure = "Could not open " + ProjectName(*found);
        const std::optional<Toolchain> toolchain = RequireToolchain(failure);
        if (!toolchain) return;
        auto job = std::make_unique<Job<void>>(
            "Opening " + ProjectName(*found), OpenProjectSteps,
            [project = found->info, toolchain = *toolchain](JobContext& context) {
                return OpenProject(project, toolchain, LauncherEngineRunner(toolchain, context.CancelFlag()),
                                   StartDetached, context);
            });
        StartJob(std::move(job), failure, [this, root = found->entry.root] {
            MarkProjectOpened(_settings, root, CurrentTimeIso());
            Save();
            _quit = true; // D15: the editor takes over; "Back to project list" brings the launcher back
        });
    });
}

void LauncherModel::Run(std::size_t row) {
    Queue([this, row] {
        const ProjectRow* found = RowAt(row);
        if (!found || _job || !::ActionsFor(*found, _engines, _settings.mode).run) return;
        const std::string failure = "Could not run " + ProjectName(*found);
        const std::optional<Toolchain> toolchain = RequireToolchain(failure);
        if (!toolchain) return;
        auto job = std::make_unique<Job<void>>(
            "Running " + ProjectName(*found), RunProjectSteps,
            [project = found->info, toolchain = *toolchain](JobContext& context) {
                return RunProject(project, LauncherEngineRunner(toolchain, context.CancelFlag()), context);
            });
        StartJob(std::move(job), failure, [this] { _panel = LauncherPanel::None; });
    });
}

void LauncherModel::ShowInFolder(std::size_t row) {
    Queue([this, row] {
        if (const ProjectRow* found = RowAt(row))
            if (Status opened = OpenFolder(found->entry.root); !opened.Ok())
                ShowNotice(NoticeLevel::Error, "Could not show the folder", opened.Error());
    });
}

void LauncherModel::AskRemove(std::size_t row) {
    Queue([this, row] {
        if (const ProjectRow* found = RowAt(row)) {
            _confirmation = Confirmation::RemoveProject;
            _confirmationRoot = found->entry.root;
        }
    });
}

void LauncherModel::RemoveMissing() {
    Queue([this] {
        int removed = 0;
        for (const ProjectRow& row : _rows)
            if (row.folderMissing && RemoveProject(_settings, row.entry.root).Ok()) ++removed;
        if (removed == 0) return;
        Save();
        ReloadRows();
        ShowNotice(NoticeLevel::Info, "Removed missing projects",
                   removed == 1 ? "Removed 1 project from the list"
                                : "Removed " + std::to_string(removed) + " projects from the list");
    });
}

void LauncherModel::ChangeEngine(std::size_t row, EngineChange change) {
    Queue([this, row, change] {
        const ProjectRow* found = RowAt(row);
        if (!found || _job) return;
        const AvailableEngine* target = change == EngineChange::Update   ? UpdateTarget(*found, _engines)
                                        : change == EngineChange::Repair ? RepairTarget(*found, _engines)
                                                                         : ReplaceTarget(*found, _engines);
        if (!target) return;
        _change = change;
        _changeRoot = found->entry.root;
        _plan.reset();
        _planError.clear();
        _panel = LauncherPanel::EngineUpdate;
        _planJob = std::make_unique<Job<EngineUpdatePlan>>(
            "Planning the engine change", 1, [project = found->info, target = *target](JobContext& context) {
                context.Step("Comparing engine files");
                return PlanEngineUpdate(project, target);
            });
    });
}

void LauncherModel::UpdateEngineFromRemote(std::size_t row) {
    Queue([this, row] {
        const ProjectRow* found = RowAt(row);
        if (!found || _job || !::ActionsFor(*found, _engines, _settings.mode).engineGit) return;
        auto job = std::make_unique<Job<void>>("Updating the engine of " + ProjectName(*found), 1,
                                               [root = found->entry.root](JobContext& context) {
                                                   context.Step("Updating the engine from its repository");
                                                   return ::UpdateEngineFromRemote(root, context.Lines());
                                               });
        StartJob(std::move(job), "Could not update the engine", [this] {
            _panel = LauncherPanel::None;
            ReloadRows();
        });
    });
}

void LauncherModel::AskPublishEngine(std::size_t row) {
    Queue([this, row] {
        const ProjectRow* found = RowAt(row);
        if (!found || !::ActionsFor(*found, _engines, _settings.mode).engineGit) return;
        _confirmation = Confirmation::PublishEngine;
        _confirmationRoot = found->entry.root;
        _confirmationForm.commitMessage.clear();
    });
}

// --- Engines ---

int LauncherModel::ProjectsUsing(const EngineVersion& version) const { return ::ProjectsUsing(_rows, version); }

bool LauncherModel::DevelopmentEngineAvailable() const {
    return _settings.mode == LauncherMode::Developer && _engines.Newest() &&
           !_engines.Newest()->manifest.repository.empty();
}

// --- New project ---

std::vector<EngineChoice> LauncherModel::NewProjectEngines() const {
    std::vector<EngineChoice> choices;
    for (const AvailableEngine& engine : _engines.Engines())
        choices.push_back({engine.manifest.version.ToString(), &engine});
    if (DevelopmentEngineAvailable()) choices.push_back({std::string("dev (") + DevelopmentBranch + ")", nullptr});
    return choices;
}

std::string LauncherModel::NewProjectProblem() const {
    const std::vector<EngineChoice> choices = NewProjectEngines();
    if (_newProject.engine >= choices.size()) return "No engine to create the project with";
    const EngineChoice& choice = choices[_newProject.engine];
    NewProjectRequest request{_newProject.name, Utf8ToPath(_newProject.location), {}};
    if (choice.stable)
        request.engine = *choice.stable;
    else
        request.engine = DevelopmentEngine{_engines.Newest()->manifest.repository, DevelopmentBranch};
    const Status usable = CheckNewProjectFolder(request);
    return usable.Ok() ? std::string() : usable.Error();
}

std::filesystem::path LauncherModel::NewProjectFolder() const {
    return Utf8ToPath(_newProject.location) / Utf8ToPath(_newProject.name);
}

void LauncherModel::BrowseNewProjectLocation() {
    Queue([this] {
        if (const std::optional<std::filesystem::path> folder =
                PickFolder("Project location", Utf8ToPath(_newProject.location)))
            _newProject.location = PathToUtf8(*folder);
    });
}

void LauncherModel::CreateProject() {
    Queue([this] {
        if (_job || !NewProjectProblem().empty()) return;
        const std::string failure = "Could not create " + _newProject.name;
        const std::optional<Toolchain> toolchain = RequireToolchain(failure);
        if (!toolchain) return;
        const EngineChoice choice = NewProjectEngines()[_newProject.engine];
        NewProjectRequest request{_newProject.name, Utf8ToPath(_newProject.location), {}};
        if (choice.stable)
            request.engine = *choice.stable;
        else
            request.engine = DevelopmentEngine{_engines.Newest()->manifest.repository, DevelopmentBranch};
        auto job = std::make_unique<Job<std::filesystem::path>>(
            "Creating " + _newProject.name, 1,
            [request, toolchain = *toolchain](JobContext& context) -> Result<std::filesystem::path> {
                context.Step("Creating the project");
                return ::CreateProject(request, LauncherEngineRunner(toolchain, context.CancelFlag()), context.Lines());
            });
        auto* created = job.get();
        StartJob(std::move(job), failure, [this, created] {
            const std::filesystem::path root = created->Outcome()->Value();
            (void)AddProject(_settings, root);
            Save();
            ReloadRows();
            if (const ProjectRow* row = RowFor(CanonicalRoot(root)))
                _selected = static_cast<std::size_t>(row - _rows.data());
            _newProject.name.clear();
            _tab = LauncherTab::Projects;
            _panel = LauncherPanel::None;
        });
    });
}

// --- Engine update ---

const ProjectRow* LauncherModel::ChangeProject() const { return RowFor(_changeRoot); }

void LauncherModel::ConfirmEngineChange() {
    Queue([this] {
        if (_job || !_plan) return;
        const ProjectRow* row = RowFor(_changeRoot);
        const std::string name = row ? ProjectName(*row) : PathToUtf8(_changeRoot.filename());
        auto job =
            std::make_unique<Job<void>>("Changing the engine of " + name, 1, [plan = *_plan](JobContext& context) {
                context.Step("Copying engine " + plan.target.manifest.version.ToString());
                return ApplyEngineUpdate(plan);
            });
        StartJob(std::move(job), "Could not change the engine of " + name,
                 [this, name, version = _plan->target.manifest.version.ToString()] {
                     _plan.reset();
                     _panel = LauncherPanel::None;
                     ReloadRows();
                     ShowNotice(NoticeLevel::Info, "Engine changed", name + " now uses engine " + version);
                 });
    });
}

// --- System ---

SystemState LauncherModel::System() const {
    if (!_report) return SystemState::Checking;
    if (!_report->ReadyToBuild()) return SystemState::NotReady;
    const bool warning =
        std::ranges::any_of(_report->items, [](const CheckItem& item) { return item.status != CheckStatus::Ok; });
    return warning ? SystemState::Warning : SystemState::Ready;
}

void LauncherModel::RunSystemCheck() {
    Queue([this] {
        _report.reset();
        StartSystemCheck();
    });
}

void LauncherModel::StartSystemCheck() {
    if (_systemJob) return;
    _systemJob = std::make_unique<Job<SystemReport>>(
        "System check", 1, [layout = _layout, mode = _settings.mode](JobContext& context) -> Result<SystemReport> {
            context.Step("Checking tools");
            return ::RunSystemCheck(layout, mode);
        });
}

void LauncherModel::InstallMsvc() {
    Queue([this] {
        if (_job) return;
        auto job = std::make_unique<Job<void>>("Installing Visual Studio Build Tools", 1, [](JobContext& context) {
            context.Step("Installing with winget");
            return ::InstallMsvc(context.Lines(), context.CancelFlag());
        });
        StartJob(std::move(job), "Could not install Visual Studio Build Tools", [this] {
            _panel = LauncherPanel::SystemCheck;
            _report.reset();
            StartSystemCheck();
        });
    });
}

// --- Settings ---

std::filesystem::path LauncherModel::ProjectsFolder() const { return ProjectsFolderOf(_settings); }

bool LauncherModel::DeveloperModeAvailable() const { return EmbeddedDeveloperAccess().has_value(); }

void LauncherModel::SetOnPath(bool onPath) {
    Queue([this, onPath] {
        const Status changed = onPath ? AddToUserPath(LauncherFolder()) : RemoveFromUserPath(LauncherFolder());
        if (!changed.Ok())
            ShowNotice(NoticeLevel::Error, onPath ? "Could not add to PATH" : "Could not remove from PATH",
                       changed.Error());
        ReadPathState();
    });
}

void LauncherModel::OpenInstallFolder() {
    Queue([this] {
        if (Status opened = OpenFolder(LauncherFolder()); !opened.Ok())
            ShowNotice(NoticeLevel::Error, "Could not open the install folder", opened.Error());
    });
}

void LauncherModel::AskDeveloperMode() {
    Queue([this] {
        if (!DeveloperModeAvailable() || _settings.mode == LauncherMode::Developer) return;
        _confirmation = Confirmation::DeveloperPassword;
        _confirmationForm.password.clear();
        _passwordRejected = false;
    });
}

void LauncherModel::LeaveDeveloperMode() {
    Queue([this] {
        _settings.mode = LauncherMode::User;
        Save();
        ReloadRows();
        _report.reset();
        StartSystemCheck();
    });
}

// --- Confirmations ---

const ProjectRow* LauncherModel::PendingProject() const { return RowFor(_confirmationRoot); }

void LauncherModel::Confirm() {
    Queue([this] {
        switch (_confirmation) {
        case Confirmation::None: return;
        case Confirmation::RemoveProject:
            if (Status removed = RemoveProject(_settings, _confirmationRoot); !removed.Ok())
                ShowNotice(NoticeLevel::Error, "Could not remove the project", removed.Error());
            Save();
            ReloadRows();
            _confirmation = Confirmation::None;
            return;
        case Confirmation::DeveloperPassword: {
            const std::optional<DeveloperAccessRecord> access = EmbeddedDeveloperAccess();
            if (!access || _passwordJob) return;
            _passwordRejected = false;
            // PBKDF2 takes ~0.3 s: off the UI thread.
            _passwordJob = std::make_unique<Job<bool>>(
                "Checking the password", 1,
                [record = *access, password = _confirmationForm.password](JobContext&) -> Result<bool> {
                    return CheckDeveloperPassword(record, password);
                });
            _confirmationForm.password.clear();
            return;
        }
        case Confirmation::PublishEngine: {
            if (_job || _confirmationForm.commitMessage.empty()) return;
            const ProjectRow* row = RowFor(_confirmationRoot);
            if (!row) return;
            auto job = std::make_unique<Job<void>>(
                "Publishing the engine changes of " + ProjectName(*row), 1,
                [root = row->entry.root, message = _confirmationForm.commitMessage](JobContext& context) {
                    context.Step("Committing and pushing the engine");
                    return PublishEngineChanges(root, message, context.Lines());
                });
            _confirmation = Confirmation::None;
            StartJob(std::move(job), "Could not publish the engine changes", [this] {
                _panel = LauncherPanel::None;
                ReloadRows();
            });
            return;
        }
        }
    });
}

void LauncherModel::CancelConfirmation() {
    Queue([this] {
        _confirmation = Confirmation::None;
        _confirmationForm = {};
        _passwordJob.reset();
        _passwordRejected = false;
    });
}

// --- Job ---

const std::string& LauncherModel::JobFailureTitle() const {
    static const std::string none;
    return _job ? _job->failureTitle : none;
}

void LauncherModel::CancelJob() {
    Queue([this] {
        if (_job) _job->job->Cancel();
    });
}

void LauncherModel::DismissJob() {
    Queue([this] {
        if (!_job || !_job->job->Finished()) return;
        _job.reset();
        _jobOutput.clear();
        _panel = LauncherPanel::None;
    });
}

void LauncherModel::StartJob(std::unique_ptr<BackgroundJob> job, std::string failureTitle,
                             std::function<void()> onSuccess) {
    _jobOutput.clear();
    _jobLine = 0;
    _job = ForegroundJob{std::move(job), std::move(failureTitle), std::move(onSuccess)};
    _panel = LauncherPanel::Job;
}

void LauncherModel::FinishJobs() {
    if (_job) {
        // State first, then the lines: a finished job has written all of its output.
        const bool finished = _job->job->Finished();
        for (std::string& line : _job->job->LinesFrom(_jobLine))
            _jobOutput.push_back(std::move(line));
        if (_jobOutput.size() > BackgroundJob::MaxLines)
            _jobOutput.erase(_jobOutput.begin(), _jobOutput.begin() + static_cast<std::ptrdiff_t>(
                                                                          _jobOutput.size() - BackgroundJob::MaxLines));
        if (finished && _job->job->State() == JobState::Succeeded) {
            const std::function<void()> onSuccess = std::move(_job->onSuccess);
            onSuccess(); // Reads the job's result: the job is released after it
            _job.reset();
            _jobOutput.clear();
            if (_panel == LauncherPanel::Job) _panel = LauncherPanel::None;
        }
        // Failed / Canceled: the Job panel shows the error and the output until DismissJob.
    }
    if (_details && _details->Finished()) {
        if (_details->State() == JobState::Succeeded)
            for (const ProjectDetails& details : _details->Outcome()->Value())
                _detailsByRoot.insert_or_assign(details.root, details);
        _details.reset();
    }
    if (_systemJob && _systemJob->Finished()) {
        if (_systemJob->State() == JobState::Succeeded) _report = _systemJob->Outcome()->Value();
        _systemJob.reset();
    }
    if (_planJob && _planJob->Finished()) {
        if (_planJob->State() == JobState::Succeeded)
            _plan = _planJob->Outcome()->Value();
        else
            _planError = _planJob->Error();
        _planJob.reset();
    }
    if (_passwordJob && _passwordJob->Finished()) {
        const bool accepted = _passwordJob->State() == JobState::Succeeded && _passwordJob->Outcome()->Value();
        _passwordJob.reset();
        if (accepted) {
            _settings.mode = LauncherMode::Developer;
            _confirmation = Confirmation::None;
            Save();
            ReloadRows();
            _report.reset();
            StartSystemCheck();
        } else {
            _passwordRejected = true;
        }
    }
}

// --- Helpers ---

void LauncherModel::ReloadRows() {
    std::optional<std::filesystem::path> selected;
    if (_selected && *_selected < _rows.size()) selected = _rows[*_selected].entry.root;
    _rows = InspectProjectRows(_settings, _engines);
    _visible = ::VisibleRows(_rows, _filter, _settings.projectSort);
    _visibleFilter = _filter;
    _selected.reset();
    if (selected)
        if (const ProjectRow* row = RowFor(*selected)) _selected = static_cast<std::size_t>(row - _rows.data());
    _detailsByRoot.clear();
    StartDetails();
}

void LauncherModel::StartDetails() {
    _details.reset(); // Cancels a check of the previous list
    _details = std::make_unique<Job<std::vector<ProjectDetails>>>(
        "Checking projects", 1,
        [rows = _rows, mode = _settings.mode](JobContext& context) -> Result<std::vector<ProjectDetails>> {
            return InspectProjectDetails(rows, mode, context.CancelFlag());
        });
}

void LauncherModel::Save() {
    if (!_settingsError.empty()) return; // Never overwrite a broken file without "Start with defaults"
    if (Status saved = SaveLauncherSettings(_layout.SettingsFile(), _settings); !saved.Ok())
        ShowNotice(NoticeLevel::Error, "Could not save settings", saved.Error());
}

void LauncherModel::ShowNotice(NoticeLevel level, std::string title, std::string message) {
    _notice = Notice{level, std::move(title), std::move(message)};
}

const ProjectRow* LauncherModel::RowAt(std::size_t row) const { return row < _rows.size() ? &_rows[row] : nullptr; }

const ProjectRow* LauncherModel::RowFor(const std::filesystem::path& root) const {
    const auto found = std::ranges::find_if(_rows, [&](const ProjectRow& row) { return row.entry.root == root; });
    return found == _rows.end() ? nullptr : &*found;
}

std::optional<Toolchain> LauncherModel::RequireToolchain(const std::string& failureTitle) {
    if (!_toolchain) ShowNotice(NoticeLevel::Error, failureTitle, _toolchainError);
    return _toolchain;
}

void LauncherModel::ReadPathState() {
    const Result<bool> onPath = IsOnUserPath(LauncherFolder());
    _onPath = onPath.Ok() ? std::optional<bool>(onPath.Value()) : std::nullopt;
}
