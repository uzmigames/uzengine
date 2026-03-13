#pragma once

/// UzEngine Stack Walker — Windows DbgHelp-based stack trace capture.
///
/// Captures and symbolizes stack traces using CaptureStackBackTrace and
/// the DbgHelp symbol resolution API (SymFromAddr, SymGetLineFromAddr64).
///
/// # Usage
/// - Call WalkStack() from normal code to get a resolved stack trace.
/// - Call FormatStackTrace() to produce a human-readable multi-line string.
/// - CaptureRawStackTrace() is a thin wrapper over CaptureStackBackTrace
///   for use in crash/exception context where symbol resolution is unsafe.
///
/// # Thread Safety
/// DbgHelp is NOT thread-safe. WalkStack() uses an internal mutex to
/// serialize symbol resolution. CaptureRawStackTrace() is lock-free.
///
/// # Limitations
/// Symbol resolution requires PDB files next to the executable. Without
/// PDBs, only raw addresses are available.

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
    int lineNumber;           ///< Source line number, or 0 if unavailable.
};

/// Captures and resolves the current thread's stack trace.
///
/// Internally calls CaptureStackBackTrace, then resolves each frame via
/// SymFromAddr/SymGetLineFromAddr64. DbgHelp initialization is performed
/// once on first call (thread-safe via internal mutex).
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
/// Thread-safe. Uses internal mutex for DbgHelp serialization.
int WalkStack(StackFrame* frames, int maxFrames, int skipFrames = 1);

/// Formats resolved stack frames as a human-readable multi-line string.
///
/// Each line has the format:
/// ```
/// [N] FunctionName (file.cpp:123) [0x00007FF123456789]
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
/// This is safe to call from exception/crash context because it uses
/// only CaptureStackBackTrace (no heap allocation, no DbgHelp calls).
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
