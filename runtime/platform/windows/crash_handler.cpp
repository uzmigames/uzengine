/// UzEngine Crash Handler — Windows SEH implementation.
///
/// Installs an unhandled exception filter that writes minidump, text, and
/// JSON crash reports into `{exe_dir}/crashes/{timestamp}/`.

#include "crash_handler.h"

#include "log.h"
#include "paths.h"
#include "profiler.h"
#include "stack_walker.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>

// ─────────────────────────────────────────────────────────────────────────────
// Log category
// ─────────────────────────────────────────────────────────────────────────────

UZ_DEFINE_LOG_CATEGORY(CrashHandler, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Global crash context — updated by engine systems, read by exception filter
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Atomic frame counter updated by the renderer each frame.
std::atomic<uint64_t> g_frameNumber{0};

/// Atomic flag for GPU device removal.
std::atomic<bool> g_gpuDeviceRemoved{false};

/// Scene name buffer. Written by scene system, read in crash context.
/// Not atomic — single writer assumed, crash handler reads after exception.
char g_sceneName[256] = {};

/// Last RHI operation buffer. Same thread-safety model as g_sceneName.
char g_lastRhiOp[256] = {};

/// GPU adapter name buffer. Written once at RHI init.
char g_gpuAdapter[256] = {};

/// Previous exception filter, restored on uninstall.
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

/// Whether the crash handler is currently installed.
bool g_installed = false;

// ─────────────────────────────────────────────────────────────────────────────
// Crash directory and file writing helpers — exception-safe (no heap allocs)
// ─────────────────────────────────────────────────────────────────────────────

/// Formats current local time as "YYYY-MM-DD_HH-MM-SS" into the provided buffer.
/// Returns false if time retrieval fails.
bool FormatTimestamp(char* buf, size_t bufSize) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    const int written = std::snprintf(buf, bufSize,
        "%04d-%02d-%02d_%02d-%02d-%02d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    return written > 0 && static_cast<size_t>(written) < bufSize;
}

/// Returns a human-readable name for common Windows exception codes.
const char* ExceptionCodeToString(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:       return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:  return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:             return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:  return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:     return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_OVERFLOW:           return "EXCEPTION_FLT_OVERFLOW";
    case EXCEPTION_FLT_UNDERFLOW:          return "EXCEPTION_FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:    return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:     return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:           return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_STACK_OVERFLOW:         return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_PRIV_INSTRUCTION:       return "EXCEPTION_PRIV_INSTRUCTION";
    default:                               return "UNKNOWN_EXCEPTION";
    }
}

/// Writes a MiniDump file at the given path using the exception pointers.
/// Returns true on success.
bool WriteMiniDump(const char* path, EXCEPTION_POINTERS* exceptionInfo) {
    const HANDLE file = CreateFileA(
        path, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION mdei{};
    mdei.ThreadId = GetCurrentThreadId();
    mdei.ExceptionPointers = exceptionInfo;
    mdei.ClientPointers = FALSE;

    // MiniDumpWithDataSegs includes global variable state.
    // MiniDumpWithHandleData includes OS handle info for debugging.
    const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithDataSegs | MiniDumpWithHandleData);

    const BOOL success = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        file,
        dumpType,
        &mdei,
        nullptr,
        nullptr);

    CloseHandle(file);
    return success != FALSE;
}

