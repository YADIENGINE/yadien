#pragma once

#include "Platform/Process.h"
#include "Platform/Result.h"
#include "Settings/LauncherLayout.h"
#include "Settings/LauncherSettings.h"
#include <atomic>
#include <string>
#include <vector>

// The System check screen's model (design D8, D15). Ok: ready. Warning: works, but something is off (shown, does
// not block). Missing: building is not possible until it is fixed.
enum class CheckStatus { Ok, Warning, Missing };
// What the screen's button for an item does.
enum class CheckFix { None, InstallMsvc };

struct CheckItem {
    std::string id; // Stable key: "python", "cmake", "ninja", "msvc", "engines", "git"
    std::string title;
    CheckStatus status = CheckStatus::Ok;
    std::string detail; // Version found, or what is wrong
    CheckFix fix = CheckFix::None;
};

struct SystemReport {
    std::vector<CheckItem> items;

    [[nodiscard]] bool ReadyToBuild() const; // No item is Missing
};

// Runs the tools (`--version`), vswhere and, in developer mode, git; takes about a second. Run it off the UI thread.
[[nodiscard]] SystemReport RunSystemCheck(const LauncherLayout& layout, LauncherMode mode);

// Installs Visual Studio 2022 Build Tools with the C++ workload through winget (the installer asks for elevation
// itself). Long; output streams through `onLine`; `cancel` may be null.
[[nodiscard]] Status InstallMsvc(const OutputLine& onLine, const std::atomic<bool>* cancel);
