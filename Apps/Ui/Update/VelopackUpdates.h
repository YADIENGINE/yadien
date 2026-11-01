#pragma once

#include <memory>
#include "Update/UpdateController.h"

// The launcher's updates through Velopack (design D15), reading the repository's releases with ReleaseSource.
// Null when this executable was not installed by Velopack: a development build has nothing to update.
// LAUNCHER_UPDATE_API replaces the releases API URL: Scripts/update_test.py points it at a local file.
[[nodiscard]] std::unique_ptr<UpdateBackend> CreateVelopackUpdates();

// Passed to the launcher when it is restarted after an update.
inline constexpr const char* UpdatedArgument = "--updated";