/// Writes the human-readable crash report (.txt) using only stack buffers.
void WriteTextReport(const char* path, const CrashContext& ctx) {
    const HANDLE file = CreateFileA(
        path, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    char buf[4096];
    DWORD written = 0;
    int len = 0;

    // Header
    len = std::snprintf(buf, sizeof(buf),
        "=== UzEngine Crash Report ===\n\n");
    WriteFile(file, buf, static_cast<DWORD>(len), &written, nullptr);

    // Exception info
    len = std::snprintf(buf, sizeof(buf),
        "Exception:  0x%08X (%s)\n"
        "Address:    0x%016llX\n"
        "Frame:      %llu\n"
        "Scene:      %s\n"
        "Last RHI:   %s\n"
        "GPU:        %s\n"
        "Device Removed: %s\n\n",
        ctx.exceptionCode,
        ExceptionCodeToString(ctx.exceptionCode),
        reinterpret_cast<unsigned long long>(ctx.exceptionAddress),
        static_cast<unsigned long long>(ctx.frameNumber),
        ctx.sceneName[0] ? ctx.sceneName : "(none)",
        ctx.lastRhiOp[0] ? ctx.lastRhiOp : "(none)",
        ctx.gpuAdapter[0] ? ctx.gpuAdapter : "(unknown)",
        ctx.gpuDeviceRemoved ? "YES" : "no");
    WriteFile(file, buf, static_cast<DWORD>(len), &written, nullptr);

    // Stack trace — resolve symbols (we're already crashing, worth the attempt).
    len = std::snprintf(buf, sizeof(buf), "Stack Trace (%d frames):\n", ctx.stackFrameCount);
    WriteFile(file, buf, static_cast<DWORD>(len), &written, nullptr);

    // Resolve and write each frame individually to limit stack usage.
    StackFrame frame{};
    for (int i = 0; i < ctx.stackFrameCount; ++i) {
        // Use WalkStack's internal resolution on a single frame at a time
        // would be ideal, but we have raw pointers. Do manual resolution here.
        frame.address = ctx.stackFrames[i];
        frame.functionName[0] = '\0';
        frame.fileName[0] = '\0';
        frame.lineNumber = 0;

        // Attempt symbol resolution (DbgHelp may be in bad state post-crash).
        const auto addr = reinterpret_cast<DWORD64>(ctx.stackFrames[i]);

        alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 256];
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;

        DWORD64 disp64 = 0;
        if (SymFromAddr(GetCurrentProcess(), addr, &disp64, sym)) {
            std::strncpy(frame.functionName, sym->Name, sizeof(frame.functionName) - 1);
            frame.functionName[sizeof(frame.functionName) - 1] = '\0';
        } else {
            std::snprintf(frame.functionName, sizeof(frame.functionName), "???");
        }

        IMAGEHLP_LINE64 lineInfo{};
        lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD disp32 = 0;
        if (SymGetLineFromAddr64(GetCurrentProcess(), addr, &disp32, &lineInfo)) {
            std::strncpy(frame.fileName, lineInfo.FileName, sizeof(frame.fileName) - 1);
            frame.fileName[sizeof(frame.fileName) - 1] = '\0';
            frame.lineNumber = static_cast<int>(lineInfo.LineNumber);
        }

        if (frame.fileName[0] != '\0' && frame.lineNumber > 0) {
            len = std::snprintf(buf, sizeof(buf),
                "  [%2d] %s (%s:%d) [0x%016llX]\n",
                i, frame.functionName, frame.fileName, frame.lineNumber,
                reinterpret_cast<unsigned long long>(frame.address));
        } else {
            len = std::snprintf(buf, sizeof(buf),
                "  [%2d] %s [0x%016llX]\n",
                i, frame.functionName,
                reinterpret_cast<unsigned long long>(frame.address));
        }
        WriteFile(file, buf, static_cast<DWORD>(len), &written, nullptr);
    }

    CloseHandle(file);
}

