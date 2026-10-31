#pragma once

#include <filesystem>

[[nodiscard]] std::filesystem::path ExecutablePath();
[[nodiscard]] std::filesystem::path LocalAppDataFolder();
[[nodiscard]] std::filesystem::path DocumentsFolder();
