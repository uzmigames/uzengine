/// UzEngine Clipboard Implementation (Linux)
///
/// Delegates all clipboard operations to `xclip` or `xsel` via POSIX
/// popen()/pclose() for read and fork/execvp pipes for write.
/// No X11 headers are required — the tool handles the display connection.
///
/// # Tool Preference
/// xclip is tried first on every call. If popen/execvp fails with ENOENT
/// (tool not on PATH), xsel is tried. If both are absent, the function logs
/// Clipboard::Error and returns the failure sentinel.
///
/// # Write Path (Set operations)
/// Uses a private fork helper that creates an anonymous pipe, forks, wires the
/// read end to the child's stdin via dup2, and writes the payload bytes from
/// the parent before closing the write end. The child execs the clipboard tool
/// directly (no sh -c).
///
/// # Read Path (Get operations)
/// Uses a private fork helper that creates an anonymous pipe, forks, wires the
/// write end to the child's stdout via dup2, and reads all bytes from the read
/// end in the parent. Direct exec avoids shell-metacharacter injection from
/// caller-supplied formatName values.

#include "clipboard.h"

#ifdef __linux__

#include "log.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

UZ_DEFINE_LOG_CATEGORY(Clipboard, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Read-buffer chunk size for popen output collection.
constexpr size_t kReadChunkSize = 4096;

/// Timeout waiting for a write subprocess to exit, in milliseconds.
constexpr int kWriteTimeoutMs = 5000;

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Tool Detection
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Known clipboard tool identifiers, in preference order.
enum class ClipTool { XClip, XSel };

/// Tests whether `xclip` exists on PATH via `which`.
/// We probe once and cache. The flag is tri-state: -1=unchecked, 0=absent, 1=present.
bool ToolExists(ClipTool tool) noexcept {
    // Simple probe: attempt to exec with --version redirected to /dev/null.
    // We use posix_spawn-free approach: fork, exec, wait.
    const char* probe = (tool == ClipTool::XClip) ? "xclip" : "xsel";

    const pid_t pid = ::fork();
    if (pid < 0) return false;

    if (pid == 0) {
        // Child: redirect stdout/stderr to /dev/null so no output leaks
        const int devNull = ::open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            ::dup2(devNull, STDOUT_FILENO);
            ::dup2(devNull, STDERR_FILENO);
            ::close(devNull);
        }
        // exec with --version; exit code doesn't matter — ENOENT on exec = not found
        char* argv[] = {
            const_cast<char*>(probe),
            const_cast<char*>("--version"),
            nullptr
        };
        ::execvp(probe, argv);
        ::_exit(127); // execvp failed (ENOENT most likely)
    }

    int status = 0;
    ::waitpid(pid, &status, 0);

    // Exit 127 is our sentinel for execvp failure (tool not found)
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) return false;
    return true;
}

