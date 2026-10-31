#include "Platform/Shell.h"
#include <windows.h>
#include <shellapi.h>
#include <cstdint>
#include "Platform/Text.h"

Status OpenFolder(const std::filesystem::path& folder) {
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) return Status::Failure(PathToUtf8(folder) + " does not exist");
    // ShellExecute reports success with a value above 32.
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteW(nullptr, L"explore", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) return Status::Failure("Cannot open " + PathToUtf8(folder) + " in Explorer");
    return Status::Success();
}
