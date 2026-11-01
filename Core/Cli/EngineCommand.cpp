#include "Cli/EngineCommand.h"
#include <algorithm>
#include <array>
#include "Platform/KnownFolders.h"
#include "Platform/Text.h"

namespace {
constexpr std::array SupportedContracts = {1, 2};
} // namespace

bool SupportsToolsContract(int contract) {
    return std::ranges::find(SupportedContracts, contract) != SupportedContracts.end();
}

std::filesystem::path WindowedLauncherExecutable() {
    const std::filesystem::path executable =
        ExecutablePath().parent_path() / Utf8ToPath(std::string(LAUNCHER_EXECUTABLE_NAME) + ".exe");
    std::error_code error;
    return std::filesystem::is_regular_file(executable, error) ? executable : std::filesystem::path{};
}

Result<ProcessCommand> EngineCommand(const Toolchain& toolchain, const ProjectInfo& project,
                                     const std::vector<std::string>& arguments) {
    if (!project.manifest)
        return Result<ProcessCommand>::Failure(PathToUtf8(project.root) + " has no usable engine: " + project.problem);
    if (!SupportsToolsContract(project.manifest->toolsContract))
        return Result<ProcessCommand>::Failure("The project's engine " + project.manifest->version.ToString() +
                                               " uses tools contract " +
                                               std::to_string(project.manifest->toolsContract) +
                                               ", which this launcher does not know; update the launcher");
    ProcessCommand command;
    command.executable = toolchain.python.executable;
    command.arguments = {"-X", "utf8", PathToUtf8(project.EngineFolder()), "--project", PathToUtf8(project.root)};
    command.arguments.insert(command.arguments.end(), arguments.begin(), arguments.end());
    command.workingDirectory = project.root;
    command.pathPrefix = toolchain.PathFolders();
    if (const std::filesystem::path launcher = WindowedLauncherExecutable(); !launcher.empty())
        command.environment.emplace_back(LauncherVariable, PathToUtf8(launcher));
    return command;
}

EngineRunner LauncherEngineRunner(Toolchain toolchain, const std::atomic<bool>* cancel) {
    return [toolchain = std::move(toolchain), cancel](
               const std::filesystem::path& root, const std::vector<std::string>& arguments, const OutputLine& onLine) {
        Result<ProcessCommand> command = EngineCommand(toolchain, InspectProject(root), arguments);
        if (!command.Ok()) return ProcessResult{-1, false, command.Error()};
        return RunCaptured(command.Value(), onLine, cancel);
    };
}
