#include "System/DisplayText.h"
#include <cstdlib>
#include <cwctype>
#include <optional>
#include "Platform/Text.h"

namespace {
std::optional<int> Digits(std::string_view text) {
    int value = 0;
    if (text.empty()) return std::nullopt;
    for (const char character : text) {
        if (character < '0' || character > '9') return std::nullopt;
        value = value * 10 + (character - '0');
    }
    return value;
}
std::optional<std::chrono::system_clock::time_point> ParseIsoUtc(std::string_view text) {
    // YYYY-MM-DDTHH:MM:SSZ
    constexpr std::size_t Length = 20;
    if (text.size() != Length || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        text[16] != ':' || text[19] != 'Z')
        return std::nullopt;
    const auto year = Digits(text.substr(0, 4));
    const auto month = Digits(text.substr(5, 2));
    const auto day = Digits(text.substr(8, 2));
    const auto hour = Digits(text.substr(11, 2));
    const auto minute = Digits(text.substr(14, 2));
    const auto second = Digits(text.substr(17, 2));
    if (!year || !month || !day || !hour || !minute || !second) return std::nullopt;
    const std::chrono::year_month_day date{std::chrono::year(*year), std::chrono::month(static_cast<unsigned>(*month)),
                                           std::chrono::day(static_cast<unsigned>(*day))};
    if (!date.ok()) return std::nullopt;
    return std::chrono::sys_days(date) + std::chrono::hours(*hour) + std::chrono::minutes(*minute) +
           std::chrono::seconds(*second);
}
std::string Ago(long long count, const char* unit) {
    return std::to_string(count) + " " + unit + (count == 1 ? "" : "s") + " ago";
}
} // namespace

std::string RelativeTimeText(std::string_view isoUtc, std::chrono::system_clock::time_point now) {
    const auto then = ParseIsoUtc(isoUtc);
    if (!then) return {};
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(now - *then).count();
    constexpr long long Hour = 60;
    constexpr long long Day = 24 * Hour;
    constexpr long long Week = 7 * Day;
    constexpr long long Month = 30 * Day;
    constexpr long long Year = 365 * Day;
    if (minutes < 1) return "Just now";
    if (minutes < Hour) return Ago(minutes, "minute");
    if (minutes < Day) return Ago(minutes / Hour, "hour");
    if (minutes < 2 * Day) return "Yesterday";
    if (minutes < Week) return Ago(minutes / Day, "day");
    if (minutes < Month) return Ago(minutes / Week, "week");
    if (minutes < Year) return Ago(minutes / Month, "month");
    return Ago(minutes / Year, "year");
}

std::filesystem::path UserHomeFolder() {
    std::size_t length = 0;
    if (_wgetenv_s(&length, nullptr, 0, L"USERPROFILE") != 0 || length <= 1) return {};
    std::wstring value(length, L'\0');
    if (_wgetenv_s(&length, value.data(), value.size(), L"USERPROFILE") != 0) return {};
    value.resize(length - 1);
    return value;
}

std::string ShortPathText(const std::filesystem::path& path, const std::filesystem::path& home) {
    const std::wstring full = path.native();
    std::wstring base = home.native();
    while (!base.empty() && (base.back() == L'\\' || base.back() == L'/'))
        base.pop_back();
    if (base.empty() || full.size() < base.size()) return PathToUtf8(path);
    for (std::size_t index = 0; index < base.size(); ++index)
        if (std::towlower(full[index]) != std::towlower(base[index])) return PathToUtf8(path);
    if (full.size() > base.size() && full[base.size()] != L'\\' && full[base.size()] != L'/') return PathToUtf8(path);
    return "~" + PathToUtf8(std::filesystem::path(full.substr(base.size())));
}
