#pragma once

#include "Engines/EngineLibrary.h"
#include "Jobs/Job.h"
#include "Projects/EngineTrial.h"
#include "Projects/EngineUpdate.h"
#include "Projects/ProjectDetails.h"
#include "Projects/ProjectRow.h"
#include "Settings/LauncherLayout.h"
#include "Settings/LauncherSettings.h"
#include "System/SystemCheck.h"
#include "System/AudioTools.h"
#include "System/CommandActivity.h"
#include "System/Toolchain.h"
#include "Update/UpdateController.h"
#include "Update/UpdateNotes.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Where the launcher is (engine Docs/Design/README.md D15 → Düzen ve gezinme). Tabs fill the window, a panel is
// drawn over the tab (at most one), a confirmation is a small popup.
enum class LauncherTab { Projects, Engines };
enum class LauncherPanel { None, NewProject, Settings, SystemCheck, EngineUpdate, Job, Update };
// Small confirmations: remove a project from the list, enter developer mode (warning), publish engine changes
// (commit message).
enum class Confirmation { None, RemoveProject, DeveloperMode, PublishEngine, RemoveEngine };
// Update: another stable version, the newest by default; Repair: the same version back over edits; Replace: a stable
// engine for an unregistered / missing one.
enum class EngineChange { Update, Repair, Replace };
// The dot in the top bar: Checking until the first report; Ready, Warning (works), NotReady (cannot build).
enum class SystemState { Checking, Ready, Warning, NotReady };
enum class NoticeLevel { Info, Warning, Error };

// A message over the current tab until dismissed or replaced. The UI shows `title` and, under it, `message` as is
// (the core's sentence).
struct Notice {
    NoticeLevel level = NoticeLevel::Info;
    std::string title;
    std::string message;
};

// New project panel fields, edited by the UI directly.
struct NewProjectForm {
    std::string name;
    std::string location;   // UTF-8 folder; the project is created at location / name
    std::size_t engine = 0; // Index into NewProjectEngines()
};

// One choice of the New project panel's engine list: a stable copy, or "dev (main)" in developer mode.
struct EngineChoice {
    std::string label;                       // "0.2.0", "dev (main)"
    const AvailableEngine* stable = nullptr; // On this computer
    const OnlineEngine* online = nullptr; // Downloaded when the project is created; both null: the development engine
};

// Text the confirmation popups edit.
struct ConfirmationForm {
    std::string commitMessage; // PublishEngine
};

// Tries an engine on a project; RunEngineTrial with the launcher's tools unless a test scripts it.
using EngineTrialRunner = std::function<Result<EngineTrial>(const EngineUpdatePlan&, JobContext&)>;

// The launcher window's whole state and every action it offers. Screens read the getters and call the actions;
// they hold no state of their own beyond ImGui's. Actions are queued and applied by Tick() at the start of the
// next frame, so a screen may call them while drawing (e.g. inside its row loop).
// All work is in LauncherCore; this class only orders it: jobs, navigation, settings saves, notices.
class LauncherModel {
public:
    // `updates`: null when the launcher cannot update itself (a development build). `justUpdated`: says so in a notice.
    explicit LauncherModel(LauncherLayout layout, std::unique_ptr<UpdateBackend> updates = nullptr,
                           bool justUpdated = false, EngineTrialRunner trials = {});
    ~LauncherModel();

    // Applies queued actions and collects finished jobs. Called once per frame before drawing.
    void Tick();
    [[nodiscard]] bool QuitRequested() const { return _quit; }

    // --- Identity and navigation ---
    [[nodiscard]] static const char* ProductName(); // From the engine manifest (never written in code)
    [[nodiscard]] static const char* LauncherVersion();
    [[nodiscard]] LauncherMode Mode() const { return _settings.mode; }
    [[nodiscard]] LauncherTab Tab() const { return _tab; }
    [[nodiscard]] LauncherPanel Panel() const { return _panel; }
    [[nodiscard]] const std::optional<Notice>& CurrentNotice() const { return _notice; }
    // Settings.json could not be read: shown over Projects with "Start with defaults"; nothing is saved until then.
    [[nodiscard]] const std::string& SettingsError() const { return _settingsError; }

