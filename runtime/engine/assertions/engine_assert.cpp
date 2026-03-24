/// UzEngine Assertion System Implementation
///
/// Provides symbolized stack trace capture via the platform stack walker
/// (WalkStack + FormatStackTrace), assertion failure reporting to stderr,
/// and debugger break support. Fatal assertions (UZ_CHECK) call std::abort()
/// after logging.

#include "engine_assert.h"

#include <cstdio>
#include <cstdlib>
#include <string>

// Include the platform-specific stack walker header so that
// uz::WalkStack / uz::FormatStackTrace are available.  Each header is
// guarded by its own platform preprocessor check, so exactly one of the
// three branches below will contribute a definition.
#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   define NOMINMAX
#   include <windows.h>
#   include "stack_walker.h"   // platform/windows/stack_walker.h
#elif defined(__linux__)
#   include "stack_walker.h"   // platform/linux/stack_walker.h
#elif defined(__APPLE__)
#   include "stack_walker.h"   // platform/macos/stack_walker.h
#endif

namespace uz::detail {

/// Captures up to `kAssertStackFrames` resolved frames and prints them to
/// stderr.
///
/// # Skip count
/// The skip accounts for the internal call chain on the failure path so that
/// the first printed frame is the caller of UZ_CHECK / UZ_ENSURE:
///
///   PrintStackTrace            [skip]
///   AssertionFailed / FatalAssert  [skip]
///   UZ_CHECK / UZ_ENSURE lambda    [skip] (ENSURE uses a lambda)
///   <user code>                [first visible frame]
///
/// We pass skipFrames=3 to WalkStack so those three internal frames are
/// omitted (WalkStack itself always skips itself as frame 0 internally,
/// so the value here refers to frames above WalkStack).
static constexpr int kAssertStackFrames = 8;
static constexpr int kAssertSkipFrames  = 3;

static void PrintStackTrace() {
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    uz::StackFrame frames[kAssertStackFrames];
    const int count = uz::WalkStack(frames, kAssertStackFrames, kAssertSkipFrames);
    if (count > 0) {
        const std::string trace = uz::FormatStackTrace(frames, count);
        std::fprintf(stderr, "  Stack trace:\n%s", trace.c_str());
    } else {
        std::fprintf(stderr, "  Stack trace: (unavailable — no PDB / no debug symbols)\n");
    }
#else
    std::fprintf(stderr, "  Stack trace: (not supported on this platform)\n");
#endif
}

/// Implements UZ_ENSURE — logs violation but continues.
///
/// # Output
/// - Expression: the code that failed
/// - Location: file and line number
/// - Message: optional custom message from UZ_ENSURE macro
/// - Stack trace: symbolized frames (function name + file:line where available)
///
/// # Behavior
/// - Prints to stderr (not the game log, to guarantee visibility)
/// - Flushes stderr to ensure output appears
/// - Returns false (caller can check: if (!UZ_ENSURE(...)) return;)
bool AssertionFailed(const char* expr, const char* file, int line, const char* msg) {
    std::fprintf(stderr, "\n=== UZ_ENSURE FAILED ===\n");
    std::fprintf(stderr, "  Expression: %s\n", expr);
    std::fprintf(stderr, "  Location:   %s:%d\n", file, line);
    if (msg && msg[0] != '\0') {
        std::fprintf(stderr, "  Message:    %s\n", msg);
    }
    PrintStackTrace();
    std::fprintf(stderr, "  (continuing execution)\n\n");
    std::fflush(stderr);
    return false;
}

/// Implements UZ_CHECK and UZ_VERIFY — fatal assertion failure.
///
/// # Output
/// - Expression: the code that failed
/// - Location: file and line number
/// - Message: optional custom message from UZ_CHECKF macro
/// - Stack trace: symbolized frames (function name + file:line where available)
///
/// # Behavior
/// - Prints to stderr (bypasses logging system, guaranteed output)
/// - If debugger attached (Windows): calls __debugbreak() for immediate inspection
/// - Calls std::abort() — TERMINATES PROCESS
///
/// # Never Returns
/// This function is [[noreturn]] — execution never continues after calling.
[[noreturn]] void FatalAssert(const char* expr, const char* file, int line, const char* msg) {
    std::fprintf(stderr, "\n=== UZ_CHECK FAILED (FATAL) ===\n");
    std::fprintf(stderr, "  Expression: %s\n", expr);
    std::fprintf(stderr, "  Location:   %s:%d\n", file, line);
    if (msg && msg[0] != '\0') {
        std::fprintf(stderr, "  Message:    %s\n", msg);
    }
    PrintStackTrace();
    std::fprintf(stderr, "\n");
    std::fflush(stderr);

#ifdef _WIN32
    if (IsDebuggerPresent()) {
        __debugbreak();
    }
#endif

    std::abort();
}

} // namespace uz::detail
