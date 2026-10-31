#pragma once

// The installer's entry into <engine>.exe (Velopack, design D15). Velopack starts the main executable with its own
// arguments after installing (adds PATH) and before uninstalling (removes PATH and the data folder: settings, audio
// tools); this runs the matching hook and ends the process. Without those
// arguments (a normal start, or a development build that was never installed) it returns and the launcher opens.
// Must run first, before any window or setting is touched.
void RunInstallerHooks();
