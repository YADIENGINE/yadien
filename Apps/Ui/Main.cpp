#include <windows.h>
#include <exception>
#include <string>
#include <utility>
#include "LauncherEntryPoint.h"
#include "Platform/KnownFolders.h"
#include "Platform/Text.h"
#include "Settings/LauncherLayout.h"

// <engine>.exe (WIN32): the launcher window. The console twin <engine>.com is Apps/Cli.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring caption = Utf8ToWide(LAUNCHER_PRODUCT_NAME);
    try {
        Result<LauncherLayout> layout = ResolveLauncherLayout(ExecutablePath().parent_path());
        if (!layout.Ok()) {
            MessageBoxW(nullptr, Utf8ToWide(layout.Error()).c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
            return 1;
        }
        return RunLauncherWindow(std::move(layout).Value());
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, Utf8ToWide(error.what()).c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }
}
