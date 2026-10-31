// <engine>.com: the console entry point (design D15). Everything happens in LauncherCore (Cli/CommandLine).
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "Cli/CommandLine.h"
#include "Platform/KnownFolders.h"
#include "Platform/Text.h"
#include "Settings/LauncherLayout.h"

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i)
        arguments.push_back(WideToUtf8(argv[i]));

    Result<LauncherLayout> layout = ResolveLauncherLayout(ExecutablePath().parent_path());
    if (!layout.Ok()) {
        std::cerr << "error: " << layout.Error() << "\n";
        return 2;
    }
    std::error_code error;
    return RunCommandLine(arguments, layout.Value(), std::filesystem::current_path(error));
}
