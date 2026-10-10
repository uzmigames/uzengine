#pragma once

/// UzEngine Process Management — Child process creation and I/O on Windows.
/// Named explicitly to avoid shadowing the Windows CRT's <process.h>.
///
/// Wraps Win32 CreateProcess with stdout/stderr pipe capture.
/// Provides non-blocking reads, wait, and termination.
///
/// # Lifecycle
/// 1. `ProcessCreate()` — spawns child with piped stdout/stderr
/// 2. `ProcessReadStdout()`/`ProcessReadStderr()` — poll output (non-blocking)
/// 3. `ProcessWait()` or `ProcessIsRunning()` — check completion
/// 4. `ProcessClose()` — release all handles (MUST call when done)
///
/// # Thread Safety
/// Each ProcessHandle is independent. Do not share a single handle across
/// threads without external synchronization.

#include <cstddef>
#include <cstdint>

namespace uz {

/// Opaque handle bundle for a child process and its I/O pipes.
///
/// All fields are Win32 HANDLEs cast to `void*`.
/// Zero-initialized = invalid/closed.
struct ProcessHandle {
    void* hProcess = nullptr;     ///< Win32 process handle (HANDLE).
    void* hStdoutRead = nullptr;  ///< Read end of child stdout pipe.
    void* hStderrRead = nullptr;  ///< Read end of child stderr pipe.
    uint32_t processId = 0;       ///< Win32 process ID (PID).
};

/// Creates a child process with piped stdout and stderr.
///
/// Spawns the executable via CreateProcessW. The child's stdout and stderr
/// are redirected to anonymous pipes readable via ProcessReadStdout/Stderr.
///
/// # Arguments
/// * `exe` — UTF-8 path to the executable. Must not be nullptr.
/// * `args` — Optional UTF-8 command-line arguments (appended after exe).
/// * `workDir` — Optional UTF-8 working directory for the child. nullptr = inherit.
///
/// # Returns
/// - Valid ProcessHandle on success (hProcess != nullptr).
/// - Zero-initialized ProcessHandle on failure (check logs).
///
/// # Errors
/// Logs Platform::Error with GetLastError() on pipe or process creation failure.
ProcessHandle ProcessCreate(const char* exe, const char* args = nullptr, const char* workDir = nullptr);

/// Reads available stdout data from the child process (non-blocking).
///
/// Uses PeekNamedPipe to check availability, then ReadFile.
/// Returns immediately with 0 if no data is available.
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
/// Same semantics as ProcessReadStdout but for the stderr pipe.
///
/// # Returns
/// Number of bytes actually read (0 if nothing available or pipe closed).
size_t ProcessReadStderr(const ProcessHandle& proc, char* buffer, size_t bufferSize);

/// Waits for the child process to exit and returns its exit code.
///
/// # Arguments
/// * `proc` — Handle from ProcessCreate().
/// * `timeoutMs` — Maximum wait time in milliseconds. 0 = wait forever (INFINITE).
///
/// # Returns
/// - Process exit code on normal termination.
/// - -1 if the wait timed out or an error occurred.
int ProcessWait(const ProcessHandle& proc, uint32_t timeoutMs = 0);

/// Checks whether the child process is still running.
///
/// # Returns
/// - `true` if the process has not yet exited.
/// - `false` if exited, or if the handle is invalid.
bool ProcessIsRunning(const ProcessHandle& proc);

/// Forcibly terminates the child process.
///
/// Calls TerminateProcess with exit code 1. ProcessClose() must still
/// be called afterward to release handles.
///
/// # Arguments
/// * `proc` — Handle to terminate. hProcess is NOT nulled (call ProcessClose).
void ProcessTerminate(ProcessHandle& proc);

/// Closes all handles in the ProcessHandle and zeros them.
///
/// Must be called when the process is no longer needed, whether it exited
/// normally, was terminated, or failed to start. Safe to call multiple times.
void ProcessClose(ProcessHandle& proc);

} // namespace uz
