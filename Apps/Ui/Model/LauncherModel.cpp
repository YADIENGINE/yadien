#include "Model/LauncherModel.h"
#include <algorithm>
#include <utility>
#include "Cli/EngineCommand.h"
#include "Developer/DeveloperGit.h"
#include "Engines/EngineCatalog.h"
#include "Dialogs/FolderDialog.h"
#include "Platform/FileSystem.h"
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

// A faster start, never a requirement: without it the project builds the engine from source.
void FetchPrebuiltEngine(const OnlineEngine& engine, const std::filesystem::path& folder, JobContext& context) {
    if (Status fetched = DownloadPrebuiltEngine(engine, folder, context.CancelFlag()); !fetched.Ok())
        context.Output("warning: " + fetched.Error());
}
} // namespace

LauncherModel::LauncherModel(LauncherLayout layout, std::unique_ptr<UpdateBackend> updates, bool justUpdated,
                             EngineTrialRunner trials)
    : _layout(std::move(layout))
    , _engines(EngineLibrary::Scan(_layout.enginesFolder, _layout.EngineCacheFolder()))
    , _trials(std::move(trials))
    , _updates(std::move(updates), [this] { return _job.has_value() || CommandsRunning(LauncherFolder()); }) {
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
    _audioTools = InspectAudioTools(_layout.AudioToolsFolder(), AudioPackages());
    ReloadRows();
    ReadPathState();
    StartSystemCheck();
    if (firstStart) { // D15: the first start opens the System check; saving makes the next start a normal one
        Save();
        _panel = LauncherPanel::SystemCheck;
    }
    _removeLeftoverTrial = !CommandsRunning(LauncherFolder());
    if (_settings.checkForUpdates) {
        _updates.Check(false);
        StartEngineCatalog();
    }
    if (justUpdated)
        ShowNotice(NoticeLevel::Info, std::string("Updated to ") + LAUNCHER_VERSION,
                   "Your projects and settings were not changed.");
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
    TrackTrial();
    MeasureEngines();
    _updates.Tick();
    if (_updates.QuitRequested()) _quit = true;
    if (const auto& update = _updates.Update(); update && update->version != _updateNotesVersion) {
        _updateNotesVersion = update->version;
        _updateNotes = ParseUpdateNotes(update->notes);
    }
    AnnounceUpdate();
    if (_visibleFilter != _filter) {
        _visibleFilter = _filter;
        _visible = ::VisibleRows(_rows, _filter, _settings.projectSort);
        KeepSelectionVisible();
    }
}

// The details panel always speaks about a project in the list when there is one.
void LauncherModel::KeepSelectionVisible() {
    if (_visible.empty()) return;
    if (!_selected || std::ranges::find(_visible, *_selected) == _visible.end()) _selected = _visible.front();
}

void LauncherModel::Queue(std::function<void()> action) { _requests.push_back(std::move(action)); }

// A found update opens its panel once per start, as soon as nothing else is open: the top bar's button alone is easy to
// miss. Later closes it until the next start; Skip this version keeps it closed.
void LauncherModel::AnnounceUpdate() {
    const std::optional<AvailableUpdate>& update = _updates.Update();
    if (!update || update->version == _announcedUpdate || !UpdateOffered()) return;
    const UpdatePhase phase = _updates.Phase();
    if (phase != UpdatePhase::Available && phase != UpdatePhase::Ready) return;
    if (_panel == LauncherPanel::Update) _announcedUpdate = update->version; // Already in front of the user
    if (_job || _panel != LauncherPanel::None) return;
    _announcedUpdate = update->version;
    _panel = LauncherPanel::Update;
}

// --- Navigation ---

void LauncherModel::SelectTab(LauncherTab tab) {
    Queue([this, tab] {
        _tab = tab;
        if (tab == LauncherTab::Engines) StartEngineCatalog();
    });
}

