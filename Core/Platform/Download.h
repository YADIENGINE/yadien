#pragma once

#include "Platform/Result.h"
#include <atomic>
#include <filesystem>
#include <string>

// Downloads `url` (https, or file for tests) to `file` through Windows' URL moniker (urlmon): redirects, proxies and
// TLS are the system's. Writes `<file>.part` first and renames it when complete, so `file` is never half written.
// Setting `cancel` stops the transfer; the result is then a failure that says so.
[[nodiscard]] Status DownloadFile(const std::string& url, const std::filesystem::path& file,
                                  const std::atomic<bool>* cancel = nullptr);
