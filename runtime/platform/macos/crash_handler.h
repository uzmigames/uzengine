#pragma once

/// UzEngine Crash Handler — macOS signal-based crash diagnostics.
///
/// Installs signal handlers that capture crash context, write a human-readable
/// crash log (.txt), and a machine-parseable crash report (.json) into a
/// timestamped directory under `{exe_dir}/crashes/`.
///
/// # Architecture
/// - **Signal handler**: Registered via sigaction for SIGSEGV, SIGABRT,
///   SIGFPE, SIGBUS, and SIGILL.
/// - **Crash context**: Global atomic/volatile state updated by engine
///   systems each frame (frame number, scene name, RHI operation, GPU info).
/// - **Output directory**: `{exe_dir}/crashes/YYYY-MM-DD_HH-MM-SS/`
/// - **Files written**: `crash.txt` (readable), `crash.json` (structured).
///   No minidump on macOS.
///
/// # Async-Signal Safety
/// The signal handler itself avoids malloc, UZ_LOG, and any non-async-signal-safe
/// function. All output is written using write() to file descriptors. Symbol
/// resolution via backtrace_symbols is attempted as best-effort.
///
/// # Integration Points
/// - Call InstallCrashHandler() once at engine startup (after LogInit).
/// - Call UninstallCrashHandler() at engine shutdown.
/// - RHI: call CrashHandler_SetLastRhiOp() before each GPU submission.
/// - Renderer: call CrashHandler_SetFrameNumber() each frame.
/// - Scene: call CrashHandler_SetSceneName() on scene load.
///
/// # Example
/// ```cpp
/// uz::InstallCrashHandler();
/// // ... engine runs ...
/// uz::CrashHandler_SetFrameNumber(frameCount);
/// uz::CrashHandler_SetLastRhiOp("MTLCommandBuffer::commit");
/// // ... if a crash occurs, files appear in crashes/ directory ...
/// uz::UninstallCrashHandler();
/// ```

#ifdef __APPLE__

#include <cstdint>

namespace uz {

/// Snapshot of engine state at the time of a crash.
///
/// Populated by the signal handler from global state and the siginfo_t
/// structure. All strings are fixed-size buffers to avoid heap allocation
/// in crash context.
struct CrashContext {
    uint32_t exceptionCode;     ///< Signal number (e.g., SIGSEGV=11, SIGABRT=6).
    void* exceptionAddress;     ///< Instruction pointer where the signal was raised (si_addr).
    uint64_t frameNumber;       ///< Engine frame number at time of crash (set by renderer).
    char sceneName[256];        ///< Active scene name (set by scene system).
    char lastRhiOp[256];        ///< Last RHI operation before crash (set by RHI layer).
    char gpuAdapter[256];       ///< GPU adapter description (set at RHI init).
    bool gpuDeviceRemoved;      ///< True if a device-removed error was detected before crash.

    /// Stack trace captured at signal point.
    static constexpr int kMaxStackFrames = 64;
    void* stackFrames[kMaxStackFrames]; ///< Raw instruction pointer addresses.
    int stackFrameCount;                ///< Number of valid entries in stackFrames.
};

/// Installs signal handlers for SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL.
///
/// Uses sigaction with SA_SIGINFO | SA_RESETHAND so that re-entrant crashes
/// invoke the default handler rather than looping. Call once at engine startup,
/// after LogInit() has been called.
///
/// # Behavior
/// - Registers a crash-capturing signal handler for all five signals.
/// - Logs installation at Info level to CrashHandler category.
/// - Safe to call multiple times (idempotent — reinstalls the same handlers).
///
/// # Thread Safety
/// Thread-safe. Signal handlers are process-global (POSIX semantics).
void InstallCrashHandler();

/// Removes the crash signal handlers and restores SIG_DFL for all signals.
///
/// Call at engine shutdown (before LogShutdown).
///
/// # Behavior
/// - Restores SIG_DFL for SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL.
/// - No-op if InstallCrashHandler() was never called.
void UninstallCrashHandler();

/// Updates the current frame number in crash context.
///
/// Call once per frame from the renderer's main loop.
///
/// # Thread Safety
/// Thread-safe (uses atomic store).
void CrashHandler_SetFrameNumber(uint64_t frame);

/// Updates the current scene name in crash context.
///
/// Call when a scene is loaded or changed.
///
/// # Arguments
/// * `name` - Scene name string. Copied into an internal buffer (max 255 chars).
///            Passing nullptr clears the scene name.
///
/// # Thread Safety
/// Thread-safe for single-writer (scene system). Concurrent reads from the
/// crash handler are safe because the crash handler only reads after the
/// signal has been delivered to the crashing thread.
void CrashHandler_SetSceneName(const char* name);

/// Updates the last RHI operation description in crash context.
///
/// Call before each significant RHI/GPU operation so that crash reports
/// include context about what the GPU was doing.
///
/// # Arguments
/// * `op` - Operation description (e.g., "MTLCommandBuffer::commit", "Present").
///          Copied into an internal buffer (max 255 chars).
///          Passing nullptr clears the operation string.
///
/// # Thread Safety
/// Thread-safe for single-writer (RHI thread).
void CrashHandler_SetLastRhiOp(const char* op);

/// Sets the GPU adapter name in crash context.
///
/// Call once at RHI initialization after enumerating the adapter.
///
/// # Arguments
/// * `adapter` - GPU adapter description string (e.g., "Apple M3 Pro").
///               Copied into an internal buffer (max 255 chars).
///               Passing nullptr clears the adapter string.
///
/// # Thread Safety
/// Thread-safe for single-writer (RHI init).
void CrashHandler_SetGpuAdapter(const char* adapter);

/// Sets the GPU device-removed flag in crash context.
///
/// Call from RHI when a device-lost or device-removed error is detected.
/// This flag is included in crash reports to help diagnose GPU hangs.
///
/// # Arguments
/// * `removed` - True if device removal was detected.
///
/// # Thread Safety
/// Thread-safe (uses atomic store).
void CrashHandler_SetGpuDeviceRemoved(bool removed);

} // namespace uz

#endif // __APPLE__
