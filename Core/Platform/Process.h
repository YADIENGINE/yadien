#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// A program to start. Arguments are UTF-8 and quoted for Windows by BuildCommandLine.
struct ProcessCommand {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;        // Empty: the launcher's current folder
    std::vector<std::filesystem::path> pathPrefix; // Put before the inherited PATH, in this order
    // Variables to set; std::nullopt removes the variable from the child's environment.
    std::vector<std::pair<std::string, std::optional<std::string>>> environment;
};

struct ProcessResult {
    int exitCode = -1;
    bool canceled = false;
    std::string startError; // Non-empty when the program could not be started

    [[nodiscard]] bool Started() const { return startError.empty(); }
    [[nodiscard]] bool Succeeded() const { return Started() && !canceled && exitCode == 0; }
};

// One line of the child's output (stdout and stderr merged), without the line break. UTF-8; output that is not
// valid UTF-8 is read in the console's OEM code page (MSVC tools).
using OutputLine = std::function<void(const std::string& line)>;

// Shares the launcher's console and standard handles and waits (CLI: output, colors and Ctrl+C reach the user
// directly). Ctrl+C is left to the child; its exit code is returned (Ctrl+C exit → 130).
[[nodiscard]] ProcessResult RunAttached(const ProcessCommand& command);

// Captures the output line by line and waits. No console window opens. Setting `cancel` ends the child and every
// process it started (job object); the result is then `canceled`.
[[nodiscard]] ProcessResult RunCaptured(const ProcessCommand& command, const OutputLine& onLine,
                                        const std::atomic<bool>* cancel = nullptr);

// Starts and returns at once; the program outlives the launcher (opening the editor). Empty string on success.
[[nodiscard]] std::string StartDetached(const ProcessCommand& command);

// The Windows command line for `command` (executable and arguments, quoted as CommandLineToArgvW reads them).
[[nodiscard]] std::wstring BuildCommandLine(const ProcessCommand& command);

// Exit code a shell reports for Ctrl+C.
inline constexpr int InterruptedExitCode = 130;
