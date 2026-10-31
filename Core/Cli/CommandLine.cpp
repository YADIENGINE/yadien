#include "Cli/CommandLine.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include "Cli/EngineCommand.h"
#include "Engines/EngineCopyRecord.h"
#include "Engines/EngineLibrary.h"
#include "Platform/Text.h"
#include "Projects/CopyIntegrity.h"
#include "Projects/ProjectInfo.h"
#include "Settings/LauncherSettings.h"
#include "Settings/ProjectList.h"
#include "System/AudioTools.h"
#include "System/SystemCheck.h"

namespace {
constexpr int ExitOk = 0;
constexpr int ExitFailed = 1;
constexpr int ExitUsage = 2;
constexpr const char* LauncherWord = "launcher";
constexpr const char* ProjectOption = "--project";
constexpr size_t ListedChanges = 5;

const char* KindName(EngineKind kind) {
    switch (kind) {
    case EngineKind::Copy        : return "copy";
    case EngineKind::Git         : return "git";
    case EngineKind::Unregistered: return "unregistered";
    case EngineKind::Missing     : return "missing";
    }
    return "missing";
}

const char* StatusName(CheckStatus status) {
    switch (status) {
    case CheckStatus::Ok     : return "ok";
    case CheckStatus::Warning: return "warning";
    case CheckStatus::Missing: return "missing";
    }
    return "missing";
}

void PrintUsage() {
    const std::string name = LAUNCHER_EXECUTABLE_NAME;
    std::cout << "usage: " << name << " [--project ROOT] <engine command> [options]\n"
              << "       " << name << " launcher <job> [--json]\n\n"
              << "Engine commands run with the project's own engine tools (" << name
              << " build, run, test, editor, refresh, release, ...);\n"
              << "list them with: " << name << " --project ROOT --help, or from a project folder: " << name
              << " --help\n\n"
              << "Launcher jobs:\n"
              << "  version        launcher and bundled engine versions\n"
              << "  system-check   compiler and tools (exit 1 when building is not possible)\n"
              << "  projects       the launcher's project list\n"
              << "  audio-tools [status|install|remove]\n"
              << "                 NumPy / SciPy for the engine's sound generator (downloads on install)\n"
              << "  python SCRIPT [ARGS]\n"
              << "                 run a Python script with the bundled Python and the audio tools\n";
}

bool WantsJson(const std::vector<std::string>& arguments) {
    return std::ranges::find(arguments, "--json") != arguments.end();
}

int Version(const LauncherLayout& layout) {
    std::cout << LAUNCHER_PRODUCT_NAME << " launcher " << LAUNCHER_VERSION << "\n";
    // A named library: ranging over a temporary's member would read it after the temporary is gone (C++20).
    const EngineLibrary library = EngineLibrary::Scan(layout.enginesFolder);
    for (const AvailableEngine& engine : library.Engines())
        std::cout << "  engine " << engine.manifest.version.ToString() << " (" << engine.record.tag << ")\n";
    for (const std::string& problem : library.Problems())
        std::cerr << "warning: " << problem << "\n";
    return ExitOk;
}

int SystemCheckJob(const LauncherLayout& layout, bool json) {
    Result<LauncherSettings> settings = LoadLauncherSettings(layout.SettingsFile());
    const LauncherMode mode = settings.Ok() ? settings.Value().mode : LauncherMode::User;
    const SystemReport report = RunSystemCheck(layout, mode);
    if (json) {
        nlohmann::json items = nlohmann::json::array();
        for (const CheckItem& item : report.items)
            items.push_back(
                {{"id", item.id}, {"title", item.title}, {"status", StatusName(item.status)}, {"detail", item.detail}});
        std::cout << nlohmann::json{{"ready", report.ReadyToBuild()}, {"items", items}}.dump(2) << "\n";
    } else {
        for (const CheckItem& item : report.items)
            std::cout << "[" << StatusName(item.status) << "] " << item.title << ": " << item.detail << "\n";
    }
    return report.ReadyToBuild() ? ExitOk : ExitFailed;
}

int ProjectsJob(const LauncherLayout& layout, bool json) {
    Result<LauncherSettings> settings = LoadLauncherSettings(layout.SettingsFile());
    if (!settings.Ok()) {
        std::cerr << "error: " << settings.Error() << "\n";
        return ExitUsage;
    }
    nlohmann::json rows = nlohmann::json::array();
    for (const ProjectEntry& entry : RecentProjects(settings.Value())) {
        const ProjectInfo info = InspectProject(entry.root);
        const std::string version = info.manifest ? info.manifest->version.ToString() : "";
        if (json) {
            rows.push_back({{"name", info.name},
                            {"root", PathToUtf8(info.root)},
                            {"engine", version},
                            {"kind", KindName(info.kind)},
                            {"lastOpened", entry.lastOpened},
                            {"problem", info.problem}});
        } else {
            std::cout << info.name << "  " << PathToUtf8(info.root) << "  " << (version.empty() ? "-" : version) << " ("
                      << KindName(info.kind) << ")" << (info.problem.empty() ? "" : "  ! " + info.problem) << "\n";
        }
    }
    if (json) std::cout << rows.dump(2) << "\n";
    return ExitOk;
}

const char* AudioStateName(AudioToolsState state) {
    switch (state) {
    case AudioToolsState::NotOffered  : return "not offered by this launcher";
    case AudioToolsState::NotInstalled: return "not installed";
    case AudioToolsState::Outdated    : return "installed, but not this launcher's versions (reinstall)";
    case AudioToolsState::Installed   : return "installed";
    }
    return "not installed";
}

int AudioToolsJob(const std::vector<std::string>& arguments, const LauncherLayout& layout) {
    const std::string action = arguments.size() > 2 ? arguments[2] : "status";
    Result<Toolchain> toolchain = LoadToolchain(layout.toolsFolder);
    if (!toolchain.Ok()) {
        std::cerr << "error: " << toolchain.Error() << "; reinstall the launcher\n";
        return ExitUsage;
    }
    const std::vector<AudioPackage>& packages = toolchain.Value().audio;
    const std::filesystem::path folder = layout.AudioToolsFolder();
    if (action == "status") {
        std::cout << "audio tools: " << AudioStateName(InspectAudioTools(folder, packages)) << "\n";
        for (const AudioPackage& package : packages)
            std::cout << "  " << package.name << " " << package.version << "\n";
        return ExitOk;
    }
    Status done = Status::Success();
    if (action == "install") {
        done = InstallAudioTools(folder, packages, [](const std::string& line) { std::cout << line << "\n"; });
    } else if (action == "remove") {
        done = RemoveAudioTools(folder);
    } else {
        std::cerr << "error: unknown audio-tools action '" << action << "' (status, install, remove)\n";
        return ExitUsage;
    }
    if (!done.Ok()) {
        std::cerr << "error: " << done.Error() << "\n";
        return ExitFailed;
    }
    std::cout << "audio tools: " << AudioStateName(InspectAudioTools(folder, packages)) << "\n";
    return ExitOk;
}

int PythonJob(const std::vector<std::string>& arguments, const LauncherLayout& layout) {
    if (arguments.size() < 3) {
        std::cerr << "error: launcher python needs a script: " << LAUNCHER_EXECUTABLE_NAME
                  << " launcher python SCRIPT [ARGS]\n";
        return ExitUsage;
    }
    Result<Toolchain> toolchain = LoadToolchain(layout.toolsFolder);
    if (!toolchain.Ok()) {
        std::cerr << "error: " << toolchain.Error() << "; reinstall the launcher\n";
        return ExitUsage;
    }
    const std::vector<std::string> script(arguments.begin() + 2, arguments.end());
    std::cout.flush();
    const ProcessResult result = RunAttached(PythonScriptCommand(toolchain.Value(), layout.AudioToolsFolder(), script));
    if (!result.Started()) {
        std::cerr << "error: " << result.startError << "\n";
        return ExitUsage;
    }
    return result.exitCode;
}

int LauncherJob(const std::vector<std::string>& arguments, const LauncherLayout& layout) {
    const std::string job = arguments.size() > 1 ? arguments[1] : "help";
    const bool json = WantsJson(arguments);
    if (job == "version") return Version(layout);
    if (job == "system-check") return SystemCheckJob(layout, json);
    if (job == "projects") return ProjectsJob(layout, json);
    if (job == "audio-tools") return AudioToolsJob(arguments, layout);
    if (job == "python") return PythonJob(arguments, layout);
    if (job == "help" || job == "--help" || job == "-h") {
        PrintUsage();
        return ExitOk;
    }
    std::cerr << "error: unknown launcher job '" << job << "'\n";
    PrintUsage();
    return ExitUsage;
}

// The read-only rule (design D4): a changed copy still builds, the user is told on every command.
void WarnAboutEditedCopy(const ProjectInfo& project) {
    if (project.kind != EngineKind::Copy) return;
    Result<EngineCopyRecord> record = LoadEngineCopyRecord(project.EngineFolder());
    if (!record.Ok()) return;
    Result<CopyIntegrity> integrity = CheckCopyIntegrity(project.EngineFolder(), record.Value());
    if (!integrity.Ok() || integrity.Value().Clean()) return;
    const CopyIntegrity& found = integrity.Value();
    std::vector<std::string> changes;
    for (const std::string& file : found.modified)
        changes.push_back("changed " + file);
    for (const std::string& file : found.missing)
        changes.push_back("missing " + file);
    for (const std::string& file : found.added)
        changes.push_back("added " + file);
    std::cerr << "warning: Engine/ is a read-only copy of " << record.Value().tag << " but " << changes.size()
              << " file(s) differ; the launcher's engine update restores it\n";
    for (size_t i = 0; i < changes.size() && i < ListedChanges; ++i)
        std::cerr << "  " << changes[i] << "\n";
    if (changes.size() > ListedChanges) std::cerr << "  ...\n";
}

int Forward(const ParsedCommandLine& parsed, const LauncherLayout& layout, const std::filesystem::path& currentFolder) {
    const std::optional<std::filesystem::path> root =
        parsed.project ? std::optional(CanonicalRoot(*parsed.project)) : FindProjectRoot(currentFolder);
    if (!root) {
        std::cerr << "error: no project here (no Project.json in this folder or above); run it in a project folder "
                     "or pass --project ROOT\n";
        return ExitUsage;
    }
    Result<Toolchain> toolchain = LoadToolchain(layout.toolsFolder);
    if (!toolchain.Ok()) {
        std::cerr << "error: " << toolchain.Error() << "; reinstall the launcher\n";
        return ExitUsage;
    }
    const ProjectInfo project = InspectProject(*root);
    Result<ProcessCommand> command = EngineCommand(toolchain.Value(), project, parsed.arguments);
    if (!command.Ok()) {
        std::cerr << "error: " << command.Error() << "\n";
        return ExitUsage;
    }
    WarnAboutEditedCopy(project);
    std::cout.flush();
    const ProcessResult result = RunAttached(command.Value());
    if (!result.Started()) {
        std::cerr << "error: " << result.startError << "\n";
        return ExitUsage;
    }
    return result.exitCode;
}
} // namespace

