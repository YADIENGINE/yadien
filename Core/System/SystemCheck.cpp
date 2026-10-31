#include "System/SystemCheck.h"
#include <windows.h>
#include <algorithm>
#include "Engines/EngineLibrary.h"
#include "Platform/Text.h"
#include "System/Toolchain.h"

namespace {
// The compiler the engine builds with (engine Tools/msvc.py looks for the same component).
constexpr const char* MsvcComponent = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64";
constexpr const char* BuildToolsPackage = "Microsoft.VisualStudio.2022.BuildTools";
constexpr const char* BuildToolsOverride =
    "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended";
constexpr const char* BuildToolsPage =
    "https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022";

std::string Trimmed(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

// First output line of a program, or the reason it did not run.
Result<std::string> FirstLine(const std::filesystem::path& executable, const std::vector<std::string>& arguments) {
    std::string first;
    ProcessCommand command{executable, arguments, {}, {}, {}};
    const ProcessResult result = RunCaptured(command, [&](const std::string& line) {
        if (first.empty()) first = line;
    });
    if (!result.Started()) return Result<std::string>::Failure(result.startError);
    if (result.exitCode != 0)
        return Result<std::string>::Failure(PathToUtf8(executable.filename()) + " exited with code " +
                                            std::to_string(result.exitCode));
    return Trimmed(first);
}

CheckItem CheckTool(const std::string& id, const std::string& title, const Tool& tool) {
    CheckItem item{id, title, CheckStatus::Ok, "", CheckFix::None};
    Result<std::string> version = FirstLine(tool.executable, {"--version"});
    if (!version.Ok()) {
        item.status = CheckStatus::Missing;
        item.detail = version.Error() + "; reinstall the launcher";
    } else if (version.Value().find(tool.version) == std::string::npos) {
        item.status = CheckStatus::Missing;
        item.detail = "Expected " + tool.version + ", found \"" + version.Value() + "\"; reinstall the launcher";
    } else {
        item.detail = tool.version;
    }
    return item;
}

std::filesystem::path VswherePath() {
    std::wstring folder(MAX_PATH, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"ProgramFiles(x86)", folder.data(), MAX_PATH);
    folder.resize(length > 0 && length < MAX_PATH ? length : 0);
    if (folder.empty()) folder = L"C:\\Program Files (x86)";
    return std::filesystem::path(folder) / L"Microsoft Visual Studio" / L"Installer" / L"vswhere.exe";
}

CheckItem CheckMsvc() {
    CheckItem item{"msvc", "Visual Studio C++ build tools", CheckStatus::Missing, "", CheckFix::InstallMsvc};
    const std::filesystem::path vswhere = VswherePath();
    std::error_code error;
    if (!std::filesystem::exists(vswhere, error)) {
        item.detail = "Visual Studio Build Tools are not installed";
        return item;
    }
    Result<std::string> version = FirstLine(vswhere, {"-latest", "-products", "*", "-requires", MsvcComponent,
                                                      "-property", "catalog_productDisplayVersion"});
    if (!version.Ok() || version.Value().empty()) {
        item.detail = "No Visual Studio installation has the C++ x64 build tools";
        return item;
    }
    item.status = CheckStatus::Ok;
    item.fix = CheckFix::None;
    item.detail = "Visual Studio " + version.Value();
    return item;
}

CheckItem CheckEngines(const LauncherLayout& layout) {
    const EngineLibrary library = EngineLibrary::Scan(layout.enginesFolder);
    CheckItem item{"engines", "Engine versions", CheckStatus::Ok, "", CheckFix::None};
    for (const AvailableEngine& engine : library.Engines())
        item.detail += (item.detail.empty() ? "" : ", ") + engine.manifest.version.ToString();
    if (library.Engines().empty()) {
        item.status = CheckStatus::Missing;
        item.detail = "No engine in " + PathToUtf8(layout.enginesFolder) + "; reinstall the launcher";
    } else if (!library.Problems().empty()) {
        item.status = CheckStatus::Warning;
        item.detail += "; skipped: " + library.Problems().front();
    }
    return item;
}

CheckItem CheckGit() {
    CheckItem item{"git", "Git (developer mode)", CheckStatus::Ok, "", CheckFix::None};
    Result<std::string> version = FirstLine("git", {"--version"});
    if (!version.Ok()) {
        item.status = CheckStatus::Missing;
        item.detail = "git is not on PATH; install Git for Windows";
        return item;
    }
    item.detail = version.Value();
    for (const char* key : {"user.name", "user.email"}) {
        Result<std::string> value = FirstLine("git", {"config", "--get", key});
        if (!value.Ok() || value.Value().empty()) {
            item.status = CheckStatus::Warning;
            item.detail += std::string("; set ") + key + " (git config --global " + key + " ...) to commit";
        }
    }
    return item;
}
} // namespace

bool SystemReport::ReadyToBuild() const {
    return std::ranges::none_of(items, [](const CheckItem& item) { return item.status == CheckStatus::Missing; });
}

SystemReport RunSystemCheck(const LauncherLayout& layout, LauncherMode mode) {
    SystemReport report;
    Result<Toolchain> toolchain = LoadToolchain(layout.toolsFolder);
    if (toolchain.Ok()) {
        report.items.push_back(CheckTool("python", "Python", toolchain.Value().python));
        report.items.push_back(CheckTool("cmake", "CMake", toolchain.Value().cmake));
        report.items.push_back(CheckTool("ninja", "Ninja", toolchain.Value().ninja));
    } else {
        report.items.push_back({"tools", "Launcher tools", CheckStatus::Missing,
                                toolchain.Error() + "; reinstall the launcher", CheckFix::None});
    }
    report.items.push_back(CheckMsvc());
    report.items.push_back(CheckEngines(layout));
    if (mode == LauncherMode::Developer) report.items.push_back(CheckGit());
    return report;
}

Status InstallMsvc(const OutputLine& onLine, const std::atomic<bool>* cancel) {
    ProcessCommand command{"winget",
                           {"install", "--id", BuildToolsPackage, "--exact", "--source", "winget",
                            "--accept-package-agreements", "--accept-source-agreements", "--override",
                            BuildToolsOverride},
                           {},
                           {},
                           {}};
    const ProcessResult result = RunCaptured(command, onLine, cancel);
    if (!result.Started())
        return Status::Failure("winget is not available; install the C++ build tools from " +
                               std::string(BuildToolsPage));
    if (result.canceled) return Status::Failure("Canceled");
    if (result.exitCode != 0)
        return Status::Failure("winget exited with code " + std::to_string(result.exitCode) + "; see the output");
    return Status::Success();
}
