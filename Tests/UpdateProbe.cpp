// Scripts/update_test.py's driver: the launcher's real update backend without its window, staged only into the test's
// package (it must run from an installed current/ folder). Output lines are read by the script.
#include <atomic>
#include <cstring>
#include <iostream>
#include "Update/VelopackUpdates.h"

namespace {
constexpr int ExitFailure = 1;
constexpr int ExitNotInstalled = 3;
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: LauncherUpdateProbe check|update\n";
        return ExitFailure;
    }
    std::unique_ptr<UpdateBackend> backend = CreateVelopackUpdates();
    if (!backend) {
        std::cout << "not-installed\n";
        return ExitNotInstalled;
    }
    const std::atomic<bool> cancel = false;
    const Result<std::optional<AvailableUpdate>> found = backend->Check(&cancel);
    if (!found.Ok()) {
        std::cout << "failed: " << found.Error() << "\n";
        return ExitFailure;
    }
    if (!found.Value()) {
        std::cout << "up-to-date\n";
        return 0;
    }
    const AvailableUpdate& update = *found.Value();
    std::cout << "update " << update.version << " " << update.downloadBytes << " " << update.notes.size() << "\n";
    if (std::strcmp(argv[1], "update") != 0) return 0;

    int reached = 0;
    if (Status downloaded = backend->Download([&reached](int percent) { reached = percent; }, &cancel);
        !downloaded.Ok()) {
        std::cout << "failed: " << downloaded.Error() << "\n";
        return ExitFailure;
    }
    std::cout << "downloaded " << reached << "\n";
    if (Status applied = backend->ApplyAfterExit(false); !applied.Ok()) {
        std::cout << "failed: " << applied.Error() << "\n";
        return ExitFailure;
    }
    std::cout << "applying\n";
    return 0;
}
