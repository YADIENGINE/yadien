#include "Platform/KnownFolders.h"
#include <windows.h>
#include <shlobj.h>
#include <string>

namespace {
std::filesystem::path KnownFolder(const KNOWNFOLDERID& id) {
    wchar_t* path = nullptr;
    std::filesystem::path folder;
    if (SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path) == S_OK) folder = path;
    CoTaskMemFree(path);
    return folder;
}
} // namespace

std::filesystem::path ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length < buffer.size()) return std::filesystem::path(buffer.substr(0, length));
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path LocalAppDataFolder() { return KnownFolder(FOLDERID_LocalAppData); }

std::filesystem::path DocumentsFolder() { return KnownFolder(FOLDERID_Documents); }
