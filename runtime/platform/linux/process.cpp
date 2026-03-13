/// UzEngine Process Management Implementation (Linux)
///
/// fork/execvp with anonymous pipe capture for stdout/stderr.
/// Non-blocking reads via fcntl(O_NONBLOCK) on the parent's read ends.

#include "process.h"

#ifdef __linux__

#include "log.h"
#include "profiler.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Returns the file descriptor encoded in a void* handle, or -1 if null.
[[nodiscard]] int FdFromHandle(void* h) noexcept {
    if (!h) return -1;
    return static_cast<int>(reinterpret_cast<intptr_t>(h));
}

/// Encodes a file descriptor as a void* handle.
[[nodiscard]] void* HandleFromFd(int fd) noexcept {
    return reinterpret_cast<void*>(static_cast<intptr_t>(fd));
}

/// Closes a fd-as-void* and zeroes the pointer. No-op if null or fd < 0.
void SafeCloseFd(void*& h) noexcept {
    const int fd = FdFromHandle(h);
    if (fd >= 0) {
        ::close(fd);
    }
    h = nullptr;
}

/// Enables O_NONBLOCK on a file descriptor. Returns false on error.
bool SetNonBlocking(int fd) noexcept {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

/// Non-blocking read from an fd-as-void* handle. Returns bytes read.
size_t ReadFdNonBlocking(void* handle, char* buffer, size_t bufferSize) noexcept {
    if (!handle || !buffer || bufferSize == 0) return 0;

    const int fd = FdFromHandle(handle);
    if (fd < 0) return 0;

    const ssize_t n = ::read(fd, buffer, bufferSize);
    if (n <= 0) {
        // EAGAIN/EWOULDBLOCK = no data yet; EOF and errors both return 0
        return 0;
    }
    return static_cast<size_t>(n);
}

/// Splits a command-line string into an argv vector terminated by nullptr.
/// The first element is the exe; subsequent elements are the args tokens.
std::vector<char*> BuildArgv(const std::string& exe, const char* args,
                              std::vector<std::string>& storage) {
    storage.clear();
    storage.push_back(exe); // argv[0] = exe name

    if (args && args[0] != '\0') {
        // Tokenise on spaces (simple split, no quoting support)
        std::string token;
        for (const char* p = args; *p; ++p) {
            if (*p == ' ') {
                if (!token.empty()) {
                    storage.push_back(token);
                    token.clear();
                }
            } else {
                token += *p;
            }
        }
        if (!token.empty()) {
            storage.push_back(token);
        }
    }

    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& s : storage) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr); // execvp sentinel
    return argv;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

ProcessHandle ProcessCreate(const char* exe, const char* args, const char* workDir) {
    UZ_PROFILE_FUNCTION();

    ProcessHandle result{};

    if (!exe) {
        UZ_LOG(Platform, Error, "ProcessCreate called with nullptr exe path");
        return result;
    }

    // Create stdout pipe: [0]=read, [1]=write
    int stdoutPipe[2] = { -1, -1 };
    if (::pipe(stdoutPipe) != 0) {
        UZ_LOG(Platform, Error, "ProcessCreate: stdout pipe() failed: {}", ::strerror(errno));
        return result;
    }

    // Create stderr pipe: [0]=read, [1]=write
    int stderrPipe[2] = { -1, -1 };
    if (::pipe(stderrPipe) != 0) {
        UZ_LOG(Platform, Error, "ProcessCreate: stderr pipe() failed: {}", ::strerror(errno));
        ::close(stdoutPipe[0]);
        ::close(stdoutPipe[1]);
        return result;
    }

    const pid_t pid = ::fork();

    if (pid < 0) {
        UZ_LOG(Platform, Error, "ProcessCreate: fork() failed: {}", ::strerror(errno));
        ::close(stdoutPipe[0]);
        ::close(stdoutPipe[1]);
        ::close(stderrPipe[0]);
        ::close(stderrPipe[1]);
        return result;
    }

    if (pid == 0) {
        // ── Child process ────────────────────────────────────────────────────
        // Close read ends — child only writes
        ::close(stdoutPipe[0]);
        ::close(stderrPipe[0]);

        // Redirect stdout/stderr to write ends of pipes
        ::dup2(stdoutPipe[1], STDOUT_FILENO);
        ::dup2(stderrPipe[1], STDERR_FILENO);

        // Close original write ends (now duplicated)
        ::close(stdoutPipe[1]);
        ::close(stderrPipe[1]);

        // Change working directory if requested
        if (workDir && workDir[0] != '\0') {
            if (::chdir(workDir) != 0) {
                // Can't use UZ_LOG here (unsafe in child post-fork)
                // Write raw error to stderr (now our pipe)
                const char* msg = "ProcessCreate: chdir failed\n";
                ::write(STDERR_FILENO, msg, ::strlen(msg));
                ::_exit(127);
            }
        }

        // Build argv and exec
        std::vector<std::string> tokenStorage;
        std::vector<char*> argv = BuildArgv(std::string(exe), args, tokenStorage);

        ::execvp(exe, argv.data());

        // execvp only returns on failure
        const char* msg = "ProcessCreate: execvp failed\n";
        ::write(STDERR_FILENO, msg, ::strlen(msg));
        ::_exit(127);
    }

    // ── Parent process ───────────────────────────────────────────────────────
    // Close write ends — parent only reads
    ::close(stdoutPipe[1]);
    ::close(stderrPipe[1]);

    // Set parent read ends to non-blocking
    if (!SetNonBlocking(stdoutPipe[0])) {
        UZ_LOG(Platform, Warning, "ProcessCreate: failed to set stdout pipe non-blocking");
    }
    if (!SetNonBlocking(stderrPipe[0])) {
        UZ_LOG(Platform, Warning, "ProcessCreate: failed to set stderr pipe non-blocking");
    }

    result.hProcess    = reinterpret_cast<void*>(static_cast<intptr_t>(pid));
    result.hStdoutRead = HandleFromFd(stdoutPipe[0]);
    result.hStderrRead = HandleFromFd(stderrPipe[0]);
    result.processId   = static_cast<uint32_t>(pid);

    UZ_LOG(Platform, Info, "Process created: '{}' (PID={})", exe, result.processId);
    return result;
}

