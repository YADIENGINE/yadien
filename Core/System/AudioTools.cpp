#include "System/AudioTools.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include "Platform/Crypto.h"
#include "Platform/Download.h"
#include "Platform/FileSystem.h"
#include "Platform/KnownFolders.h"
#include "Platform/Text.h"

namespace {
constexpr const char* RecordFile = "AudioTools.json";
constexpr const char* SiteFolder = "site";
constexpr int RecordFormat = 1;
// sys.argv for `python -c` is ['-c', SITE, SCRIPT, ARGS...]: the script then sees [SCRIPT, ARGS...] as `python SCRIPT`.
// One line: a command-line argument, not a file.
constexpr const char* Bootstrap = "import os, runpy, sys; site, script = sys.argv[1], sys.argv[2]; "
                                  "sys.argv = sys.argv[2:]; "
                                  "sys.path[0:0] = [os.path.dirname(os.path.abspath(script)), site]; "
                                  "runpy.run_path(script, run_name='__main__')";

std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::filesystem::path StagingFolder(const std::filesystem::path& folder) {
    std::filesystem::path staging = folder;
    staging += L".installing";
    return staging;
}

Status InstallInto(const std::filesystem::path& staging, const std::vector<AudioPackage>& packages,
                   const OutputLine& onLine, const std::atomic<bool>* cancel) {
    const std::filesystem::path downloads = staging / "downloads";
    nlohmann::json versions = nlohmann::json::object();
    for (const AudioPackage& package : packages) {
        const std::filesystem::path wheel = downloads / Utf8ToPath(package.name + ".whl");
        onLine("Downloading " + package.name + " " + package.version);
        if (Status downloaded = DownloadFile(package.url, wheel, cancel); !downloaded.Ok()) return downloaded;
        Result<std::string> hash = Sha256FileHex(wheel);
        if (!hash.Ok()) return Status::Failure(hash.Error());
        if (hash.Value() != Lower(package.sha256))
            return Status::Failure(package.name + " " + package.version +
                                   ": the download does not match the expected SHA-256; nothing was installed");
        onLine("Extracting " + package.name);
        std::error_code error;
        std::filesystem::create_directories(staging / SiteFolder, error);
        const ProcessCommand tar{SystemFolder() / "tar.exe",
                                 {"-xf", PathToUtf8(wheel), "-C", PathToUtf8(staging / SiteFolder)}};
        const ProcessResult extracted = RunCaptured(tar, onLine, cancel);
        if (extracted.canceled) return Status::Failure("The audio tools installation was canceled");
        if (!extracted.Succeeded())
            return Status::Failure("Cannot extract " + package.name +
                                   (extracted.Started() ? "" : ": " + extracted.startError));
        versions[package.name] = package.version;
    }
    if (Status removed = RemoveTree(downloads); !removed.Ok()) return removed;
    return WriteJsonFileAtomic(staging / RecordFile, {{"format", RecordFormat}, {"packages", versions}});
}
} // namespace

AudioToolsState InspectAudioTools(const std::filesystem::path& folder, const std::vector<AudioPackage>& packages) {
    if (packages.empty()) return AudioToolsState::NotOffered;
    std::error_code error;
    if (!std::filesystem::is_regular_file(folder / RecordFile, error)) return AudioToolsState::NotInstalled;
    Result<nlohmann::json> record = ReadJsonFile(folder / RecordFile);
    if (!record.Ok() || !record.Value().is_object()) return AudioToolsState::Outdated;
    const nlohmann::json installed = record.Value().value("packages", nlohmann::json::object());
    if (!installed.is_object() || installed.size() != packages.size()) return AudioToolsState::Outdated;
    for (const AudioPackage& package : packages)
        if (!installed.contains(package.name) || installed[package.name] != package.version)
            return AudioToolsState::Outdated;
    return AudioToolsState::Installed;
}

Status InstallAudioTools(const std::filesystem::path& folder, const std::vector<AudioPackage>& packages,
                         const OutputLine& onLine, const std::atomic<bool>* cancel) {
    if (packages.empty()) return Status::Failure("This launcher offers no audio tools");
    const std::filesystem::path staging = StagingFolder(folder);
    if (Status cleared = RemoveTree(staging); !cleared.Ok()) return cleared;
    Status installed = InstallInto(staging, packages, onLine, cancel);
    if (installed.Ok()) installed = RemoveTree(folder);
    if (installed.Ok()) installed = MovePath(staging, folder);
    if (!installed.Ok()) (void)RemoveTree(staging);
    return installed;
}

Status RemoveAudioTools(const std::filesystem::path& folder) { return RemoveTree(folder); }

ProcessCommand PythonScriptCommand(const Toolchain& toolchain, const std::filesystem::path& folder,
                                   const std::vector<std::string>& arguments) {
    ProcessCommand command{toolchain.python.executable,
                           {"-X", "utf8", "-c", Bootstrap, PathToUtf8(folder / SiteFolder)}};
    command.arguments.insert(command.arguments.end(), arguments.begin(), arguments.end());
    command.pathPrefix = toolchain.PathFolders();
    return command;
}