    void SelectTab(LauncherTab tab);
    // NewProject, Settings or SystemCheck (EngineUpdate and Job open through their actions).
    void OpenPanel(LauncherPanel panel);
    // Closes the panel; a running job's panel stays (CancelJob first).
    void ClosePanel();
    void DismissNotice();
    void StartWithDefaultSettings();

    // --- Projects tab ---
    [[nodiscard]] const std::vector<ProjectRow>& Rows() const { return _rows; }
    // Row indices to draw, filtered and sorted.
    [[nodiscard]] const std::vector<std::size_t>& VisibleRows() const { return _visible; }
    [[nodiscard]] std::optional<std::size_t> Selected() const { return _selected; }
    [[nodiscard]] ProjectActions ActionsFor(std::size_t row) const;
    // Engine file check / git status of a row; null until the background check has reached it.
    [[nodiscard]] const ProjectDetails* DetailsFor(std::size_t row) const;
    [[nodiscard]] bool DetailsRunning() const { return _details != nullptr; }
    // The filter text, edited by the UI (the list follows next frame).
    [[nodiscard]] std::string& Filter() { return _filter; }
    [[nodiscard]] ProjectSort Sort() const { return _settings.projectSort; }

    void Select(std::size_t row);
    void SetSort(ProjectSort sort);
    void Create();               // Opens the New project panel
    void Import();               // Folder dialog → AddProject
    void BrowseProjectsFolder(); // Folder dialog → saves the default projects folder and scans it
    void SetProjectsFolder(std::filesystem::path folder); // Applies a folder choice and starts discovery
    void Open(std::size_t row); // Job: check, build editor, start it; then the launcher closes
    void Run(std::size_t row);  // Job: the engine's run (until the game closes)
    void ShowInFolder(std::size_t row);
    void AskRemove(std::size_t row); // → Confirmation::RemoveProject
    void RemoveMissing();
    void ChangeEngine(std::size_t row, EngineChange change); // → EngineUpdate panel, plan in the background
    void UpdateEngineFromRemote(std::size_t row);            // Developer: job
    void AskPublishEngine(std::size_t row);                  // Developer: → Confirmation::PublishEngine

    // --- Engines tab ---
    [[nodiscard]] const EngineLibrary& Engines() const { return _engines; }
    [[nodiscard]] int ProjectsUsing(const EngineVersion& version) const;
    // Reading the list of published versions; why it could not be read (empty otherwise).
    [[nodiscard]] bool EngineCatalogRunning() const { return _catalogJob != nullptr; }
    [[nodiscard]] const std::string& EngineCatalogError() const { return _catalogError; }
    void RetryEngineCatalog();
    void DownloadEngineVersion(const EngineVersion& version); // Job
    // The version the tab's details show: the chosen one while it is still known, else the newest.
    [[nodiscard]] std::optional<EngineVersion> ShownEngine() const;
    void SelectEngine(const EngineVersion& version);
    // Bytes on disk; measured in the background while the tab is open, nullopt until then.
    [[nodiscard]] std::optional<std::uintmax_t> EngineSize(const EngineVersion& version) const;
    // Why this version cannot be removed, empty when it can.
    [[nodiscard]] std::string EngineRemovalProblem(const EngineVersion& version) const;
    void AskRemoveEngine(const EngineVersion& version); // → Confirmation::RemoveEngine, then a job
    void ShowEngineFolder(const EngineVersion& version);
    // Developer mode and the newest engine names its repository: "dev (main)" is offered.
    [[nodiscard]] bool DevelopmentEngineAvailable() const;

    // --- New project panel ---
    [[nodiscard]] NewProjectForm& NewProject() { return _newProject; }
    [[nodiscard]] std::vector<EngineChoice> NewProjectEngines() const;
    // Live validation of the form: why it cannot be created (empty when it can), and the folder it would create.
    [[nodiscard]] std::string NewProjectProblem() const;
    [[nodiscard]] std::filesystem::path NewProjectFolder() const;
    void BrowseNewProjectLocation();
    void CreateProject(); // Job: copy / submodule + the engine's new; then the project is listed and selected

