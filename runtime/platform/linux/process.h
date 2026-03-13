#pragma once

/// UzEngine Process Management — Child process creation and I/O on Linux.
///
/// Wraps POSIX fork/execvp with pipe-based stdout/stderr capture.
/// Provides non-blocking reads, wait, and termination via POSIX signals.
///
/// # Lifecycle
/// 1. `ProcessCreate()` — spawns child with piped stdout/stderr via fork+execvp
/// 2. `ProcessReadStdout()`/`ProcessReadStderr()` — poll output (non-blocking)
/// 3. `ProcessWait()` or `ProcessIsRunning()` — check completion via waitpid
/// 4. `ProcessClose()` — close pipe fds and zero handle (MUST call when done)
///
/// # Thread Safety
/// Each ProcessHandle is independent. Do not share a single handle across
/// threads without external synchronization.

#ifdef __linux__

#include <cstddef>
#include <cstdint>

namespace uz {

/// Opaque handle bundle for a child process and its I/O pipes.
///
/// `hProcess` holds the child pid_t cast to void*.
/// `hStdoutRead` and `hStderrRead` hold the read-end file descriptors cast
/// to void* (intptr_t semantics). Zero-initialized = invalid/closed.
struct ProcessHandle {
    void*    hProcess    = nullptr; ///< Child pid_t cast to void*. 0 = invalid.
    void*    hStdoutRead = nullptr; ///< Read end of child stdout pipe (fd as void*).
    void*    hStderrRead = nullptr; ///< Read end of child stderr pipe (fd as void*).
    uint32_t processId   = 0;       ///< Child PID (same as hProcess, kept for symmetry).
};

/// Creates a child process with piped stdout and stderr.
///
/// Creates two anonymous pipes for stdout and stderr, then fork()s. The child
/// redirects its stdout/stderr via dup2() and exec()s the given executable
/// using execvp() so PATH is searched. The parent's read ends of both pipes
/// are set to non-blocking mode via fcntl().
///
/// # Arguments
/// * `exe` — UTF-8 path or name of the executable. Must not be nullptr.
///           If the path contains no '/', PATH is searched.
/// * `args` — Optional space-separated arguments (appended after exe).
///            nullptr or empty string means no extra arguments.
/// * `workDir` — Optional working directory for the child. nullptr = inherit.
///               Note: chdir() is called in the child after fork() and before exec().
///
/// # Returns
/// - Valid ProcessHandle (hProcess != nullptr) on success.
/// - Zero-initialized ProcessHandle on failure (check logs).
///
/// # Errors
/// Logs Platform::Error with errno description on pipe, fork, or exec failure.
ProcessHandle ProcessCreate(const char* exe, const char* args = nullptr, const char* workDir = nullptr);

/// Reads available stdout data from the child process (non-blocking).
///
/// Uses a non-blocking read() on the stdout pipe fd. Returns immediately
/// with 0 if no data is available (EAGAIN/EWOULDBLOCK) or the pipe is closed.
///
/// # Arguments
/// * `proc` — Handle from ProcessCreate().
/// * `buffer` — Destination buffer for output bytes.
/// * `bufferSize` — Maximum bytes to read.
///
/// # Returns
/// Number of bytes actually read (0 if nothing available or pipe closed).
size_t ProcessReadStdout(const ProcessHandle& proc, char* buffer, size_t bufferSize);

/// Reads available stderr data from the child process (non-blocking).
///
/// Same semantics as ProcessReadStdout but reads from the stderr pipe fd.
///
/// # Returns
/// Number of bytes actually read (0 if nothing available or pipe closed).
size_t ProcessReadStderr(const ProcessHandle& proc, char* buffer, size_t bufferSize);

/// Waits for the child process to exit and returns its exit code.
///
/// Uses waitpid() in blocking or timed-poll mode. When timeoutMs > 0, polls
/// every 1ms via waitpid(WNOHANG) until the process exits or time runs out.
///
/// # Arguments
/// * `proc` — Handle from ProcessCreate().
/// * `timeoutMs` — Maximum wait time in milliseconds. 0 = wait forever (blocking).
///
/// # Returns
/// - Process exit code (WEXITSTATUS) on normal termination.
/// - -1 if the wait timed out, the process was signalled, or an error occurred.
int ProcessWait(const ProcessHandle& proc, uint32_t timeoutMs = 0);

/// Checks whether the child process is still running.
///
/// Calls waitpid(WNOHANG) — does not block or consume the exit status.
///
/// # Returns
/// - `true` if the process has not yet exited.
/// - `false` if exited, was killed, or the handle is invalid.
bool ProcessIsRunning(const ProcessHandle& proc);

/// Forcibly terminates the child process with SIGKILL.
///
/// Calls kill(pid, SIGKILL). ProcessClose() must still be called afterward
/// to reap the zombie and close pipe fds.
///
/// # Arguments
/// * `proc` — Handle to terminate. hProcess is NOT nulled (call ProcessClose).
void ProcessTerminate(ProcessHandle& proc);

/// Closes all fds in the ProcessHandle and zeros them.
///
/// Closes the stdout and stderr read pipe fds, then waitpid()s to reap any
/// zombie. Must be called when the process is no longer needed, whether it
/// exited normally, was terminated, or failed to start. Safe to call multiple
/// times (null-checks each fd before closing).
void ProcessClose(ProcessHandle& proc);

} // namespace uz

#endif // __linux__
