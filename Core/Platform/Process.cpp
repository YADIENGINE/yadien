#include "Platform/Process.h"
#include <windows.h>
#include <algorithm>
#include <map>
#include "Platform/Text.h"

namespace {
constexpr DWORD ControlCExit = 0xC000013A; // STATUS_CONTROL_C_EXIT
constexpr DWORD PollMilliseconds = 20;
constexpr DWORD ReadBufferBytes = 4096;

// Windows keeps a handle per object; this owns one.
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle)
        : _handle(handle == INVALID_HANDLE_VALUE ? nullptr : handle) {}
    Handle(Handle&& other) noexcept
        : _handle(std::exchange(other._handle, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        Reset();
        _handle = std::exchange(other._handle, nullptr);
        return *this;
    }
    ~Handle() { Reset(); }

    [[nodiscard]] HANDLE Get() const { return _handle; }
    void Reset() {
        if (_handle != nullptr) CloseHandle(_handle);
        _handle = nullptr;
    }

private:
    HANDLE _handle = nullptr;

public:
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

void AppendQuoted(std::wstring& line, const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += argument;
        return;
    }
    line += L'"';
    size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        // Backslashes before a quote are doubled, and the quote itself is escaped
        line.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        line += c;
    }
    line.append(backslashes * 2, L'\\');
    line += L'"';
}

struct CaseInsensitiveLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()),
                                    TRUE) == CSTR_LESS_THAN;
    }
};

// The child's environment block: the launcher's own, with the command's changes and PATH prefix.
std::wstring EnvironmentBlock(const ProcessCommand& command) {
    std::map<std::wstring, std::wstring, CaseInsensitiveLess> variables;
    std::vector<std::wstring> driveEntries; // "=C:=C:\..." entries keep the per-drive current folders
    if (wchar_t* inherited = GetEnvironmentStringsW(); inherited != nullptr) {
        for (const wchar_t* entry = inherited; *entry != L'\0'; entry += wcslen(entry) + 1) {
            const std::wstring text(entry);
            const size_t equals = text.find(L'=', 1);
            if (text.front() == L'=')
                driveEntries.push_back(text);
            else if (equals != std::wstring::npos)
                variables[text.substr(0, equals)] = text.substr(equals + 1);
        }
        FreeEnvironmentStringsW(inherited);
    }
    for (const auto& [name, value] : command.environment) {
        if (value.has_value())
            variables[Utf8ToWide(name)] = Utf8ToWide(*value);
        else
            variables.erase(Utf8ToWide(name));
    }
    if (!command.pathPrefix.empty()) {
        std::wstring path;
        for (const std::filesystem::path& folder : command.pathPrefix)
            path += folder.native() + L";";
        variables[L"PATH"] = path + variables[L"PATH"];
    }

    std::wstring block;
    for (const std::wstring& entry : driveEntries)
        block += entry + L'\0';
    for (const auto& [name, value] : variables)
        block += name + L'=' + value + L'\0';
    block += L'\0';
    return block;
}

std::string DecodeLine(const std::string& bytes) {
    if (bytes.empty()) return bytes;
    const int size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (size > 0) return bytes;
    const int oemSize = MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(oemSize), L'\0');
    MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), oemSize);
    return WideToUtf8(wide);
}

// Splits the stream into lines; a lone '\r' (progress output) also ends a line.
class LineSplitter {
public:
    explicit LineSplitter(const OutputLine& onLine)
        : _onLine(onLine) {}

    void Add(const char* data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            const char c = data[i];
            if (c == '\n') {
                Emit();
            } else if (c == '\r') {
                _carriageReturn = true;
            } else {
                if (_carriageReturn) Emit();
                _pending.push_back(c);
            }
        }
    }

    void Finish() {
        if (!_pending.empty()) Emit();
    }

private:
    void Emit() {
        _carriageReturn = false;
        if (_onLine) _onLine(DecodeLine(_pending));
        _pending.clear();
    }

    const OutputLine& _onLine;
    std::string _pending;
    bool _carriageReturn = false;
};

struct Started {
    Handle process;
    Handle job;
    std::string error;
};

