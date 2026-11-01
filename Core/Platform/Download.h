#pragma once

#include "Platform/Result.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

// Downloads `url` (https, or file for tests) to `file` through Windows' URL moniker (urlmon): redirects, proxies and
// TLS are the system's. Writes `<file>.part` first and renames it when complete, so `file` is never half written.
// Setting `cancel` stops the transfer; the result is then a failure that says so. Always reads the server, never the
// system's download cache. `progress` gets the bytes so far and the total (0 when the server does not say).
using DownloadProgress = std::function<void(std::uint64_t done, std::uint64_t total)>;
[[nodiscard]] Status DownloadFile(const std::string& url, const std::filesystem::path& file,
                                  const std::atomic<bool>* cancel = nullptr, const DownloadProgress& progress = {});
