#pragma once

#include <atomic>
#include <filesystem>
#include <vector>

// "Scan": the project folders (with a Project.json) under `folder`, the folder itself included, at most
// MaxScanDepth levels down. A project's own subfolders, hidden folders and build output are not entered.
// Canonical roots (CanonicalRoot), sorted. `cancel` may be null; unreadable folders are skipped.
inline constexpr int MaxScanDepth = 4;
[[nodiscard]] std::vector<std::filesystem::path> FindProjects(const std::filesystem::path& folder,
                                                              const std::atomic<bool>* cancel = nullptr);