// Starts suspended inside a job object (when `useJob`), so the job holds the child and its children.
Started Start(const ProcessCommand& command, STARTUPINFOW& startup, BOOL inheritHandles, DWORD flags, bool useJob) {
    Started started;
    std::wstring commandLine = BuildCommandLine(command);
    std::wstring environment = EnvironmentBlock(command);
    const std::wstring folder = command.workingDirectory.native();
    PROCESS_INFORMATION info{};
    // A bare name ("git") is searched on PATH through the command line; a path is started as is.
    const wchar_t* application = command.executable.is_absolute() ? command.executable.c_str() : nullptr;
    if (!CreateProcessW(application, commandLine.data(), nullptr, nullptr, inheritHandles,
                        flags | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, environment.data(),
                        folder.empty() ? nullptr : folder.c_str(), &startup, &info)) {
        started.error = "Cannot start " + PathToUtf8(command.executable) + ": " + LastWindowsError();
        return started;
    }
    started.process = Handle(info.hProcess);
    Handle thread(info.hThread);
    if (useJob) {
        started.job = Handle(CreateJobObjectW(nullptr, nullptr));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(started.job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(started.job.Get(), started.process.Get());
    }
    ResumeThread(thread.Get());
    return started;
}

int ExitCodeOf(HANDLE process) {
    DWORD code = 0;
    GetExitCodeProcess(process, &code);
    return code == ControlCExit ? InterruptedExitCode : static_cast<int>(code);
}
} // namespace

std::wstring BuildCommandLine(const ProcessCommand& command) {
    std::wstring line;
    AppendQuoted(line, command.executable.native());
    for (const std::string& argument : command.arguments) {
        line += L' ';
        AppendQuoted(line, Utf8ToWide(argument));
    }
    return line;
}

ProcessResult RunAttached(const ProcessCommand& command) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    ProcessResult result;
    Started started = Start(command, startup, TRUE, 0, true);
    if (!started.error.empty()) {
        result.startError = started.error;
        return result;
    }
    SetConsoleCtrlHandler(nullptr, TRUE); // The child answers Ctrl+C; the launcher waits for its exit code
    WaitForSingleObject(started.process.Get(), INFINITE);
    SetConsoleCtrlHandler(nullptr, FALSE);
    result.exitCode = ExitCodeOf(started.process.Get());
    return result;
}

ProcessResult RunCaptured(const ProcessCommand& command, const OutputLine& onLine, const std::atomic<bool>* cancel) {
    ProcessResult result;
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0)) {
        result.startError = "Cannot create a pipe: " + LastWindowsError();
        return result;
    }
    Handle reader(readEnd);
    Handle writer(writeEnd);
    SetHandleInformation(reader.Get(), HANDLE_FLAG_INHERIT, 0);
    Handle nul(
        CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul.Get();
    startup.hStdOutput = writer.Get();
    startup.hStdError = writer.Get();
    Started started = Start(command, startup, TRUE, CREATE_NO_WINDOW, true);
    writer.Reset(); // Only the child holds the write end now: reading ends when it (and its children) exit
    if (!started.error.empty()) {
        result.startError = started.error;
        return result;
    }

    LineSplitter lines(onLine);
    std::string buffer(ReadBufferBytes, '\0');
    while (true) {
        if (cancel != nullptr && cancel->load()) {
            TerminateJobObject(started.job.Get(), 1);
            WaitForSingleObject(started.process.Get(), INFINITE);
            lines.Finish();
            result.canceled = true;
            result.exitCode = ExitCodeOf(started.process.Get());
            return result;
        }
        DWORD available = 0;
        if (!PeekNamedPipe(reader.Get(), nullptr, 0, nullptr, &available, nullptr)) break; // Write end closed
        if (available == 0) {
            Sleep(PollMilliseconds);
            continue;
        }
        DWORD read = 0;
        if (!ReadFile(reader.Get(), buffer.data(), std::min<DWORD>(available, ReadBufferBytes), &read, nullptr)) break;
        lines.Add(buffer.data(), read);
    }
    lines.Finish();
    WaitForSingleObject(started.process.Get(), INFINITE);
    result.exitCode = ExitCodeOf(started.process.Get());
    return result;
}

std::string StartDetached(const ProcessCommand& command) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    // A hidden console (not none): console programs it starts in turn (build tools) do not open windows.
    Started started = Start(command, startup, FALSE, CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW, false);
    return started.error;
}