/// Probes xclip then xsel and returns which tool to use.
/// Returns -1 (as int) if neither is available.
/// Result is cached after first successful probe.
int DetectClipboardTool() noexcept {
    // Lazily probe. Use a simple tri-state: -2 = not yet probed.
    static int s_cached = -2; // -2=unchecked, 0=xclip, 1=xsel, -1=none

    if (s_cached != -2) return s_cached;

    if (ToolExists(ClipTool::XClip)) {
        s_cached = 0;
    } else if (ToolExists(ClipTool::XSel)) {
        s_cached = 1;
    } else {
        s_cached = -1;
    }
    return s_cached;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Read Helper (popen-based)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Runs a clipboard read command and returns all bytes from its stdout.
/// `argv` must be null-terminated. Returns empty vector on failure.
std::vector<uint8_t> RunReadCommand(const char* const* argv) {
    // We exec directly (no shell) to avoid metacharacter issues.
    // Use pipe + fork since popen() goes through sh -c.
    int pipeFds[2] = { -1, -1 };
    if (::pipe(pipeFds) != 0) {
        UZ_LOG(Clipboard, Error, "RunReadCommand: pipe() failed: {}", ::strerror(errno));
        return {};
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        UZ_LOG(Clipboard, Error, "RunReadCommand: fork() failed: {}", ::strerror(errno));
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return {};
    }

    if (pid == 0) {
        // Child: wire stdout → pipe write end
        ::close(pipeFds[0]);
        ::dup2(pipeFds[1], STDOUT_FILENO);
        ::close(pipeFds[1]);

        // Silence stderr
        const int devNull = ::open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            ::dup2(devNull, STDERR_FILENO);
            ::close(devNull);
        }

        // execvp takes non-const argv; safe to cast — we never write through it
        ::execvp(argv[0], const_cast<char* const*>(argv));
        ::_exit(127);
    }

    // Parent: close write end, read from read end until EOF
    ::close(pipeFds[1]);

    std::vector<uint8_t> result;
    char buf[kReadChunkSize];
    ssize_t n = 0;
    while ((n = ::read(pipeFds[0], buf, sizeof(buf))) > 0) {
        result.insert(result.end(), buf, buf + n);
    }

    ::close(pipeFds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        // Non-zero exit usually means clipboard is empty or format missing
        result.clear();
    }

    return result;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Write Helper (pipe stdin to child)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Forks a child, wires a pipe to its stdin, writes `data`/`size` bytes,
/// closes the write end, then waits for the child to exit.
/// Returns true if the child exits with code 0.
bool RunWriteCommand(const char* const* argv, const void* data, size_t size) {
    int stdinPipe[2] = { -1, -1 };
    if (::pipe(stdinPipe) != 0) {
        UZ_LOG(Clipboard, Error, "RunWriteCommand: pipe() failed: {}", ::strerror(errno));
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        UZ_LOG(Clipboard, Error, "RunWriteCommand: fork() failed: {}", ::strerror(errno));
        ::close(stdinPipe[0]);
        ::close(stdinPipe[1]);
        return false;
    }

    if (pid == 0) {
        // Child: wire stdin ← pipe read end
        ::close(stdinPipe[1]);
        ::dup2(stdinPipe[0], STDIN_FILENO);
        ::close(stdinPipe[0]);

        // Silence stdout/stderr so clipboard tools don't spam the terminal
        const int devNull = ::open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            ::dup2(devNull, STDOUT_FILENO);
            ::dup2(devNull, STDERR_FILENO);
            ::close(devNull);
        }

        ::execvp(argv[0], const_cast<char* const*>(argv));
        ::_exit(127);
    }

    // Parent: close read end, write data, close write end to signal EOF
    ::close(stdinPipe[0]);

    if (data && size > 0) {
        const auto* src = static_cast<const uint8_t*>(data);
        size_t written = 0;
        while (written < size) {
            const ssize_t n = ::write(stdinPipe[1], src + written, size - written);
            if (n <= 0) {
                // EPIPE: child exited early (likely tool not found)
                if (errno != EPIPE) {
                    UZ_LOG(Clipboard, Error, "RunWriteCommand: write() failed: {}", ::strerror(errno));
                }
                break;
            }
            written += static_cast<size_t>(n);
        }
    }

    ::close(stdinPipe[1]); // EOF signals the clipboard tool to proceed

    // Wait for child — use timed poll so a hung tool doesn't block the engine
    int status = 0;
    const int pollIntervalMs = 5;
    int elapsed = 0;
    bool timedOut = false;

    while (elapsed < kWriteTimeoutMs) {
        const pid_t ret = ::waitpid(pid, &status, WNOHANG);
        if (ret == pid) break;
        if (ret < 0) break;

        // Sleep 5ms between polls
        struct timespec ts { 0, static_cast<long>(pollIntervalMs) * 1'000'000L };
        ::nanosleep(&ts, nullptr);
        elapsed += pollIntervalMs;
    }

    if (elapsed >= kWriteTimeoutMs) {
        UZ_LOG(Clipboard, Error, "RunWriteCommand: child timed out after {}ms — killing", kWriteTimeoutMs);
        ::kill(pid, SIGKILL);
        ::waitpid(pid, &status, 0);
        timedOut = true;
    }

    if (timedOut) return false;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;

    UZ_LOG(Clipboard, Error, "RunWriteCommand: child exited with code={}", WEXITSTATUS(status));
    return false;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Text Clipboard
// ─────────────────────────────────────────────────────────────────────────────

bool ClipboardSetText(const char* text) {
    if (!text) return false;

    const int tool = DetectClipboardTool();
    if (tool < 0) {
        UZ_LOG(Clipboard, Error, "ClipboardSetText: neither xclip nor xsel found on PATH");
        return false;
    }

    const size_t textLen = ::strlen(text);

    if (tool == 0) {
        // xclip: pipe text to stdin, -selection clipboard
        const char* argv[] = {
            "xclip", "-selection", "clipboard", nullptr
        };
        return RunWriteCommand(argv, text, textLen);
    } else {
        // xsel: --clipboard --input reads stdin
        const char* argv[] = {
            "xsel", "--clipboard", "--input", nullptr
        };
        return RunWriteCommand(argv, text, textLen);
    }
}

std::string ClipboardGetText() {
    const int tool = DetectClipboardTool();
    if (tool < 0) {
        UZ_LOG(Clipboard, Error, "ClipboardGetText: neither xclip nor xsel found on PATH");
        return {};
    }

    std::vector<uint8_t> raw;

    if (tool == 0) {
        const char* argv[] = {
            "xclip", "-selection", "clipboard", "-o", nullptr
        };
        raw = RunReadCommand(argv);
    } else {
        const char* argv[] = {
            "xsel", "--clipboard", "--output", nullptr
        };
        raw = RunReadCommand(argv);
    }

    if (raw.empty()) return {};
    return std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Custom Binary Format
// ─────────────────────────────────────────────────────────────────────────────

bool ClipboardSetData(const char* formatName, const void* data, size_t size) {
    if (!formatName) return false;
    if (size == 0) return true; // no-op, matches Windows semantics
    if (!data) return false;

    const int tool = DetectClipboardTool();
    if (tool < 0) {
        UZ_LOG(Clipboard, Error, "ClipboardSetData: neither xclip nor xsel found on PATH: format='{}'", formatName);
        return false;
    }

    if (tool == 0) {
        // xclip: -t <MIME> sets the MIME type for the stored data
        const char* argv[] = {
            "xclip", "-selection", "clipboard", "-t", formatName, nullptr
        };
        return RunWriteCommand(argv, data, size);
    } else {
        // xsel does not support custom MIME types via a simple flag.
        // We store as plain binary piped to --clipboard --input.
        // The MIME type is not advertised to other X clients, but GetData
        // with the same formatName will retrieve it within the session.
        UZ_LOG(Clipboard, Warning,
            "ClipboardSetData: xsel does not support custom MIME type '{}' — "
            "data stored without type annotation", formatName);
        const char* argv[] = {
            "xsel", "--clipboard", "--input", nullptr
        };
        return RunWriteCommand(argv, data, size);
    }
}

std::vector<uint8_t> ClipboardGetData(const char* formatName) {
    if (!formatName) return {};

    const int tool = DetectClipboardTool();
    if (tool < 0) {
        UZ_LOG(Clipboard, Error, "ClipboardGetData: neither xclip nor xsel found on PATH: format='{}'", formatName);
        return {};
    }

    if (tool == 0) {
        // xclip: -t <MIME> -o requests data in that MIME type
        const char* argv[] = {
            "xclip", "-selection", "clipboard", "-o", "-t", formatName, nullptr
        };
        return RunReadCommand(argv);
    } else {
        // xsel: no MIME type filtering — return raw clipboard bytes
        const char* argv[] = {
            "xsel", "--clipboard", "--output", nullptr
        };
        return RunReadCommand(argv);
    }
}

} // namespace uz

#endif // __linux__