void LauncherModel::OpenPanel(LauncherPanel panel) {
    Queue([this, panel] {
        if (_job || (panel != LauncherPanel::NewProject && panel != LauncherPanel::Settings &&
                     panel != LauncherPanel::SystemCheck))
            return;
        _panel = panel;
        if (panel == LauncherPanel::Settings) ReadPathState();
        // New engine versions come without a launcher release: the list offers the newest published one
        if (panel == LauncherPanel::NewProject) StartEngineCatalog();
    });
}

void LauncherModel::ClosePanel() {
    Queue([this] {
        if (_panel == LauncherPanel::Job) return; // DismissJob / CancelJob own it
        if (_panel == LauncherPanel::EngineUpdate) {
            _planJob.reset();
            _plan.reset();
            _trial.reset();
            _switchError.clear();
        }
        if (_panel == LauncherPanel::Update) _updates.Dismiss();
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
        std::optional<OnlineEngine> online;
        if (found->info.manifest) {
            if (const OnlineEngine* listed = _engines.FindPublished(found->info.manifest->version)) online = *listed;
        }
        auto job = std::make_unique<Job<void>>(
            "Opening " + ProjectName(*found), OpenProjectSteps,
            [project = found->info, toolchain = *toolchain, online = std::move(online),
             prebuilt = _layout.EnginePrebuiltFolder()](JobContext& context) {
                // An engine downloaded before its release had a compiled copy, or by an older launcher.
                if (online && PrebuiltEngineFolder(project).empty()) FetchPrebuiltEngine(*online, prebuilt, context);
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

void LauncherModel::AskCleanBuild(std::size_t row) {
    Queue([this, row] {
        if (const ProjectRow* found = RowAt(row); found && !_job) {
            _confirmation = Confirmation::CleanBuild;
            _confirmationRoot = found->entry.root;
        }
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
        const std::optional<EngineVersion> target = change == EngineChange::Update   ? UpdateTarget(*found, _engines)
                                                    : change == EngineChange::Repair ? RepairTarget(*found, _engines)
                                                                                     : ReplaceTarget(*found, _engines);
        std::optional<EngineVersion> version = target;
        if (!version && change == EngineChange::Update) { // Nothing newer: the panel offers the other versions
            const std::vector<EngineVersion> choices = ChangeChoices(*found, _engines);
            if (!choices.empty()) version = choices.front();
        }
        if (!version) return;
        _change = change;
        _changeRoot = found->entry.root;
        _panel = LauncherPanel::EngineUpdate;
        StartEngineCatalog();
        StartPlan(*version);
    });
}

void LauncherModel::StartPlan(const EngineVersion& version) {
    const ProjectRow* row = RowFor(_changeRoot);
    if (!row) return;
    _planJob.reset();
    _plan.reset();
    _planError.clear();
    _trial.reset();
    _switchError.clear();
    _changeVersion = version;
    std::optional<AvailableEngine> local;
    if (const AvailableEngine* found = _engines.Find(version)) local = *found;
    std::optional<OnlineEngine> online;
    if (const OnlineEngine* found = _engines.FindOnline(version)) online = *found;
    _planJob = std::make_unique<Job<EngineUpdatePlan>>(
        "Planning the engine change", 2,
        [project = row->info, local = std::move(local), online = std::move(online), version,
         cache = _layout.EngineCacheFolder(),
         prebuilt = _layout.EnginePrebuiltFolder()](JobContext& context) -> Result<EngineUpdatePlan> {
            std::optional<AvailableEngine> target = local;
            if (!target) {
                if (!online)
                    return Result<EngineUpdatePlan>::Failure("Engine " + version.ToString() + " is not available");
                context.Step("Downloading engine " + version.ToString());
                Result<AvailableEngine> downloaded = DownloadEngine(*online, cache, context.CancelFlag());
                if (!downloaded.Ok()) return Result<EngineUpdatePlan>::Failure(downloaded.Error());
                target = std::move(downloaded).Value();
                FetchPrebuiltEngine(*online, prebuilt, context);
            }
            context.Step("Comparing engine files");
            return PlanEngineUpdate(project, *target);
        });
}

std::string LauncherModel::PlanStep() const { return _planJob ? _planJob->StepText() : std::string(); }

std::vector<EngineVersion> LauncherModel::ChangeVersions() const {
    const ProjectRow* row = RowFor(_changeRoot);
    return row ? ChangeChoices(*row, _engines) : std::vector<EngineVersion>{};
}

void LauncherModel::ChooseChangeVersion(const EngineVersion& version) {
    Queue([this, version] {
        if (!_job && _panel == LauncherPanel::EngineUpdate && version != _changeVersion) StartPlan(version);
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
    for (const EngineVersion& version : _engines.KnownVersions()) {
        if (const AvailableEngine* local = _engines.Find(version))
            choices.push_back({version.ToString(), version.ToString(), local, nullptr});
        else
            choices.push_back(
                {version.ToString() + " (download)", version.ToString(), nullptr, _engines.FindOnline(version)});
    }
    if (DevelopmentEngineAvailable())
        choices.push_back({std::string("dev (") + DevelopmentBranch + ")", "dev", nullptr, nullptr});
    return choices;
}

std::size_t LauncherModel::NewProjectEngine(const std::vector<EngineChoice>& choices) const {
    if (choices.empty()) return 0;
    const auto picked = std::ranges::find(choices, _newProject.engine, &EngineChoice::id);
    return picked == choices.end() ? 0 : static_cast<std::size_t>(picked - choices.begin());
}

std::string LauncherModel::NewProjectProblem() const {
    const std::vector<EngineChoice> choices = NewProjectEngines();
    const std::size_t index = NewProjectEngine(choices);
    if (index >= choices.size()) return "No engine to create the project with";
    const EngineChoice& choice = choices[index];
    NewProjectRequest request{_newProject.name, Utf8ToPath(_newProject.location), {}};
    if (choice.stable)
        request.engine = *choice.stable;
    else if (!choice.online)
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
        const std::vector<EngineChoice> choices = NewProjectEngines();
        const EngineChoice choice = choices[NewProjectEngine(choices)];
        NewProjectRequest request{_newProject.name, Utf8ToPath(_newProject.location), {}};
        if (choice.stable)
            request.engine = *choice.stable;
        else if (!choice.online)
            request.engine = DevelopmentEngine{_engines.Newest()->manifest.repository, DevelopmentBranch};
        std::optional<OnlineEngine> online;
        if (choice.online) online = *choice.online;
        const int steps = online ? 2 : 1;
        auto job = std::make_unique<Job<std::filesystem::path>>(
            "Creating " + _newProject.name, steps,
            [request, online = std::move(online), cache = _layout.EngineCacheFolder(),
             prebuilt = _layout.EnginePrebuiltFolder(),
             toolchain = *toolchain](JobContext& context) mutable -> Result<std::filesystem::path> {
                if (online) {
                    context.Step("Downloading engine " + online->version.ToString());
                    Result<AvailableEngine> engine = DownloadEngine(*online, cache, context.CancelFlag());
                    if (!engine.Ok()) return Result<std::filesystem::path>::Failure(engine.Error());
                    request.engine = std::move(engine).Value();
                    FetchPrebuiltEngine(*online, prebuilt, context);
                }
                context.Step("Creating the project");
                return ::CreateProject(request, LauncherEngineRunner(toolchain, context.CancelFlag()), context.Lines());
            });
        auto* created = job.get();
        StartJob(std::move(job), failure, [this, created] {
            const std::filesystem::path root = created->Outcome()->Value();
            RescanEngines();
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
        if (_change == EngineChange::Repair) {
            SwitchEngine();
            return;
        }
        StartTrial();
    });
}

// Times each step of the running trial from the job's step text.
void LauncherModel::TrackTrial() {
    if (!_trialRunning) return;
    const auto now = std::chrono::steady_clock::now();
    const bool finished = !_job || _job->job->Finished();
    const std::string step = finished ? std::string() : _job->job->StepText();
    if (step == _trialStep && !finished) return;
    if (!_trialStep.empty())
        _trialStepSeconds[_trialStep] = std::chrono::duration<double>(now - _trialStepStart).count();
    _trialStep = step;
    _trialStepStart = now;
    if (finished) _trialRunning = false;
}

std::vector<TrialStepView> LauncherModel::TrialSteps() const {
    // A verdict names the step that stopped it: those before it passed, those after it never ran.
    const auto stopped = _trial && _trial->verdict != TrialVerdict::Passed
                             ? std::ranges::find(_trialStepNames, _trial->check)
                             : _trialStepNames.end();
    std::vector<TrialStepView> steps;
    for (auto name = _trialStepNames.begin(); name != _trialStepNames.end(); ++name) {
        TrialStepView view{*name};
        if (const auto seconds = _trialStepSeconds.find(*name); seconds != _trialStepSeconds.end())
            view.seconds = seconds->second;
        if (_trial) {
            view.status = stopped == _trialStepNames.end() || name < stopped ? TrialStepStatus::Passed
                          : name == stopped                                  ? TrialStepStatus::Failed
                                                                             : TrialStepStatus::Waiting;
            if (_trial->verdict != TrialVerdict::Passed && stopped == _trialStepNames.end())
                view.status = view.seconds ? TrialStepStatus::Passed : TrialStepStatus::Waiting;
        } else if (_trialRunning && *name == _trialStep) {
            view.status = TrialStepStatus::Running;
            view.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - _trialStepStart).count();
        } else if (view.seconds) {
            view.status = TrialStepStatus::Passed;
        }
        steps.push_back(std::move(view));
    }
    return steps;
}

void LauncherModel::StartTrial() {
    const ProjectRow* row = RowFor(_changeRoot);
    const std::string name = row ? ProjectName(*row) : PathToUtf8(_changeRoot.filename());
    const std::string version = _plan->target.manifest.version.ToString();
    const std::string failure = "Could not try engine " + version + " on " + name;
    EngineTrialRunner run = _trials;
    if (!run) {
        const std::optional<Toolchain> toolchain = RequireToolchain(failure);
        if (!toolchain) return;
        run = [toolchain = *toolchain, folder = _layout.EngineTrialFolder()](const EngineUpdatePlan& plan,
                                                                             JobContext& context) {
            return RunEngineTrial(plan, folder, LauncherEngineRunner(toolchain, context.CancelFlag()), context);
        };
    }
    _trial.reset();
    _switchError.clear();
    _trialRunning = true;
    _trialStepNames = EngineTrialStepNames(_changeRoot);
    _trialStepSeconds.clear();
    _trialStep.clear();
    auto job = std::make_unique<Job<EngineTrial>>(
        "Trying engine " + version + " on " + name, EngineTrialSteps,
        [plan = *_plan, run = std::move(run)](JobContext& context) { return run(plan, context); });
    auto* tried = job.get();
    StartJob(std::move(job), failure, [this, tried] {
        _trial = tried->Outcome()->Value();
        _panel = LauncherPanel::EngineUpdate;
        if (_trial->verdict == TrialVerdict::Passed) SwitchEngine(); // Queued: this job is still being finished
    });
}

void LauncherModel::SwitchEngine() {
    Queue([this] {
        if (_job || !_plan) return;
        const ProjectRow* row = RowFor(_changeRoot);
        const std::string name = row ? ProjectName(*row) : PathToUtf8(_changeRoot.filename());
        const std::string version = _plan->target.manifest.version.ToString();
        _switchError.clear();
        // A refusal is not a failed job: the panel keeps the trial's result and offers to try again.
        auto job = std::make_unique<Job<std::string>>(
            "Changing the engine of " + name, 1, [plan = *_plan, version](JobContext& context) -> Result<std::string> {
                context.Step("Copying engine " + version);
                Status applied = ApplyEngineUpdate(plan);
                return applied.Ok() ? std::string() : applied.Error();
            });
        auto* switched = job.get();
        StartJob(std::move(job), "Could not change the engine of " + name, [this, switched, name, version] {
            if (!switched->Outcome()->Value().empty()) {
                _switchError = switched->Outcome()->Value();
                _panel = LauncherPanel::EngineUpdate;
                return;
            }
            _plan.reset();
            _trial.reset();
            _panel = LauncherPanel::None;
            ReloadRows();
            ShowNotice(NoticeLevel::Info, "Engine changed",
                       name + " now uses engine " + version + ". The next build compiles it from scratch.");
        });
    });
}

std::string LauncherModel::TrialReport() const {
    if (!_plan || !_trial) return {};
    const ProjectRow* row = RowFor(_changeRoot);
    return EngineTrialReport(*_plan, *_trial, row ? ProjectName(*row) : PathToUtf8(_changeRoot.filename()));
}

// --- Engine versions ---

void LauncherModel::StartEngineCatalog() {
    if (_catalogJob || _catalogRead) return;
    const AvailableEngine* newest = _engines.Newest();
    std::string url = EngineCatalogSource(newest ? newest->manifest.repository : std::string());
    if (url.empty()) { // Nothing to ask: a build without an engine or an engine without a GitHub repository
        _catalogRead = true;
        return;
    }
    const bool prereleases = !newest || IncludesPrereleases(newest->manifest.version);
    const bool removeTrial = std::exchange(_removeLeftoverTrial, false);
    _catalogError.clear();
    _catalogJob = std::make_unique<Job<std::vector<OnlineEngine>>>(
        "Reading the engine list", 1,
        [url = std::move(url), prereleases, removeTrial, trial = _layout.EngineTrialFolder(),
         scratch = _layout.dataFolder / "EngineCatalog"](JobContext& context) {
            if (removeTrial) (void)RemoveTree(trial);
            context.Step("Reading the engine list");
            return FetchEngineCatalog(url, prereleases, scratch, context.CancelFlag());
        });
}

void LauncherModel::RetryEngineCatalog() {
    Queue([this] { StartEngineCatalog(); });
}

void LauncherModel::DownloadEngineVersion(const EngineVersion& version) {
    Queue([this, version] {
        const OnlineEngine* online = _engines.FindOnline(version);
        if (_job || !online) return;
        const std::string text = version.ToString();
        auto job = std::make_unique<Job<void>>(
            "Downloading engine " + text, 1,
            [engine = *online, cache = _layout.EngineCacheFolder(), prebuilt = _layout.EnginePrebuiltFolder(),
             text](JobContext& context) -> Status {
                context.Step("Downloading and checking engine " + text);
                Result<AvailableEngine> downloaded = DownloadEngine(engine, cache, context.CancelFlag());
                if (!downloaded.Ok()) return Status::Failure(downloaded.Error());
                FetchPrebuiltEngine(engine, prebuilt, context);
                return Status::Success();
            });
        StartJob(std::move(job), "Could not download engine " + text, [this] {
            RescanEngines();
            ReloadRows();
        });
    });
}

std::optional<EngineVersion> LauncherModel::ShownEngine() const {
    const std::vector<EngineVersion> versions = _engines.KnownVersions();
    if (versions.empty()) return std::nullopt;
    if (_shownEngine && std::ranges::find(versions, *_shownEngine) != versions.end()) return _shownEngine;
    return versions.front();
}

void LauncherModel::SelectEngine(const EngineVersion& version) {
    Queue([this, version] { _shownEngine = version; });
}

std::optional<std::uintmax_t> LauncherModel::EngineSize(const EngineVersion& version) const {
    const auto found = _engineSizes.find(version);
    if (found == _engineSizes.end()) return std::nullopt;
    return found->second;
}

// One engine at a time, and only for the tab that shows them.
void LauncherModel::MeasureEngines() {
    if (_engineSizeJob) {
        if (!_engineSizeJob->Finished()) return;
        const bool measured = _engineSizeJob->State() == JobState::Succeeded;
        _engineSizes[_measuringEngine] = measured ? _engineSizeJob->Outcome()->Value() : 0; // 0: reads as unknown
        _engineSizeJob.reset();
    }
    if (_tab != LauncherTab::Engines) return;
    for (const AvailableEngine& engine : _engines.Engines()) {
        if (_engineSizes.contains(engine.manifest.version)) continue;
        _measuringEngine = engine.manifest.version;
        _engineSizeJob = std::make_unique<Job<std::uintmax_t>>(
            "Measuring an engine", 1,
            [folder = engine.folder](JobContext&) -> Result<std::uintmax_t> { return FolderSize(folder); });
        return;
    }
}

std::string LauncherModel::EngineRemovalProblem(const EngineVersion& version) const {
    const AvailableEngine* engine = _engines.Find(version);
    if (!engine) return "This version is not on this computer.";
    if (!engine->downloaded) return "This version comes with the launcher.";
    if (ProjectsUsing(version) > 0) return "A project uses this version.";
    return {};
}

void LauncherModel::AskRemoveEngine(const EngineVersion& version) {
    Queue([this, version] {
        if (_job || !EngineRemovalProblem(version).empty()) return;
        _confirmation = Confirmation::RemoveEngine;
        _confirmationEngine = version;
    });
}

void LauncherModel::ShowEngineFolder(const EngineVersion& version) {
    Queue([this, version] {
        if (const AvailableEngine* engine = _engines.Find(version))
            if (Status opened = OpenFolder(engine->folder); !opened.Ok())
                ShowNotice(NoticeLevel::Error, "Could not show the folder", opened.Error());
    });
}

void LauncherModel::RescanEngines() {
    std::vector<OnlineEngine> online = _engines.Online();
    _engines = EngineLibrary::Scan(_layout.enginesFolder, _layout.EngineCacheFolder());
    _engines.SetOnline(std::move(online));
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

bool LauncherModel::LicenseNoticesAvailable() const {
    std::error_code error;
    return std::filesystem::is_regular_file(LauncherFolder() / LicenseNoticesFile, error);
}

void LauncherModel::OpenLicenseNotices() {
    Queue([this] {
        if (Status opened = OpenFile(LauncherFolder() / LicenseNoticesFile); !opened.Ok())
            ShowNotice(NoticeLevel::Error, "Could not open the license notices", opened.Error());
    });
}

const std::vector<AudioPackage>& LauncherModel::AudioPackages() const {
    static const std::vector<AudioPackage> none;
    return _toolchain ? _toolchain->audio : none;
}

void LauncherModel::InstallAudioTools() {
    Queue([this] {
        if (_job || AudioPackages().empty()) return;
        auto job = std::make_unique<Job<void>>(
            "Installing audio tools", 1,
            [folder = _layout.AudioToolsFolder(), packages = AudioPackages()](JobContext& context) {
                context.Step("Downloading and extracting NumPy / SciPy");
                return ::InstallAudioTools(folder, packages, context.Lines(), context.CancelFlag());
            });
        StartJob(std::move(job), "Could not install the audio tools", [this] {
            _audioTools = InspectAudioTools(_layout.AudioToolsFolder(), AudioPackages());
            _panel = LauncherPanel::Settings;
        });
    });
}

void LauncherModel::RemoveAudioTools() {
    Queue([this] {
        if (_job) return;
        if (Status removed = ::RemoveAudioTools(_layout.AudioToolsFolder()); !removed.Ok())
            ShowNotice(NoticeLevel::Error, "Could not remove the audio tools", removed.Error());
        _audioTools = InspectAudioTools(_layout.AudioToolsFolder(), AudioPackages());
    });
}

void LauncherModel::AskDeveloperMode() {
    Queue([this] {
        if (_settings.mode == LauncherMode::Developer) return;
        _confirmation = Confirmation::DeveloperMode;
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
        case Confirmation::DeveloperMode:
            _settings.mode = LauncherMode::Developer;
            _confirmation = Confirmation::None;
            Save();
            ReloadRows();
            _report.reset();
            StartSystemCheck();
            return;
        case Confirmation::CleanBuild: {
            _confirmation = Confirmation::None;
            const ProjectRow* row = RowFor(_confirmationRoot);
            if (_job || !row) return;
            auto job = std::make_unique<Job<void>>("Cleaning the build of " + ProjectName(*row), 1,
                                                   [root = row->entry.root](JobContext& context) -> Status {
                                                       context.Step("Deleting the build folders");
                                                       return CleanProjectBuild(root);
                                                   });
            StartJob(std::move(job), "Could not clean the build. Close the project's editor and game, then try again.",
                     [this] {
                         _panel = LauncherPanel::None;
                         ReloadRows();
                     });
            return;
        }
        case Confirmation::RemoveEngine: {
            const EngineVersion version = _confirmationEngine;
            const AvailableEngine* engine = _engines.Find(version);
            _confirmation = Confirmation::None;
            if (_job || !engine || !EngineRemovalProblem(version).empty()) return;
            const std::string text = version.ToString();
            auto job =
                std::make_unique<Job<void>>("Removing engine " + text, 1,
                                            [engine = *engine, text, prebuilt = _layout.EnginePrebuiltFolder() / text](
                                                JobContext& context) -> Status {
                                                context.Step("Removing engine " + text);
                                                Status removed = RemoveDownloadedEngine(engine);
                                                if (removed.Ok()) (void)RemoveTree(prebuilt);
                                                return removed;
                                            });
            StartJob(std::move(job), "Could not remove engine " + text, [this, version] {
                _panel = LauncherPanel::None;
                _engineSizes.erase(version);
                RescanEngines();
                ReloadRows();
                _catalogRead = false; // The catalog dropped this version while it was on the computer
                StartEngineCatalog();
            });
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
    _job = ForegroundJob{std::move(job), std::move(failureTitle), std::move(onSuccess),
                         std::make_unique<CommandActivity>(LauncherFolder())};
    _panel = LauncherPanel::Job;
}

// --- Launcher update ---

bool LauncherModel::UpdateOffered() const {
    const std::optional<AvailableUpdate>& update = _updates.Update();
    if (!update) return false;
    return _updates.Phase() != UpdatePhase::Available || update->version != _settings.skippedUpdate;
}

void LauncherModel::SetChecksForUpdates(bool enabled) {
    Queue([this, enabled] {
        _settings.checkForUpdates = enabled;
        Save();
    });
}

void LauncherModel::CheckForUpdates() {
    Queue([this] {
        if (_job) return;
        if (!_settings.skippedUpdate.empty()) {
            _settings.skippedUpdate.clear();
            Save();
        }
        _updates.Check(true);
        _panel = LauncherPanel::Update;
    });
}

void LauncherModel::OpenUpdatePanel() {
    Queue([this] {
        if (!_job) _panel = LauncherPanel::Update;
    });
}

void LauncherModel::DownloadUpdate() {
    Queue([this] { _updates.Download(); });
}

void LauncherModel::CancelUpdateDownload() {
    Queue([this] { _updates.CancelDownload(); });
}

void LauncherModel::SkipUpdate() {
    Queue([this] {
        if (const auto& update = _updates.Update()) _settings.skippedUpdate = update->version;
        Save();
        _panel = LauncherPanel::None;
    });
}

void LauncherModel::RestartToUpdate() {
    Queue([this] { _updates.Restart(); });
}

void LauncherModel::CancelUpdateRestart() {
    Queue([this] { _updates.CancelRestart(); });
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
        if (_engines.Find(_changeVersion) == nullptr) { // Downloaded: the list shows it as on this computer now
            RescanEngines();
            ReloadRows();
        }
    }
    if (_catalogJob && _catalogJob->Finished()) {
        if (_catalogJob->State() == JobState::Succeeded) {
            _engines.SetOnline(_catalogJob->Outcome()->Value());
            _catalogRead = true;
            ReloadRows();
        } else {
            _catalogError = _catalogJob->Error();
        }
        _catalogJob.reset();
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
    KeepSelectionVisible();
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
