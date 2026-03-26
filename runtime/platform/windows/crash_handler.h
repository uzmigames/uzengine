#pragma once

/// UzEngine Crash Handler — Windows SEH-based crash diagnostics.
///
/// Installs an unhandled exception filter that captures crash context,
/// writes a minidump (.dmp), a human-readable crash log (.txt), and a
/// machine-parseable crash report (.json) into a timestamped directory
/// under `{exe_dir}/crashes/`.
///
/// # Architecture
/// - **Exception filter**: Registered via SetUnhandledExceptionFilter.
/// - **Crash context**: Global atomic/volatile state updated by engine
///   systems each frame (frame number, scene name, RHI operation, GPU info).
/// - **Output directory**: `{exe_dir}/crashes/YYYY-MM-DD_HH-MM-SS/`
/// - **Files written**: `crash.dmp` (minidump), `crash.txt` (readable),
///   `crash.json` (structured).
///
/// # Exception Safety
/// The exception filter avoids heap allocation. All buffers are stack-local
/// or global. String formatting uses snprintf into fixed char arrays.
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
/// uz::CrashHandler_SetLastRhiOp("DrawIndexedInstanced");
/// // ... if a crash occurs, files appear in crashes/ directory ...
/// uz::UninstallCrashHandler();
/// ```

#include <cstdint>

namespace uz {

/// Snapshot of engine state at the time of a crash.
///
/// Populated by the exception filter from global state and the
/// EXCEPTION_POINTERS structure. All strings are fixed-size buffers
/// to avoid heap allocation in crash context.
struct CrashContext {
    uint32_t exceptionCode;     ///< Windows exception code (e.g., 0xC0000005 = access violation).
    void* exceptionAddress;     ///< Instruction pointer where the exception occurred.
    uint64_t frameNumber;       ///< Engine frame number at time of crash (set by renderer).
    char sceneName[256];        ///< Active scene name (set by scene system).
    char lastRhiOp[256];        ///< Last RHI operation before crash (set by RHI layer).
    char gpuAdapter[256];       ///< GPU adapter description (set at RHI init).
    bool gpuDeviceRemoved;      ///< True if DXGI_ERROR_DEVICE_REMOVED was detected before crash.

    /// ACCESS_VIOLATION details (valid when exceptionCode == 0xC0000005).
    uint32_t accessType;        ///< 0=read, 1=write, 8=DEP violation.
    void* accessAddress;        ///< Virtual address that caused the violation.
    bool hasAccessDetails;      ///< True if accessType/accessAddress are valid.

    /// Faulting module name resolved from exceptionAddress.
    char faultModule[256];      ///< e.g. "nvwgf2umx.dll", "d3d12.dll", "uzengine_runtime.dll"
    void* faultModuleBase;      ///< Base address of the faulting module.
    uint64_t faultModuleOffset; ///< Offset within the faulting module.

    /// Stack trace captured at exception point.
    static constexpr int kMaxStackFrames = 64;
    void* stackFrames[kMaxStackFrames]; ///< Raw instruction pointer addresses.
    int stackFrameCount;                ///< Number of valid entries in stackFrames.
};

/// Installs the crash handler.
///
/// In standalone mode (default): registers a top-level exception filter via
/// SetUnhandledExceptionFilter.
///
/// In library mode (when SetLibraryMode was called): registers a Vectored
/// Exception Handler (VEH) that writes crash reports but returns
/// EXCEPTION_CONTINUE_SEARCH. This allows inner __try/__except blocks
/// (e.g., in N-API SEH wrappers) to catch the exception gracefully instead
/// of terminating the host process.
///
/// Call once at engine startup, after LogInit() has been called.
///
/// # Behavior
/// - Replaces any previously installed unhandled exception filter (standalone).
/// - Logs installation at Info level to CrashHandler category.
/// - Safe to call multiple times (idempotent — reinstalls the same filter).
///
/// # Thread Safety
/// Thread-safe. The filter itself is process-global (Windows semantics).
void InstallCrashHandler();

/// Enables library mode for the crash handler.
///
/// When enabled, the crash handler uses a Vectored Exception Handler instead
/// of SetUnhandledExceptionFilter. This allows the exception to propagate to
/// inner __try/__except blocks after the crash report is written.
///
/// Must be called BEFORE InstallCrashHandler().
void CrashHandler_SetLibraryMode(bool enabled);

/// Removes the crash handler and restores the previous exception filter.
///
/// Call at engine shutdown (before LogShutdown).
///
/// # Behavior
/// - Restores the filter that was active before InstallCrashHandler().
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
/// exception has frozen all other threads.
void CrashHandler_SetSceneName(const char* name);

/// Updates the last RHI operation description in crash context.
///
/// Call before each significant RHI/GPU operation so that crash reports
/// include context about what the GPU was doing.
///
/// # Arguments
/// * `op` - Operation description (e.g., "DrawIndexedInstanced", "Present").
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
/// * `adapter` - GPU adapter description string (e.g., "NVIDIA GeForce RTX 4090").
///               Copied into an internal buffer (max 255 chars).
///               Passing nullptr clears the adapter string.
///
/// # Thread Safety
/// Thread-safe for single-writer (RHI init).
void CrashHandler_SetGpuAdapter(const char* adapter);

/// Sets the GPU device-removed flag in crash context.
///
/// Call from RHI when DXGI_ERROR_DEVICE_REMOVED or equivalent is detected.
/// This flag is included in crash reports to help diagnose GPU hangs/TDR.
///
/// # Arguments
/// * `removed` - True if device removal was detected.
///
/// # Thread Safety
/// Thread-safe (uses atomic store).
void CrashHandler_SetGpuDeviceRemoved(bool removed);

/// Overrides the base directory where crash reports are written.
///
/// By default, crash reports go to `{exe_dir}/crashes/`. When running as a
/// .node addon inside Electron, the exe dir is VSCode's install path, which
/// is not writable or useful. Call this to redirect crashes to the engine's
/// own directory (e.g., the `native/` folder next to `uzengine.node`).
///
/// # Arguments
/// * `dir` - Absolute path to the crash output directory. A `crashes/`
///           subdirectory will be created inside it. Pass nullptr to reset
///           to the default (exe dir).
///
/// # Safety
/// Must be called before any crash occurs. NOT thread-safe.
void CrashHandler_SetCrashDir(const char* dir);

} // namespace uz
