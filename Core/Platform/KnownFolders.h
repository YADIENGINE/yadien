#pragma once

#include <filesystem>

[[nodiscard]] std::filesystem::path ExecutablePath();
[[nodiscard]] std::filesystem::path LocalAppDataFolder();
[[nodiscard]] std::filesystem::path DocumentsFolder();
// Windows' System32 (its own tools, e.g. tar.exe), whatever drive Windows is on.
[[nodiscard]] std::filesystem::path SystemFolder();