    // --- Engine update panel ---
    [[nodiscard]] EngineChange PendingChange() const { return _change; }
    [[nodiscard]] const ProjectRow* ChangeProject() const;
    [[nodiscard]] bool PlanRunning() const { return _planJob != nullptr; }
    [[nodiscard]] std::string PlanStep() const; // What the running plan is doing
    [[nodiscard]] const EngineUpdatePlan* Plan() const { return _plan ? &*_plan : nullptr; }
    [[nodiscard]] const std::string& PlanError() const { return _planError; }
    // The versions the panel offers (Update and Replace), and the one chosen. Choosing plans again, downloading if
    // needed.
    [[nodiscard]] std::vector<EngineVersion> ChangeVersions() const;
    [[nodiscard]] const EngineVersion& ChangeVersion() const { return _changeVersion; }
    [[nodiscard]] bool VersionDownloaded(const EngineVersion& version) const {
        return _engines.Find(version) != nullptr;
    }
    void ChooseChangeVersion(const EngineVersion& version);
    // Update / Replace: job, the project is tried in a temporary copy and moves only if it passes. Repair: moves at
    // once.
    void ConfirmEngineChange();
    // The last trial, shown until the panel closes; why moving the project failed afterwards (close the editor).
    [[nodiscard]] const std::optional<EngineTrial>& Trial() const { return _trial; }
    [[nodiscard]] const std::string& SwitchError() const { return _switchError; }
    [[nodiscard]] std::string TrialReport() const; // For the clipboard
    void SwitchEngine();                           // Job: ApplyEngineUpdate, whatever the trial said

    // --- System check panel ---
    [[nodiscard]] SystemState System() const;
    [[nodiscard]] const std::optional<SystemReport>& Report() const { return _report; }
    [[nodiscard]] bool SystemCheckRunning() const { return _systemJob != nullptr; }
    void RunSystemCheck();
    void InstallMsvc(); // Job (winget); the check runs again afterwards

    // --- Settings panel ---
    // nullopt while it cannot be read.
    [[nodiscard]] std::optional<bool> OnPath() const { return _onPath; }
    [[nodiscard]] std::filesystem::path ProjectsFolder() const;
    void SetOnPath(bool onPath);
    void OpenInstallFolder();
    // The package's ThirdPartyNotices.txt; development builds have none.
    [[nodiscard]] bool LicenseNoticesAvailable() const;
    void OpenLicenseNotices();
    // Audio tools (design D7): NumPy / SciPy for the engine's sound generator, downloaded on request.
    [[nodiscard]] AudioToolsState AudioTools() const { return _audioTools; }
    [[nodiscard]] const std::vector<AudioPackage>& AudioPackages() const;
    void InstallAudioTools(); // Job; also reinstalls outdated ones
    void RemoveAudioTools();
    void AskDeveloperMode(); // → Confirmation::DeveloperMode (warning, no password: design D19)
    void LeaveDeveloperMode();

    // --- Launcher update (top bar button, Update panel, Settings) ---
    [[nodiscard]] bool UpdatesSupported() const { return _updates.Supported(); }
    [[nodiscard]] UpdatePhase UpdateState() const { return _updates.Phase(); }
    [[nodiscard]] const std::optional<AvailableUpdate>& Update() const { return _updates.Update(); }
    [[nodiscard]] const std::vector<UpdateNote>& UpdateNotes() const { return _updateNotes; }
    [[nodiscard]] int UpdateProgress() const { return _updates.Progress(); }
    [[nodiscard]] const std::string& UpdateError() const { return _updates.Error(); }
    // Whether the top bar offers the update: known, and not the version the user skipped.
    [[nodiscard]] bool UpdateOffered() const;
    [[nodiscard]] bool ChecksForUpdates() const { return _settings.checkForUpdates; }
    void SetChecksForUpdates(bool enabled);
    void CheckForUpdates(); // Asked by the user: opens the panel and offers even a skipped version
    void OpenUpdatePanel();
    void DownloadUpdate();
    void CancelUpdateDownload();
    void SkipUpdate();
    void RestartToUpdate(); // Waits for running commands, then the launcher closes and comes back updated
    void CancelUpdateRestart();

    // --- Confirmations ---
    [[nodiscard]] Confirmation Pending() const { return _confirmation; }
    [[nodiscard]] const ProjectRow* PendingProject() const;
    [[nodiscard]] const EngineVersion& PendingEngine() const { return _confirmationEngine; }
    [[nodiscard]] ConfirmationForm& ConfirmationInput() { return _confirmationForm; }
    void Confirm();
    void CancelConfirmation();