size_t ProcessReadStdout(const ProcessHandle& proc, char* buffer, size_t bufferSize) {
    return ReadFdNonBlocking(proc.hStdoutRead, buffer, bufferSize);
}

size_t ProcessReadStderr(const ProcessHandle& proc, char* buffer, size_t bufferSize) {
    return ReadFdNonBlocking(proc.hStderrRead, buffer, bufferSize);
}

int ProcessWait(const ProcessHandle& proc, uint32_t timeoutMs) {
    if (!proc.hProcess) return -1;

    const pid_t pid = static_cast<pid_t>(reinterpret_cast<intptr_t>(proc.hProcess));

    if (timeoutMs == 0) {
        // Blocking wait
        int status = 0;
        if (::waitpid(pid, &status, 0) < 0) {
            UZ_LOG(Platform, Error, "ProcessWait: waitpid failed: {}", ::strerror(errno));
            return -1;
        }
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return -1;
        return -1;
    }

    // Timed poll: check every 1ms until timeout
    const uint32_t kPollIntervalMs = 1;
    uint32_t elapsed = 0;
    while (elapsed <= timeoutMs) {
        int status = 0;
        const pid_t ret = ::waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            if (WIFSIGNALED(status)) return -1;
            return -1;
        }
        if (ret < 0) {
            UZ_LOG(Platform, Error, "ProcessWait: waitpid failed: {}", ::strerror(errno));
            return -1;
        }

        // Process still running — sleep 1ms
        struct timespec ts { 0, static_cast<long>(kPollIntervalMs) * 1'000'000L };
        ::nanosleep(&ts, nullptr);
        elapsed += kPollIntervalMs;
    }

    return -1; // Timed out
}

bool ProcessIsRunning(const ProcessHandle& proc) {
    if (!proc.hProcess) return false;

    const pid_t pid = static_cast<pid_t>(reinterpret_cast<intptr_t>(proc.hProcess));
    int status = 0;
    const pid_t ret = ::waitpid(pid, &status, WNOHANG);

    // ret == 0: still running; ret == pid: exited; ret < 0: error (already reaped)
    return ret == 0;
}

void ProcessTerminate(ProcessHandle& proc) {
    if (!proc.hProcess) return;

    const pid_t pid = static_cast<pid_t>(reinterpret_cast<intptr_t>(proc.hProcess));
    UZ_LOG(Platform, Warning, "Terminating process PID={}", proc.processId);
    ::kill(pid, SIGKILL);
}

void ProcessClose(ProcessHandle& proc) {
    SafeCloseFd(proc.hStdoutRead);
    SafeCloseFd(proc.hStderrRead);

    if (proc.hProcess) {
        const pid_t pid = static_cast<pid_t>(reinterpret_cast<intptr_t>(proc.hProcess));
        // Reap zombie (non-blocking — we don't care about the exit code here)
        int status = 0;
        ::waitpid(pid, &status, WNOHANG);
        proc.hProcess = nullptr;
    }

    proc.processId = 0;
}

} // namespace uz

#endif // __linux__
