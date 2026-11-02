#include <windows.h>
#include <shellapi.h>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "Cli/CommandLine.h"
#include "InstallerHooks.h"
#include "LauncherEntryPoint.h"
#include "Platform/KnownFolders.h"
#include "Platform/Process.h"
#include "Platform/Text.h"
#include "Settings/LauncherLayout.h"
#include "Update/VelopackUpdates.h"

namespace {
std::vector<std::string> ProgramArguments() {
    int count = 0;
    const std::unique_ptr<LPWSTR, decltype(&LocalFree)> list(CommandLineToArgvW(GetCommandLineW(), &count), &LocalFree);
    std::vector<std::string> arguments;
    for (int i = 1; list && i < count; ++i)
        arguments.push_back(WideToUtf8(list.get()[i]));
    return arguments;
}

// A dialog would stop the script that called this, so a failure only goes to its stderr and exit code.
int RunConsoleProgram(const std::vector<std::string>& arguments) {
    constexpr int SetupFailed = 2;
    const ProcessResult result = RunAttachedFromWindowedProgram(ConsoleProgramCommand(ExecutablePath(), arguments));
    if (result.startError.empty()) return result.exitCode;
    const std::string message = "error: " + result.startError + "\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
    return SetupFailed;
}
} // namespace

// <engine>.exe (WIN32): the launcher window. The console twin <engine>.com is Apps/Cli.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int) {
    const std::wstring caption = Utf8ToWide(LAUNCHER_PRODUCT_NAME);
    try {
        RunInstallerHooks(); // Ends the process when Velopack started it for a hook
        if (const std::vector<std::string> arguments = ProgramArguments();
            WantsConsoleProgram(arguments, UpdatedArgument))
            return RunConsoleProgram(arguments);
        Result<LauncherLayout> layout = ResolveLauncherLayout(ExecutablePath().parent_path());
        if (!layout.Ok()) {
            MessageBoxW(nullptr, Utf8ToWide(layout.Error()).c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
            return 1;
        }
        std::unique_ptr<UpdateBackend> updates;
        if (layout.Value().velopackInstall) updates = CreateVelopackUpdates();
        const bool justUpdated =
            std::wstring_view(commandLine).find(Utf8ToWide(UpdatedArgument)) != std::wstring_view::npos;
        return RunLauncherWindow(std::move(layout).Value(), std::move(updates), justUpdated);
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, Utf8ToWide(error.what()).c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }
}
