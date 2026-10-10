/// UzEngine Process Management Implementation (Windows)
///
/// CreateProcessW with anonymous pipe capture for stdout/stderr.

#include "platform_process.h"

#ifdef _WIN32

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Converts a UTF-8 string to a wide string. Returns empty on failure.
std::wstring Utf8ToWide(const char* str) {
    if (!str || str[0] == '\0') return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<size_t>(len) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str, -1, result.data(), len);
    return result;
}

/// Non-blocking read from a pipe handle. Returns bytes read.
size_t ReadPipeNonBlocking(void* pipeHandle, char* buffer, size_t bufferSize) {
    if (!pipeHandle || !buffer || bufferSize == 0) return 0;

    HANDLE pipe = static_cast<HANDLE>(pipeHandle);
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
        return 0;
    }

    DWORD toRead = (available < static_cast<DWORD>(bufferSize))
                       ? available
                       : static_cast<DWORD>(bufferSize);
    DWORD bytesRead = 0;
    if (!ReadFile(pipe, buffer, toRead, &bytesRead, nullptr)) {
        return 0;
    }
    return static_cast<size_t>(bytesRead);
}

/// Closes a Win32 HANDLE and sets the void* to nullptr.
void SafeCloseHandle(void*& h) {
    if (h) {
        CloseHandle(static_cast<HANDLE>(h));
        h = nullptr;
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

ProcessHandle ProcessCreate(const char* exe, const char* args, const char* workDir) {
    ProcessHandle result{};

    if (!exe) {
        UZ_LOG(Platform, Error, "ProcessCreate called with nullptr exe path");
        return result;
    }

    // Build command line: "exe" args
    std::wstring cmdLine = L"\"" + Utf8ToWide(exe) + L"\"";
    if (args && args[0] != '\0') {
        cmdLine += L" ";
        cmdLine += Utf8ToWide(args);
    }

    std::wstring wideWorkDir;
    const wchar_t* workDirPtr = nullptr;
    if (workDir && workDir[0] != '\0') {
        wideWorkDir = Utf8ToWide(workDir);
        workDirPtr = wideWorkDir.c_str();
    }

    // Create stdout pipe
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0)) {
        UZ_LOG(Platform, Error, "ProcessCreate: stdout CreatePipe failed, error={}", GetLastError());
        return result;
    }
    // Parent read end must not be inherited
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

    // Create stderr pipe
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    if (!CreatePipe(&stderrRead, &stderrWrite, &sa, 0)) {
        UZ_LOG(Platform, Error, "ProcessCreate: stderr CreatePipe failed, error={}", GetLastError());
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        return result;
    }
    SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0);

    // Launch process
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdoutWrite;
    si.hStdError = stderrWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(
        nullptr,
        cmdLine.data(),
        nullptr,    // process security
        nullptr,    // thread security
        TRUE,       // inherit handles
        0,          // creation flags
        nullptr,    // environment (inherit)
        workDirPtr,
        &si,
        &pi
    );

    // Close write ends — child owns them now (or they're unused on failure)
    CloseHandle(stdoutWrite);
    CloseHandle(stderrWrite);

    if (!ok) {
        UZ_LOG(Platform, Error, "ProcessCreate: CreateProcessW failed for '{}', error={}", exe, GetLastError());
        CloseHandle(stdoutRead);
        CloseHandle(stderrRead);
        return result;
    }

    // Don't need the thread handle
    CloseHandle(pi.hThread);

    result.hProcess = static_cast<void*>(pi.hProcess);
    result.hStdoutRead = static_cast<void*>(stdoutRead);
    result.hStderrRead = static_cast<void*>(stderrRead);
    result.processId = pi.dwProcessId;

    UZ_LOG(Platform, Info, "Process created: '{}' (PID={})", exe, result.processId);
    return result;
}

size_t ProcessReadStdout(const ProcessHandle& proc, char* buffer, size_t bufferSize) {
    return ReadPipeNonBlocking(proc.hStdoutRead, buffer, bufferSize);
}

size_t ProcessReadStderr(const ProcessHandle& proc, char* buffer, size_t bufferSize) {
    return ReadPipeNonBlocking(proc.hStderrRead, buffer, bufferSize);
}

int ProcessWait(const ProcessHandle& proc, uint32_t timeoutMs) {
    if (!proc.hProcess) return -1;

    DWORD ms = (timeoutMs == 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
    DWORD waitResult = WaitForSingleObject(static_cast<HANDLE>(proc.hProcess), ms);

    if (waitResult == WAIT_TIMEOUT) {
        return -1;
    }
    if (waitResult != WAIT_OBJECT_0) {
        UZ_LOG(Platform, Error, "ProcessWait: WaitForSingleObject failed, error={}", GetLastError());
        return -1;
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(static_cast<HANDLE>(proc.hProcess), &exitCode);
    return static_cast<int>(exitCode);
}

bool ProcessIsRunning(const ProcessHandle& proc) {
    if (!proc.hProcess) return false;
    return WaitForSingleObject(static_cast<HANDLE>(proc.hProcess), 0) == WAIT_TIMEOUT;
}

void ProcessTerminate(ProcessHandle& proc) {
    if (!proc.hProcess) return;
    UZ_LOG(Platform, Warning, "Terminating process PID={}", proc.processId);
    TerminateProcess(static_cast<HANDLE>(proc.hProcess), 1);
}

void ProcessClose(ProcessHandle& proc) {
    SafeCloseHandle(proc.hProcess);
    SafeCloseHandle(proc.hStdoutRead);
    SafeCloseHandle(proc.hStderrRead);
    proc.processId = 0;
}

} // namespace uz

#endif // _WIN32