/// Writes the machine-parseable crash report (.json) using only stack buffers.
/// Uses manual snprintf — no JSON library to avoid heap allocation.
void WriteJsonReport(const char* path, const CrashContext& ctx) {
    const HANDLE file = CreateFileA(
        path, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    char buf[4096];
    DWORD written = 0;
    int len = 0;

    // Timestamp
    char timestamp[64];
    FormatTimestamp(timestamp, sizeof(timestamp));

    len = std::snprintf(buf, sizeof(buf),
        "{\n"
        "  \"engine\": \"UzEngine\",\n"
        "  \"timestamp\": \"%s\",\n"
        "  \"exception\": {\n"
        "    \"code\": \"0x%08X\",\n"
        "    \"name\": \"%s\",\n"
        "    \"address\": \"0x%016llX\"\n"
        "  },\n"
        "  \"context\": {\n"
        "    \"frameNumber\": %llu,\n"
        "    \"sceneName\": \"%s\",\n"
        "    \"lastRhiOp\": \"%s\",\n"
        "    \"gpuAdapter\": \"%s\",\n"
        "    \"gpuDeviceRemoved\": %s\n"
        "  },\n"
        "  \"stackTrace\": [\n",
        timestamp,
        ctx.exceptionCode,
        ExceptionCodeToString(ctx.exceptionCode),
        reinterpret_cast<unsigned long long>(ctx.exceptionAddress),
        static_cast<unsigned long long>(ctx.frameNumber),
        ctx.sceneName[0] ? ctx.sceneName : "",
        ctx.lastRhiOp[0] ? ctx.lastRhiOp : "",
        ctx.gpuAdapter[0] ? ctx.gpuAdapter : "",
        ctx.gpuDeviceRemoved ? "true" : "false");
    WriteFile(file, buf, static_cast<DWORD>(len), &written, nullptr);

    // Stack frames as JSON array entries.
    for (int i = 0; i < ctx.stackFrameCount; ++i) {
        const char* comma = (i < ctx.stackFrameCount - 1) ? "," : "";
        len = std::snprintf(buf, sizeof(buf),
            "    {\"index\": %d, \"address\": \"0x%016llX\"}%s\n",
            i,
            reinterpret_cast<unsigned long long>(ctx.stackFrames[i]),
            comma);
        WriteFile(file, buf, static_cast<DWORD>(len), &written, nullptr);
    }

    len = std::snprintf(buf, sizeof(buf), "  ]\n}\n");
    WriteFile(file, buf, static_cast<DWORD>(len), &written, nullptr);

    CloseHandle(file);
}

/// Safely copies a source string into a fixed-size destination buffer.
/// If src is nullptr, clears the destination.
void SafeCopyString(char* dst, size_t dstSize, const char* src) {
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
// Unhandled exception filter — the core crash handler
// ─────────────────────────────────────────────────────────────────────────────

/// The SEH exception filter installed via SetUnhandledExceptionFilter.
///
/// This function runs in exception context. It avoids heap allocation and
/// uses only stack buffers + Win32 file APIs for output.
LONG WINAPI UzExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    // 1. Populate CrashContext from exception info and global state.
    CrashContext ctx{};
    ctx.exceptionCode = exceptionInfo->ExceptionRecord->ExceptionCode;
    ctx.exceptionAddress = exceptionInfo->ExceptionRecord->ExceptionAddress;
    ctx.frameNumber = g_frameNumber.load(std::memory_order_relaxed);
    ctx.gpuDeviceRemoved = g_gpuDeviceRemoved.load(std::memory_order_relaxed);

    // Copy string state (single-writer, safe to read post-exception).
    std::memcpy(ctx.sceneName, g_sceneName, sizeof(ctx.sceneName));
    std::memcpy(ctx.lastRhiOp, g_lastRhiOp, sizeof(ctx.lastRhiOp));
    std::memcpy(ctx.gpuAdapter, g_gpuAdapter, sizeof(ctx.gpuAdapter));

    // 2. Capture stack trace (crash-safe, no DbgHelp).
    ctx.stackFrameCount = CaptureRawStackTrace(
        ctx.stackFrames, CrashContext::kMaxStackFrames, 0);

    // 3. Build crash directory path: {exe_dir}/crashes/{timestamp}/
    char exeDirBuf[MAX_PATH];
    {
        // Get exe directory without std::filesystem (crash-safe).
        const DWORD exeLen = GetModuleFileNameA(nullptr, exeDirBuf, MAX_PATH);
        if (exeLen == 0 || exeLen >= MAX_PATH) {
            // Fallback to current directory.
            exeDirBuf[0] = '.';
            exeDirBuf[1] = '\0';
        } else {
            // Strip filename to get directory.
            char* lastSlash = nullptr;
            for (char* p = exeDirBuf; *p != '\0'; ++p) {
                if (*p == '\\' || *p == '/') {
                    lastSlash = p;
                }
            }
            if (lastSlash != nullptr) {
                *lastSlash = '\0';
            }
        }
    }

    char timestamp[64];
    FormatTimestamp(timestamp, sizeof(timestamp));

    char crashDir[MAX_PATH];
    std::snprintf(crashDir, sizeof(crashDir), "%s\\crashes\\%s",
                  exeDirBuf, timestamp);

    // Create directory hierarchy: {exe}/crashes/ then {exe}/crashes/{timestamp}/
    char crashesParent[MAX_PATH];
    std::snprintf(crashesParent, sizeof(crashesParent), "%s\\crashes", exeDirBuf);
    CreateDirectoryA(crashesParent, nullptr);
    CreateDirectoryA(crashDir, nullptr);

    // 4. Initialize DbgHelp for symbol resolution in crash reports.
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);

    // 5. Write minidump.
    char dumpPath[MAX_PATH];
    std::snprintf(dumpPath, sizeof(dumpPath), "%s\\crash.dmp", crashDir);
    WriteMiniDump(dumpPath, exceptionInfo);

    // 6. Write human-readable crash log.
    char txtPath[MAX_PATH];
    std::snprintf(txtPath, sizeof(txtPath), "%s\\crash.txt", crashDir);
    WriteTextReport(txtPath, ctx);

    // 7. Write machine-parseable JSON crash log.
    char jsonPath[MAX_PATH];
    std::snprintf(jsonPath, sizeof(jsonPath), "%s\\crash.json", crashDir);
    WriteJsonReport(jsonPath, ctx);

    return EXCEPTION_EXECUTE_HANDLER;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void InstallCrashHandler() {
    UZ_PROFILE_FUNCTION();

    g_previousFilter = SetUnhandledExceptionFilter(UzExceptionFilter);
    g_installed = true;

    UZ_LOG(CrashHandler, Info, "Crash handler installed");
}

void UninstallCrashHandler() {
    if (!g_installed) {
        return;
    }

    SetUnhandledExceptionFilter(g_previousFilter);
    g_previousFilter = nullptr;
    g_installed = false;

    UZ_LOG(CrashHandler, Info, "Crash handler uninstalled");
}

void CrashHandler_SetFrameNumber(uint64_t frame) {
    g_frameNumber.store(frame, std::memory_order_relaxed);
}

void CrashHandler_SetSceneName(const char* name) {
    SafeCopyString(g_sceneName, sizeof(g_sceneName), name);
}

void CrashHandler_SetLastRhiOp(const char* op) {
    SafeCopyString(g_lastRhiOp, sizeof(g_lastRhiOp), op);
}

void CrashHandler_SetGpuAdapter(const char* adapter) {
    SafeCopyString(g_gpuAdapter, sizeof(g_gpuAdapter), adapter);
}

void CrashHandler_SetGpuDeviceRemoved(bool removed) {
    g_gpuDeviceRemoved.store(removed, std::memory_order_relaxed);
}

} // namespace uz
