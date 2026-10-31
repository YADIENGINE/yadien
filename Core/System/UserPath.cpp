#include "System/UserPath.h"
#include <windows.h>
#include <algorithm>
#include <memory>
#include <type_traits>
#include <vector>
#include "Platform/KnownFolders.h"
#include "Platform/Text.h"

namespace {
constexpr const wchar_t* EnvironmentKey = L"Environment";
constexpr const wchar_t* PathValue = L"Path";
constexpr UINT BroadcastTimeoutMs = 2000;

using RegistryKey = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)>;

std::vector<std::string> SplitList(std::string_view list) {
    std::vector<std::string> entries;
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t end = std::min(list.find(';', start), list.size());
        entries.emplace_back(list.substr(start, end - start));
        start = end + 1;
    }
    return entries;
}

std::string JoinList(const std::vector<std::string>& entries) {
    std::string list;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i) list += ';';
        list += entries[i];
    }
    return list;
}

std::string Normalized(std::string_view entry) {
    std::string text(entry);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
    while (!text.empty() && (text.back() == '\\' || text.back() == '/'))
        text.pop_back();
    for (char& character : text)
        if (character == '/') character = '\\';
    return ToLowerAscii(text);
}

bool Names(std::string_view entry, const std::filesystem::path& folder) {
    return !entry.empty() && Normalized(entry) == Normalized(PathToUtf8(folder.lexically_normal()));
}

// HKCU\Environment\Path as written (REG_EXPAND_SZ keeps %VARIABLES% unexpanded); empty when it does not exist.
Result<std::wstring> ReadUserPath(HKEY key, DWORD& type) {
    DWORD size = 0;
    LSTATUS status = RegQueryValueExW(key, PathValue, nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND) {
        type = REG_EXPAND_SZ;
        return std::wstring();
    }
    if (status != ERROR_SUCCESS) return Result<std::wstring>::Failure("Cannot read the user PATH");
    std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(key, PathValue, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &size);
    if (status != ERROR_SUCCESS) return Result<std::wstring>::Failure("Cannot read the user PATH");
    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}

Status ChangeUserPath(const std::filesystem::path& folder, bool add) {
    HKEY opened = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, EnvironmentKey, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr,
                        &opened, nullptr) != ERROR_SUCCESS)
        return Status::Failure("Cannot open the user environment in the registry");
    const RegistryKey key(opened, &RegCloseKey);
    DWORD type = REG_EXPAND_SZ;
    Result<std::wstring> current = ReadUserPath(key.get(), type);
    if (!current.Ok()) return Status::Failure(current.Error());
    const std::string list = WideToUtf8(current.Value());
    const std::string changed = add ? PathListWith(list, folder) : PathListWithout(list, folder);
    if (changed == list) return Status::Success();
    const std::wstring value = Utf8ToWide(changed);
    const LSTATUS status = RegSetValueExW(key.get(), PathValue, 0, type == REG_SZ ? REG_SZ : REG_EXPAND_SZ,
                                          reinterpret_cast<const BYTE*>(value.c_str()),
                                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) return Status::Failure("Cannot write the user PATH");
    // Running programs (Explorer) reload the environment, so terminals opened from now on see the change.
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(EnvironmentKey), SMTO_ABORTIFHUNG,
                        BroadcastTimeoutMs, nullptr);
    return Status::Success();
}
} // namespace

std::filesystem::path LauncherFolder() { return ExecutablePath().parent_path(); }

Result<bool> IsOnUserPath(const std::filesystem::path& folder) {
    HKEY opened = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, EnvironmentKey, 0, KEY_QUERY_VALUE, &opened) != ERROR_SUCCESS) return false;
    const RegistryKey key(opened, &RegCloseKey);
    DWORD type = REG_EXPAND_SZ;
    Result<std::wstring> current = ReadUserPath(key.get(), type);
    if (!current.Ok()) return Result<bool>::Failure(current.Error());
    return PathListContains(WideToUtf8(current.Value()), folder);
}

Status AddToUserPath(const std::filesystem::path& folder) { return ChangeUserPath(folder, true); }

Status RemoveFromUserPath(const std::filesystem::path& folder) { return ChangeUserPath(folder, false); }

bool PathListContains(std::string_view list, const std::filesystem::path& folder) {
    for (const std::string& entry : SplitList(list))
        if (Names(entry, folder)) return true;
    return false;
}

std::string PathListWith(std::string_view list, const std::filesystem::path& folder) {
    if (PathListContains(list, folder)) return std::string(list);
    std::string result(list);
    if (!result.empty() && result.back() != ';') result += ';';
    return result + PathToUtf8(folder.lexically_normal());
}

std::string PathListWithout(std::string_view list, const std::filesystem::path& folder) {
    std::vector<std::string> kept;
    for (std::string& entry : SplitList(list))
        if (!Names(entry, folder)) kept.push_back(std::move(entry));
    return JoinList(kept);
}