    // --- Job panel (the one foreground job) ---
    [[nodiscard]] const BackgroundJob* CurrentJob() const { return _job ? _job->job.get() : nullptr; }
    // Title the failure is shown under ("Could not open MyGame").
    [[nodiscard]] const std::string& JobFailureTitle() const;
    // Every output line of the job so far (the newest BackgroundJob::MaxLines).
    [[nodiscard]] const std::vector<std::string>& JobOutput() const { return _jobOutput; }
    void CancelJob();
    void DismissJob(); // After a failure or cancel: back to the tab

private:
    // A job shown on the Job panel, with what happens on the UI thread when it succeeds.
    struct ForegroundJob {
        std::unique_ptr<BackgroundJob> job;
        std::string failureTitle;
        std::function<void()> onSuccess;
        std::unique_ptr<CommandActivity> activity; // Another launcher window's update waits for this job too
    };

    void Queue(std::function<void()> action);
    void StartJob(std::unique_ptr<BackgroundJob> job, std::string failureTitle, std::function<void()> onSuccess);
    void FinishJobs();
    void ReloadRows();
    void KeepSelectionVisible();
    void StartDetails();
    void StartProjectScan(const std::filesystem::path& folder);
    void StartSystemCheck();
    void StartEngineCatalog();
    void MeasureEngines();
    void StartPlan(const EngineVersion& version);
    void StartTrial();
    void RescanEngines();
    void Save();
    void ShowNotice(NoticeLevel level, std::string title, std::string message);
    [[nodiscard]] const ProjectRow* RowAt(std::size_t row) const;
    [[nodiscard]] const ProjectRow* RowFor(const std::filesystem::path& root) const;
    [[nodiscard]] std::optional<Toolchain> RequireToolchain(const std::string& failureTitle);
    void ReadPathState();

    const LauncherLayout _layout;
    LauncherSettings _settings;
    std::string _settingsError;
    EngineLibrary _engines;
    std::optional<Toolchain> _toolchain;
    std::string _toolchainError;

    LauncherTab _tab = LauncherTab::Projects;
    LauncherPanel _panel = LauncherPanel::None;
    std::optional<Notice> _notice;
    bool _quit = false;

    std::vector<ProjectRow> _rows;
    std::vector<std::size_t> _visible;
    std::string _filter;
    std::string _visibleFilter; // Filter the visible list was built with
    std::optional<std::size_t> _selected;
    std::map<std::filesystem::path, ProjectDetails> _detailsByRoot;

    NewProjectForm _newProject;
    EngineChange _change = EngineChange::Update;
    std::filesystem::path _changeRoot;
    EngineVersion _changeVersion;
    std::optional<EngineTrial> _trial;
    std::string _switchError;
    EngineTrialRunner _trials;
    std::optional<EngineVersion> _shownEngine;
    std::map<EngineVersion, std::uintmax_t> _engineSizes;
    EngineVersion _measuringEngine;
    EngineVersion _confirmationEngine;
    bool _catalogRead = false;
    bool _removeLeftoverTrial = false; // Once, at the first catalog read: a closed launcher can leave a trial folder
    std::string _catalogError;
    std::optional<EngineUpdatePlan> _plan;
    std::string _planError;
    std::optional<SystemReport> _report;
    std::optional<bool> _onPath;
    AudioToolsState _audioTools = AudioToolsState::NotOffered;
    Confirmation _confirmation = Confirmation::None;
    std::filesystem::path _confirmationRoot;
    ConfirmationForm _confirmationForm;

    std::vector<std::function<void()>> _requests;
    std::vector<std::string> _jobOutput;
    std::size_t _jobLine = 0;
    std::vector<UpdateNote> _updateNotes;
    std::string _updateNotesVersion; // The version _updateNotes were parsed for
    // Jobs last: destroyed (canceled and joined) before the state they report into.
    std::unique_ptr<Job<std::vector<ProjectDetails>>> _details;
    std::unique_ptr<Job<SystemReport>> _systemJob;
    std::unique_ptr<Job<EngineUpdatePlan>> _planJob;
    std::unique_ptr<Job<std::vector<OnlineEngine>>> _catalogJob;
    std::unique_ptr<Job<std::uintmax_t>> _engineSizeJob;
    std::optional<ForegroundJob> _job;
    UpdateController _updates;

public:
    LauncherModel(const LauncherModel&) = delete;
    LauncherModel& operator=(const LauncherModel&) = delete;
};
