#include "Platform/Text.h"
#include <windows.h>
#include <algorithm>

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), size, nullptr, nullptr);
    return narrow;
}

std::string PathToUtf8(const std::filesystem::path& path) { return WideToUtf8(path.native()); }

std::filesystem::path Utf8ToPath(std::string_view text) { return std::filesystem::path(Utf8ToWide(text)); }

std::string RelativeKey(const std::filesystem::path& path, const std::filesystem::path& base) {
    return WideToUtf8(path.lexically_relative(base).generic_wstring());
}

std::string ToLowerAscii(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
    return lower;
}

std::string LastWindowsError() {
    wchar_t* buffer = nullptr;
    const DWORD length =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, GetLastError(), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = length > 0 ? std::wstring(buffer, length) : L"unknown error";
    LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L'.'))
        text.pop_back();
    return WideToUtf8(text);
}
