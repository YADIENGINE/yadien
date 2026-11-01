#pragma once

#include <memory>
#include "Settings/LauncherLayout.h"
#include "Update/UpdateController.h"

// Runs the launcher window until it closes. Kept apart from LauncherApplication so Main.cpp does not mix
// <windows.h> with raylib (both declare CloseWindow, ShowCursor ...). `updates` is null when there is nothing to update
// (a development build); `justUpdated`: an update has just restarted the launcher.
int RunLauncherWindow(LauncherLayout layout, std::unique_ptr<UpdateBackend> updates, bool justUpdated);
