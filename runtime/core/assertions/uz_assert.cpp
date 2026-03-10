#include "uz_assert.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace uz::detail {

/// Platform-specific stack trace printing (Windows / other platforms).
///
/// # Windows
/// Uses CaptureStackBackTrace to capture up to 32 frame addresses.
/// Shows raw pointers (use debugger symbols to resolve names).
///
/// # Other Platforms
/// Currently shows a stub message (stack trace support can be added).
#ifdef _WIN32
static void PrintStackTrace() {
    constexpr int kMaxFrames = 32;
    void* frames[kMaxFrames];
    WORD frameCount = CaptureStackBackTrace(2, kMaxFrames, frames, nullptr);

    std::fprintf(stderr, "  Stack trace (%d frames):\n", frameCount);
    for (WORD i = 0; i < frameCount; ++i) {
        std::fprintf(stderr, "    [%2d] 0x%llx\n",
                     i, reinterpret_cast<unsigned long long>(frames[i]));
    }
}
#else
static void PrintStackTrace() {
    std::fprintf(stderr, "  (stack trace not available on this platform)\n");
}
#endif

/// Implements UZ_ENSURE — logs violation but continues.
///
/// # Output
/// - Expression: the code that failed
/// - Location: file and line number
/// - Message: optional custom message from UZ_ENSURE macro
/// - Stack trace: raw frame pointers for debugging
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
/// - Stack trace: raw frame pointers for crash analysis
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