ParsedCommandLine ParseCommandLine(const std::vector<std::string>& arguments) {
    ParsedCommandLine parsed;
    size_t index = 0;
    const std::string prefix = std::string(ProjectOption) + "=";
    if (!arguments.empty() && arguments[0] == ProjectOption) {
        if (arguments.size() < 2) {
            parsed.error = std::string(ProjectOption) + " needs a folder";
            return parsed;
        }
        parsed.project = Utf8ToPath(arguments[1]);
        index = 2;
    } else if (!arguments.empty() && arguments[0].starts_with(prefix)) {
        parsed.project = Utf8ToPath(arguments[0].substr(prefix.size()));
        index = 1;
    }
    parsed.arguments.assign(arguments.begin() + static_cast<std::ptrdiff_t>(index), arguments.end());
    return parsed;
}

std::optional<std::filesystem::path> FindProjectRoot(const std::filesystem::path& start) {
    std::error_code error;
    for (std::filesystem::path folder = CanonicalRoot(start);; folder = folder.parent_path()) {
        if (std::filesystem::is_regular_file(folder / ProjectFile, error) ||
            std::filesystem::is_regular_file(folder / ProjectInfo::EngineFolderName / EngineManifestFile, error))
            return folder;
        if (folder == folder.parent_path()) return std::nullopt;
    }
}

int RunCommandLine(const std::vector<std::string>& arguments, const LauncherLayout& layout,
                   const std::filesystem::path& currentFolder) {
    const ParsedCommandLine parsed = ParseCommandLine(arguments);
    if (!parsed.error.empty()) {
        std::cerr << "error: " << parsed.error << "\n";
        return ExitUsage;
    }
    if (parsed.arguments.empty()) {
        PrintUsage();
        return ExitUsage;
    }
    if (parsed.arguments.front() == LauncherWord) return LauncherJob(parsed.arguments, layout);
    if (!parsed.project && (parsed.arguments.front() == "--help" || parsed.arguments.front() == "-h") &&
        !FindProjectRoot(currentFolder)) {
        PrintUsage();
        return ExitOk;
    }
    return Forward(parsed, layout, currentFolder);
}
