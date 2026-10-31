#include "Developer/DeveloperGit.h"
#include <sstream>
#include "Platform/Text.h"
#include "Projects/ProjectInfo.h"

namespace {
constexpr const char* DefaultBranch = "main";

std::string Joined(const std::vector<std::string>& arguments) {
    std::string text;
    for (const std::string& argument : arguments)
        text += (text.empty() ? "" : " ") + argument;
    return text;
}

// One step of an operation: prints the command, runs it, turns a failure into a Status.
Status Step(const std::filesystem::path& folder, const std::vector<std::string>& arguments, const OutputLine& onLine) {
    if (onLine) onLine("> git " + Joined(arguments));
    const ProcessResult result = RunGit(folder, arguments, onLine);
    if (!result.Started()) return Status::Failure(result.startError + " (is git installed?)");
    if (result.exitCode != 0)
        return Status::Failure("git " + Joined(arguments) + " failed (exit code " + std::to_string(result.exitCode) +
                               ")");
    return Status::Success();
}

// Output of a query that must succeed; lines joined by '\n'.
Result<std::string> Query(const std::filesystem::path& folder, const std::vector<std::string>& arguments) {
    std::string output;
    const ProcessResult result = RunGit(folder, arguments, [&](const std::string& line) { output += line + "\n"; });
    if (!result.Started()) return Result<std::string>::Failure(result.startError + " (is git installed?)");
    if (result.exitCode != 0) return Result<std::string>::Failure("git " + Joined(arguments) + " failed: " + output);
    return output;
}

bool Succeeds(const std::filesystem::path& folder, const std::vector<std::string>& arguments) {
    return RunGit(folder, arguments, nullptr).exitCode == 0;
}

std::string TrackedBranch(const std::filesystem::path& root) {
    Result<std::string> branch = Query(root, {"config", "-f", ".gitmodules", "--get",
                                              std::string("submodule.") + ProjectInfo::EngineFolderName + ".branch"});
    if (!branch.Ok()) return DefaultBranch;
    std::string name = branch.Value();
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
        name.pop_back();
    return name.empty() ? DefaultBranch : name;
}

// Puts a detached Engine back on `branch` without losing commits: allowed when HEAD is on the branch already or
// the branch can fast-forward to HEAD.
Status AttachToBranch(const std::filesystem::path& engine, const std::string& branch, const OutputLine& onLine) {
    if (Succeeds(engine, {"merge-base", "--is-ancestor", "HEAD", branch}))
        return Step(engine, {"switch", branch}, onLine);
    if (Succeeds(engine, {"merge-base", "--is-ancestor", branch, "HEAD"})) {
        Result<std::string> head = Query(engine, {"rev-parse", "HEAD"});
        if (!head.Ok()) return Status::Failure(head.Error());
        std::string commit = head.Value();
        commit.erase(commit.find_last_not_of("\r\n") + 1);
        if (Status switched = Step(engine, {"switch", branch}, onLine); !switched.Ok()) return switched;
        return Step(engine, {"merge", "--ff-only", commit}, onLine);
    }
    return Status::Failure("Engine is detached at commits that are not on " + branch + "; resolve it by hand in " +
                           PathToUtf8(engine));
}

Status CommitPointer(const std::filesystem::path& root, const std::string& message, const OutputLine& onLine) {
    return Step(root, {"commit", "-m", message, "--", ProjectInfo::EngineFolderName}, onLine);
}
} // namespace

ProcessResult RunGit(const std::filesystem::path& folder, const std::vector<std::string>& arguments,
                     const OutputLine& onLine) {
    ProcessCommand command;
    command.executable = "git";
    command.arguments = {"-C", PathToUtf8(folder)};
    command.arguments.insert(command.arguments.end(), arguments.begin(), arguments.end());
    // No prompts on a hidden console; English messages so they read the same everywhere
    command.environment = {{"GIT_TERMINAL_PROMPT", "0"}, {"LC_ALL", "C"}};
    return RunCaptured(command, onLine);
}

