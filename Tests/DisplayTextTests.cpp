#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include "System/DisplayText.h"

namespace {
std::chrono::system_clock::time_point At(int year, unsigned month, unsigned day, int hour, int minute) {
    return std::chrono::sys_days(std::chrono::year(year) / std::chrono::month(month) / std::chrono::day(day)) +
           std::chrono::hours(hour) + std::chrono::minutes(minute);
}
} // namespace

TEST_CASE("RelativeTimeText reads as a person would say it", "[display]") {
    const char* opened = "2026-10-31T18:20:00Z";
    CHECK(RelativeTimeText(opened, At(2026, 10, 31, 18, 20)) == "Just now");
    CHECK(RelativeTimeText(opened, At(2026, 10, 31, 18, 21)) == "1 minute ago");
    CHECK(RelativeTimeText(opened, At(2026, 10, 31, 18, 55)) == "35 minutes ago");
    CHECK(RelativeTimeText(opened, At(2026, 10, 31, 20, 20)) == "2 hours ago");
    CHECK(RelativeTimeText(opened, At(2026, 11, 1, 19, 0)) == "Yesterday");
    CHECK(RelativeTimeText(opened, At(2026, 11, 3, 18, 20)) == "3 days ago");
    CHECK(RelativeTimeText(opened, At(2026, 11, 14, 18, 20)) == "2 weeks ago");
    CHECK(RelativeTimeText(opened, At(2027, 1, 15, 18, 20)) == "2 months ago");
    CHECK(RelativeTimeText(opened, At(2028, 12, 1, 18, 20)) == "2 years ago");
}

TEST_CASE("RelativeTimeText handles the future and unreadable input", "[display]") {
    CHECK(RelativeTimeText("2026-10-31T18:20:00Z", At(2026, 10, 30, 0, 0)) == "Just now");
    CHECK(RelativeTimeText("", At(2026, 10, 30, 0, 0)).empty());
    CHECK(RelativeTimeText("yesterday", At(2026, 10, 30, 0, 0)).empty());
    CHECK(RelativeTimeText("2026-13-41T18:20:00Z", At(2026, 10, 30, 0, 0)).empty());
}

TEST_CASE("ShortPathText shortens only paths inside the home folder", "[display]") {
    const std::filesystem::path home = L"C:\\Users\\Someone";
    CHECK(ShortPathText(L"C:\\Users\\Someone\\Desktop\\MyGame", home) == "~\\Desktop\\MyGame");
    CHECK(ShortPathText(L"c:\\users\\someone\\MyGame", home) == "~\\MyGame");
    CHECK(ShortPathText(L"C:\\Users\\Someone", home) == "~");
    CHECK(ShortPathText(L"C:\\Users\\SomeoneElse\\MyGame", home) == "C:\\Users\\SomeoneElse\\MyGame");
    CHECK(ShortPathText(L"D:\\Jams\\MyGame", home) == "D:\\Jams\\MyGame");
    CHECK(ShortPathText(L"D:\\Jams\\MyGame", {}) == "D:\\Jams\\MyGame");
}
