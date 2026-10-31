#pragma once

#include "Settings/LauncherLayout.h"

// Runs the launcher window until it closes. Kept apart from LauncherApplication so Main.cpp does not mix
// <windows.h> with raylib (both declare CloseWindow, ShowCursor ...).
int RunLauncherWindow(LauncherLayout layout);
