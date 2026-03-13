#pragma once

/// UzEngine Stack Walker — macOS backtrace-based stack trace capture.
///
/// Captures and symbolizes stack traces using backtrace() from <execinfo.h>
/// and __cxa_demangle() from <cxxabi.h> for C++ symbol demangling.
/// dladdr() is used for best-effort symbol name resolution.
///
/// # Usage
/// - Call WalkStack() from normal code to get a resolved stack trace.
/// - Call FormatStackTrace() to produce a human-readable multi-line string.
/// - CaptureRawStackTrace() is a thin wrapper over backtrace() for use in
///   signal/crash context where full symbol resolution is unsafe.
///
/// # Thread Safety
/// WalkStack() is thread-safe. __cxa_demangle allocates memory (malloc),
/// so it must not be called from async-signal-safe context.
/// CaptureRawStackTrace() is async-signal-safe.
///
/// # Limitations
/// File and line number information is not available without atos or addr2line.
/// WalkStack() provides function names and addresses via dladdr() only.

#ifdef __APPLE__

#include <cstdint>
#include <string>

namespace uz {

/// A single resolved stack frame with symbol and source information.
///
/// All strings are stored in fixed-size buffers to avoid heap allocation,
/// making this safe to use in crash context (if pre-allocated on the stack).
struct StackFrame {
    void* address;            ///< Instruction pointer address for this frame.
    char functionName[256];   ///< Demangled function name, or "???" if unresolved.
    char fileName[256];       ///< Source file path, or "" if unavailable.
    int lineNumber;           ///< Source line number, or 0 if unavailable (requires atos).
};

/// Captures and resolves the current thread's stack trace.
///
/// Internally calls backtrace() to capture raw addresses, then resolves each
/// frame via dladdr() and __cxa_demangle(). Symbol resolution requires the
/// binary to be compiled with -g or with exported symbols (-export_dynamic).
///
/// # Arguments
/// * `frames` - Output buffer for resolved stack frames.
/// * `maxFrames` - Capacity of the frames buffer (max 64 recommended).
/// * `skipFrames` - Number of top frames to skip (default 1 skips WalkStack itself).
///
/// # Returns
/// Number of frames actually resolved (0 if capture fails).
///
/// # Thread Safety
/// Thread-safe. __cxa_demangle uses malloc internally; do not call from
/// async-signal-safe context (use CaptureRawStackTrace instead).
int WalkStack(StackFrame* frames, int maxFrames, int skipFrames = 1);

/// Formats resolved stack frames as a human-readable multi-line string.
///
/// Each line has the format:
/// ```
/// #N 0x{addr} {function} ({file}:{line})
/// ```
/// If source info is unavailable, the file/line part is omitted.
///
/// # Arguments
/// * `frames` - Array of resolved stack frames from WalkStack().
/// * `count` - Number of frames in the array.
///
/// # Returns
/// Formatted string. Empty string if count is 0 or frames is nullptr.
std::string FormatStackTrace(const StackFrame* frames, int count);

/// Captures raw instruction pointers without symbol resolution.
///
/// This is async-signal-safe and suitable for use inside signal handlers
/// or crash handlers. Uses only backtrace() (no malloc, no dladdr).
///
/// # Arguments
/// * `frames` - Output buffer for raw instruction pointers.
/// * `maxFrames` - Capacity of the buffer.
/// * `skipFrames` - Number of top frames to skip (default 1).
///
/// # Returns
/// Number of frames captured.
int CaptureRawStackTrace(void* frames[], int maxFrames, int skipFrames = 1);

} // namespace uz

#endif // __APPLE__
