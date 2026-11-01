#include <windows.h>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include "InstallerHooks.h"
#include "LauncherEntryPoint.h"
#include "Platform/KnownFolders.h"
#include "Platform/Text.h"
#include "Settings/LauncherLayout.h"
#include "Update/VelopackUpdates.h"

// <engine>.exe (WIN32): the launcher window. The console twin <engine>.com is Apps/Cli.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int) {
    const std::wstring caption = Utf8ToWide(LAUNCHER_PRODUCT_NAME);
    try {
        RunInstallerHooks(); // Ends the process when Velopack started it for a hook
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
