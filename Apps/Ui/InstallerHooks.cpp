#include "InstallerHooks.h"
#include <Velopack.h>
#include "Platform/FileSystem.h"
#include "Settings/LauncherLayout.h"
#include "System/UserPath.h"

namespace {
// Hooks have no window and a 30 s limit. A PATH failure is not fatal: Settings → "Add launcher to PATH" fixes it.
void AfterInstall(void*, const char*) { (void)AddToUserPath(LauncherFolder()); }
// Velopack removes its own folder; the data folder beside it (settings, audio tools) is the launcher's to remove.
// Projects are never touched (design D15).
void BeforeUninstall(void*, const char*) {
    (void)RemoveFromUserPath(LauncherFolder());
    if (Result<LauncherLayout> layout = ResolveLauncherLayout(LauncherFolder());
        layout.Ok() && layout.Value().velopackInstall)
        (void)RemoveTree(layout.Value().dataFolder);
}
} // namespace

void RunInstallerHooks() {
    vpkc_app_set_hook_after_install(&AfterInstall);
    vpkc_app_set_hook_before_uninstall(&BeforeUninstall);
    // Updates are applied by the launcher only while nothing runs (design D15), never at an arbitrary start.
    vpkc_app_set_auto_apply_on_startup(false);
    vpkc_app_run(nullptr);
}