Result<EngineGitStatus> ReadEngineGitStatus(const std::filesystem::path& projectRoot) {
    const std::filesystem::path engine = projectRoot / ProjectInfo::EngineFolderName;
    Result<std::string> status = Query(engine, {"status", "--porcelain=v2", "--branch"});
    if (!status.Ok()) return Result<EngineGitStatus>::Failure(status.Error());

    EngineGitStatus result;
    result.trackedBranch = TrackedBranch(projectRoot);
    std::istringstream lines(status.Value());
    for (std::string line; std::getline(lines, line);) {
        if (line.starts_with("# branch.oid "))
            result.head = line.substr(13);
        else if (line.starts_with("# branch.head "))
            result.branch = line.substr(14) == "(detached)" ? "" : line.substr(14);
        else if (line.starts_with("# branch.upstream "))
            result.hasUpstream = true;
        else if (line.starts_with("# branch.ab ")) {
            std::istringstream counts(line.substr(12));
            std::string ahead;
            std::string behind;
            counts >> ahead >> behind;
            result.ahead = std::stoi(ahead.substr(1));
            result.behind = std::stoi(behind.substr(1));
        } else if (!line.empty() && line.front() != '#') {
            ++result.changedFiles;
        }
    }
    result.pointerChanged = !Succeeds(
        projectRoot, {"diff", "--quiet", "--ignore-submodules=dirty", "HEAD", "--", ProjectInfo::EngineFolderName});
    return result;
}

Status UpdateEngineFromRemote(const std::filesystem::path& projectRoot, const OutputLine& onLine) {
    Result<EngineGitStatus> status = ReadEngineGitStatus(projectRoot);
    if (!status.Ok()) return Status::Failure(status.Error());
    if (status.Value().changedFiles > 0)
        return Status::Failure("Engine has uncommitted changes; publish or discard them first");
    const std::filesystem::path engine = projectRoot / ProjectInfo::EngineFolderName;
    const std::string branch = status.Value().trackedBranch;
    if (Status fetched = Step(engine, {"fetch", "origin"}, onLine); !fetched.Ok()) return fetched;
    if (status.Value().Detached()) {
        if (Status attached = AttachToBranch(engine, branch, onLine); !attached.Ok()) return attached;
    } else if (status.Value().branch != branch) {
        if (Status switched = Step(engine, {"switch", branch}, onLine); !switched.Ok()) return switched;
    }
    if (Status pulled = Step(engine, {"pull", "--ff-only", "origin", branch}, onLine); !pulled.Ok()) return pulled;
    Result<EngineGitStatus> after = ReadEngineGitStatus(projectRoot);
    if (!after.Ok()) return Status::Failure(after.Error());
    if (!after.Value().pointerChanged) {
        if (onLine) onLine("Engine is already up to date");
        return Status::Success();
    }
    return CommitPointer(projectRoot, "Update Engine", onLine);
}

Status PublishEngineChanges(const std::filesystem::path& projectRoot, const std::string& message,
                            const OutputLine& onLine) {
    if (message.find_first_not_of(" \t\r\n") == std::string::npos)
        return Status::Failure("Write a commit message for the engine change");
    Result<EngineGitStatus> status = ReadEngineGitStatus(projectRoot);
    if (!status.Ok()) return Status::Failure(status.Error());
    const std::filesystem::path engine = projectRoot / ProjectInfo::EngineFolderName;
    const std::string branch = status.Value().trackedBranch;
    if (status.Value().Detached()) {
        if (Status attached = AttachToBranch(engine, branch, onLine); !attached.Ok()) return attached;
    } else if (status.Value().branch != branch) {
        return Status::Failure("Engine is on branch " + status.Value().branch + ", not " + branch +
                               "; switch it by hand");
    }
    if (status.Value().changedFiles > 0) {
        if (Status added = Step(engine, {"add", "-A"}, onLine); !added.Ok()) return added;
        if (Status committed = Step(engine, {"commit", "-m", message}, onLine); !committed.Ok()) return committed;
    }
    if (Status pulled = Step(engine, {"pull", "--rebase", "origin", branch}, onLine); !pulled.Ok()) return pulled;
    if (Status pushed = Step(engine, {"push", "origin", branch}, onLine); !pushed.Ok()) return pushed;
    Result<EngineGitStatus> after = ReadEngineGitStatus(projectRoot);
    if (!after.Ok()) return Status::Failure(after.Error());
    if (!after.Value().pointerChanged) {
        if (onLine) onLine("Nothing to publish");
        return Status::Success();
    }
    return CommitPointer(projectRoot, "Update Engine: " + message, onLine);
}

Status InitDevelopmentRepository(const std::filesystem::path& root, const std::string& repository,
                                 const std::string& branch, const OutputLine& onLine) {
    if (repository.empty())
        return Status::Failure("The engine manifest names no repository; developer projects need one");
    if (Status initialized = Step(root, {"init", "-b", "main"}, onLine); !initialized.Ok()) return initialized;
    return Step(root, {"submodule", "add", "-b", branch, repository, ProjectInfo::EngineFolderName}, onLine);
}

Status CommitNewProject(const std::filesystem::path& root, const std::string& name, const OutputLine& onLine) {
    if (Status added = Step(root, {"add", "-A"}, onLine); !added.Ok()) return added;
    return Step(root, {"commit", "-m", "Create " + name}, onLine);
}
