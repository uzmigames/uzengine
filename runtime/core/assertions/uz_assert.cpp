#include "uz_assert.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace uz::detail {

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
