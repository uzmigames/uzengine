/// UzEngine Stack Walker — Linux implementation.
///
/// Uses backtrace() for frame capture, dladdr() + __cxa_demangle() for
/// best-effort C++ symbol resolution. File/line info is not available
/// without addr2line; only function name + address are provided.

#ifdef __linux__

#include "stack_walker.h"

#include "../../engine/logging/log.h"
#include "../../engine/profiling/profiler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>

UZ_DECLARE_LOG_CATEGORY(CrashHandler);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CaptureRawStackTrace — async-signal-safe, no symbol resolution
// ─────────────────────────────────────────────────────────────────────────────

int CaptureRawStackTrace(void* frames[], int maxFrames, int skipFrames) {
    if (frames == nullptr || maxFrames <= 0) {
        return 0;
    }

    // Capture more than needed so we can skip the top frames.
    const int kExtraCapacity = 8;
    const int captureCount = maxFrames + skipFrames + 1 + kExtraCapacity;

    // Use a VLA-equivalent via alloca to stay heap-free in signal context.
    // Cap at 128 to bound stack usage.
    const int kAbsMax = 128;
    const int actualCapture = (captureCount < kAbsMax) ? captureCount : kAbsMax;

    void* rawBuf[128];
    const int total = backtrace(rawBuf, actualCapture);

    // skipFrames + 1 to skip this function itself.
    const int skip = skipFrames + 1;
    if (skip >= total) {
        return 0;
    }

    const int available = total - skip;
    const int copyCount = (available < maxFrames) ? available : maxFrames;
    for (int i = 0; i < copyCount; ++i) {
        frames[i] = rawBuf[skip + i];
    }

    return copyCount;
}

// ─────────────────────────────────────────────────────────────────────────────
// WalkStack — capture + resolve symbols via dladdr + __cxa_demangle
// ─────────────────────────────────────────────────────────────────────────────

int WalkStack(StackFrame* frames, int maxFrames, int skipFrames) {
    UZ_PROFILE_FUNCTION();

    if (frames == nullptr || maxFrames <= 0) {
        return 0;
    }

    // Capture raw addresses (skip +1 for this function itself).
    static constexpr int kMaxCapture = 64;
    void* rawFrames[kMaxCapture];
    const int captureMax = (maxFrames < kMaxCapture) ? maxFrames : kMaxCapture;
    const int captured = CaptureRawStackTrace(rawFrames, captureMax, skipFrames + 1);

    if (captured == 0) {
        return 0;
    }

    for (int i = 0; i < captured; ++i) {
        StackFrame& frame = frames[i];
        frame.address = rawFrames[i];
        frame.functionName[0] = '\0';
        frame.fileName[0] = '\0';
        frame.lineNumber = 0;

        // Attempt symbol resolution via dladdr.
        Dl_info info{};
        if (dladdr(rawFrames[i], &info) != 0 && info.dli_sname != nullptr) {
            // Attempt C++ demangling.
            int status = 0;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);

            if (status == 0 && demangled != nullptr) {
                std::strncpy(frame.functionName, demangled, sizeof(frame.functionName) - 1);
                frame.functionName[sizeof(frame.functionName) - 1] = '\0';
                // __cxa_demangle allocates with malloc; caller must free.
                std::free(demangled);
            } else {
                // Fall back to raw symbol name (C function or demangling failed).
                std::strncpy(frame.functionName, info.dli_sname, sizeof(frame.functionName) - 1);
                frame.functionName[sizeof(frame.functionName) - 1] = '\0';
                if (demangled != nullptr) {
                    std::free(demangled);
                }
            }
        } else {
            // dladdr failed — no symbol info at this address.
            std::snprintf(frame.functionName, sizeof(frame.functionName), "???");
        }
        // File/line not available without addr2line; left as "" / 0.
    }

    return captured;
}

// ─────────────────────────────────────────────────────────────────────────────
// FormatStackTrace — human-readable output
// ─────────────────────────────────────────────────────────────────────────────

std::string FormatStackTrace(const StackFrame* frames, int count) {
    if (frames == nullptr || count <= 0) {
        return {};
    }

    std::string result;
    // Reserve ~120 chars per frame to reduce reallocations.
    result.reserve(static_cast<size_t>(count) * 120);

    char lineBuf[512];
    for (int i = 0; i < count; ++i) {
        const StackFrame& f = frames[i];

        if (f.fileName[0] != '\0' && f.lineNumber > 0) {
            std::snprintf(lineBuf, sizeof(lineBuf),
                          "#%-2d 0x%016llX %s (%s:%d)\n",
                          i,
                          reinterpret_cast<unsigned long long>(f.address),
                          f.functionName,
                          f.fileName,
                          f.lineNumber);
        } else {
            std::snprintf(lineBuf, sizeof(lineBuf),
                          "#%-2d 0x%016llX %s\n",
                          i,
                          reinterpret_cast<unsigned long long>(f.address),
                          f.functionName);
        }

        result += lineBuf;
    }

    return result;
}

} // namespace uz

#endif // __linux__
