/// UzEngine Stack Walker — Windows implementation.
///
/// Uses CaptureStackBackTrace for frame capture and DbgHelp
/// (SymFromAddr, SymGetLineFromAddr64) for symbol resolution.

#include "stack_walker.h"

#include "log.h"
#include "profiler.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>

#include <cstdio>
#include <cstring>
#include <mutex>

UZ_DECLARE_LOG_CATEGORY(CrashHandler);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal state — DbgHelp initialization and serialization
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Mutex protecting all DbgHelp calls (DbgHelp is not thread-safe).
std::mutex g_dbgHelpMutex;

/// Whether SymInitialize has been called successfully.
bool g_symbolsInitialized = false;

/// Initializes DbgHelp symbol engine on first use.
/// Must be called under g_dbgHelpMutex.
void EnsureSymbolsInitialized() {
    if (g_symbolsInitialized) {
        return;
    }

    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);

    if (SymInitialize(process, nullptr, TRUE)) {
        g_symbolsInitialized = true;
        UZ_LOG(CrashHandler, Debug, "DbgHelp symbols initialized");
    } else {
        // SymInitialize fails with ERROR_INVALID_PARAMETER if another component
        // (e.g. Tracy profiler) already initialized it. Treat this as success —
        // the symbol engine is already usable.
        const DWORD err = GetLastError();
        if (err == ERROR_INVALID_PARAMETER) {
            g_symbolsInitialized = true;
            UZ_LOG(CrashHandler, Debug, "DbgHelp already initialized by another component");
        } else {
            UZ_LOG(CrashHandler, Error,
                   "SymInitialize failed: error=0x{:08X}", err);
        }
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// CaptureRawStackTrace — crash-safe, no DbgHelp
// ─────────────────────────────────────────────────────────────────────────────

int CaptureRawStackTrace(void* frames[], int maxFrames, int skipFrames) {
    if (frames == nullptr || maxFrames <= 0) {
        return 0;
    }

    // CaptureStackBackTrace max is 62 frames per call on some Windows versions.
    // We add 1 to skipFrames to skip this function itself.
    const USHORT captured = CaptureStackBackTrace(
        static_cast<DWORD>(skipFrames + 1),
        static_cast<DWORD>(maxFrames),
        frames,
        nullptr);

    return static_cast<int>(captured);
}

// ─────────────────────────────────────────────────────────────────────────────
// WalkStack — capture + resolve symbols
// ─────────────────────────────────────────────────────────────────────────────

int WalkStack(StackFrame* frames, int maxFrames, int skipFrames) {
    UZ_PROFILE_FUNCTION();

    if (frames == nullptr || maxFrames <= 0) {
        return 0;
    }

    // Capture raw addresses first (skip +1 for this function).
    static constexpr int kMaxCapture = 64;
    void* rawFrames[kMaxCapture];
    const int captureMax = (maxFrames < kMaxCapture) ? maxFrames : kMaxCapture;
    const int captured = CaptureRawStackTrace(rawFrames, captureMax, skipFrames + 1);

    if (captured == 0) {
        return 0;
    }

    // Resolve symbols under lock.
    std::lock_guard<std::mutex> lock(g_dbgHelpMutex);
    EnsureSymbolsInitialized();

    const HANDLE process = GetCurrentProcess();

    // SYMBOL_INFO requires extra space for the name string.
    alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 256];
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;

    IMAGEHLP_LINE64 lineInfo{};
    lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    int resolved = 0;
    for (int i = 0; i < captured && resolved < maxFrames; ++i) {
        StackFrame& frame = frames[resolved];
        frame.address = rawFrames[i];
        frame.functionName[0] = '\0';
        frame.fileName[0] = '\0';
        frame.lineNumber = 0;

        const auto addr = reinterpret_cast<DWORD64>(rawFrames[i]);

        // Resolve function name.
        DWORD64 displacement64 = 0;
        if (g_symbolsInitialized &&
            SymFromAddr(process, addr, &displacement64, symbol)) {
            std::strncpy(frame.functionName, symbol->Name,
                         sizeof(frame.functionName) - 1);
            frame.functionName[sizeof(frame.functionName) - 1] = '\0';
        } else {
            std::snprintf(frame.functionName, sizeof(frame.functionName),
                          "???");
        }

        // Resolve source file and line.
        DWORD displacement32 = 0;
        if (g_symbolsInitialized &&
            SymGetLineFromAddr64(process, addr, &displacement32, &lineInfo)) {
            std::strncpy(frame.fileName, lineInfo.FileName,
                         sizeof(frame.fileName) - 1);
            frame.fileName[sizeof(frame.fileName) - 1] = '\0';
            frame.lineNumber = static_cast<int>(lineInfo.LineNumber);
        }

        ++resolved;
    }

    return resolved;
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
                          "[%2d] %s (%s:%d) [0x%016llX]\n",
                          i, f.functionName, f.fileName, f.lineNumber,
                          reinterpret_cast<unsigned long long>(f.address));
        } else {
            std::snprintf(lineBuf, sizeof(lineBuf),
                          "[%2d] %s [0x%016llX]\n",
                          i, f.functionName,
                          reinterpret_cast<unsigned long long>(f.address));
        }

        result += lineBuf;
    }

    return result;
}

} // namespace uz
