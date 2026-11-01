#include "System/CommandActivity.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace {
constexpr const wchar_t* NamePrefix = L"Local\\launcher-command-";

std::wstring MarkName(const std::filesystem::path& installFolder) {
    std::wstring name = installFolder.lexically_normal().wstring();
    std::ranges::replace_if(
        name, [](wchar_t letter) { return letter == L'\\' || letter == L'/' || letter == L':'; }, L'_');
    std::ranges::transform(name, name.begin(),
                           [](wchar_t letter) { return static_cast<wchar_t>(std::towlower(letter)); });
    return NamePrefix + name;
}
} // namespace

CommandActivity::CommandActivity(const std::filesystem::path& installFolder)
    : _handle(CreateMutexW(nullptr, FALSE, MarkName(installFolder).c_str())) {}

CommandActivity::~CommandActivity() {
    if (_handle != nullptr) CloseHandle(_handle);
}

bool CommandsRunning(const std::filesystem::path& installFolder) {
    const HANDLE mark = OpenMutexW(SYNCHRONIZE, FALSE, MarkName(installFolder).c_str());
    if (mark == nullptr) return false;
    CloseHandle(mark);
    return true;
}
