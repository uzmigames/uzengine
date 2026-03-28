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

/// Override crash output base directory. When non-empty, crash reports go to
/// `{g_crashBaseDir}/crashes/{timestamp}/` instead of `{exe_dir}/crashes/`.
char g_crashBaseDir[MAX_PATH] = {};

/// Previous exception filter, restored on uninstall.
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

/// Whether the crash handler is currently installed.
bool g_installed = false;

/// Whether library mode is enabled (VEH instead of UEF).
bool g_libraryMode = false;

/// VEH handle for library mode.
void* g_vehHandle = nullptr;

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
        "Device Removed: %s\n",
        ctx.exceptionCode,
        ExceptionCodeToString(ctx.exceptionCode),
        reinterpret_cast<unsigned long long>(ctx.exceptionAddress),
        static_cast<unsigned long long>(ctx.frameNumber),
        ctx.sceneName[0] ? ctx.sceneName : "(none)",
        ctx.lastRhiOp[0] ? ctx.lastRhiOp : "(none)",
        ctx.gpuAdapter[0] ? ctx.gpuAdapter : "(unknown)",
        ctx.gpuDeviceRemoved ? "YES" : "no");
    WriteFile(file, buf, static_cast<DWORD>(len), &written, nullptr);

    // Faulting module
    if (ctx.faultModule[0]) {
        len = std::snprintf(buf, sizeof(buf),
            "Fault Module: %s (base=0x%016llX, offset=0x%llX)\n",
            ctx.faultModule,
            reinterpret_cast<unsigned long long>(ctx.faultModuleBase),
            static_cast<unsigned long long>(ctx.faultModuleOffset));
        WriteFile(file, buf, static_cast<DWORD>(len), &written, nullptr);
    }

    // ACCESS_VIOLATION details
    if (ctx.hasAccessDetails) {
        const char* accessStr = ctx.accessType == 0 ? "READ" :
                                ctx.accessType == 1 ? "WRITE" :
                                ctx.accessType == 8 ? "DEP" : "UNKNOWN";
        len = std::snprintf(buf, sizeof(buf),
            "Access Violation: %s at address 0x%016llX\n",
            accessStr,
            reinterpret_cast<unsigned long long>(ctx.accessAddress));
        WriteFile(file, buf, static_cast<DWORD>(len), &written, nullptr);
    }

    len = std::snprintf(buf, sizeof(buf), "\n");
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

    // Determine access violation details string
    const char* accessStr = "";
    char accessAddrStr[32] = {};
    if (ctx.hasAccessDetails) {
        accessStr = ctx.accessType == 0 ? "READ" :
                    ctx.accessType == 1 ? "WRITE" :
                    ctx.accessType == 8 ? "DEP" : "UNKNOWN";
        std::snprintf(accessAddrStr, sizeof(accessAddrStr),
                      "0x%016llX",
                      reinterpret_cast<unsigned long long>(ctx.accessAddress));
    }

    len = std::snprintf(buf, sizeof(buf),
        "{\n"
        "  \"engine\": \"UzEngine\",\n"
        "  \"timestamp\": \"%s\",\n"
        "  \"exception\": {\n"
        "    \"code\": \"0x%08X\",\n"
        "    \"name\": \"%s\",\n"
        "    \"address\": \"0x%016llX\",\n"
        "    \"faultModule\": \"%s\",\n"
        "    \"faultModuleOffset\": \"0x%llX\",\n"
        "    \"accessType\": \"%s\",\n"
        "    \"accessAddress\": \"%s\"\n"
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
        ctx.faultModule[0] ? ctx.faultModule : "",
        static_cast<unsigned long long>(ctx.faultModuleOffset),
        accessStr,
        ctx.hasAccessDetails ? accessAddrStr : "",
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
    UZ_LOG(CrashHandler, Error, "!!! UzExceptionFilter ENTERED — exception code 0x{:08X} at {}",
           exceptionInfo->ExceptionRecord->ExceptionCode,
           exceptionInfo->ExceptionRecord->ExceptionAddress);

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

    // Extract ACCESS_VIOLATION details: read/write/DEP + faulting address.
    const auto* rec = exceptionInfo->ExceptionRecord;
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
        ctx.hasAccessDetails = true;
        ctx.accessType = static_cast<uint32_t>(rec->ExceptionInformation[0]);
        ctx.accessAddress = reinterpret_cast<void*>(rec->ExceptionInformation[1]);
    }

    // Resolve faulting module from exception address.
    HMODULE hFaultMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(ctx.exceptionAddress), &hFaultMod)) {
        GetModuleFileNameA(hFaultMod, ctx.faultModule, sizeof(ctx.faultModule));
        ctx.faultModuleBase = reinterpret_cast<void*>(hFaultMod);
        ctx.faultModuleOffset = reinterpret_cast<uintptr_t>(ctx.exceptionAddress) -
                                reinterpret_cast<uintptr_t>(hFaultMod);
        // Strip path — keep only filename.
        char* lastSlash = nullptr;
        for (char* p = ctx.faultModule; *p; ++p) {
            if (*p == '\\' || *p == '/') lastSlash = p;
        }
        if (lastSlash) {
            std::memmove(ctx.faultModule, lastSlash + 1,
                         std::strlen(lastSlash + 1) + 1);
        }
    }

    // 2. Capture stack trace (crash-safe, no DbgHelp).
    ctx.stackFrameCount = CaptureRawStackTrace(
        ctx.stackFrames, CrashContext::kMaxStackFrames, 0);

    // 3. Build crash directory path: {base_dir}/crashes/{timestamp}/
    //    Use override dir if set (for .node addon running inside Electron),
    //    otherwise fall back to exe directory.
    char exeDirBuf[MAX_PATH];
    if (g_crashBaseDir[0] != '\0') {
        std::strncpy(exeDirBuf, g_crashBaseDir, MAX_PATH - 1);
        exeDirBuf[MAX_PATH - 1] = '\0';
    } else {
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

/// Vectored Exception Handler for library mode.
///
/// Writes crash reports (minidump, txt, json) but returns
/// EXCEPTION_CONTINUE_SEARCH so inner __try/__except blocks (like the
/// N-API SEH wrappers) can catch the exception and convert it to a JS error
/// instead of terminating the host process.
///
/// Only fires for fatal exceptions (access violations, stack overflow, etc.).
/// Only handles genuinely fatal exceptions (access violations, stack overflow,
/// illegal instructions, etc.). All other exceptions — including thread-naming
/// (0x406D1388), C++ exceptions, debugger events, and driver-internal exceptions
/// — are ignored immediately. This is critical in library/DLL mode because
/// calling MiniDumpWriteDump on a benign exception suspends all threads and can
/// deadlock the host process (VSCode Extension Host).
LONG WINAPI UzVectoredHandler(EXCEPTION_POINTERS* exceptionInfo) {
    const DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;

    // Only handle exceptions that are genuinely fatal and unrecoverable.
    // Everything else passes through untouched — no minidump, no logging.
    const bool isFatal =
        code == EXCEPTION_ACCESS_VIOLATION      ||  // 0xC0000005
        code == EXCEPTION_STACK_OVERFLOW         ||  // 0xC00000FD
        code == EXCEPTION_ILLEGAL_INSTRUCTION    ||  // 0xC000001D
        code == EXCEPTION_INT_DIVIDE_BY_ZERO     ||  // 0xC0000094
        code == EXCEPTION_FLT_DIVIDE_BY_ZERO     ||  // 0xC000008E
        code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED  ||  // 0xC000008C
        code == EXCEPTION_IN_PAGE_ERROR          ||  // 0xC0000006
        code == EXCEPTION_NONCONTINUABLE_EXCEPTION||  // 0xC0000025
        code == 0xC0000409;                           // STATUS_STACK_BUFFER_OVERRUN (/GS)

    if (!isFatal) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Write crash reports using the same logic as UzExceptionFilter.
    UzExceptionFilter(exceptionInfo);

    // Let the exception propagate to inner __try/__except blocks.
    return EXCEPTION_CONTINUE_SEARCH;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void InstallCrashHandler() {
    UZ_PROFILE_FUNCTION();

    if (g_libraryMode) {
        // VEH: writes crash reports but lets __try/__except catch the exception.
        g_vehHandle = AddVectoredExceptionHandler(1, UzVectoredHandler);
        UZ_LOG(CrashHandler, Info, "Crash handler installed (library mode — VEH)");
    } else {
        // Standalone: traditional unhandled exception filter.
        g_previousFilter = SetUnhandledExceptionFilter(UzExceptionFilter);
        UZ_LOG(CrashHandler, Info, "Crash handler installed (standalone — UEF)");
    }
    g_installed = true;
}

void UninstallCrashHandler() {
    if (!g_installed) {
        return;
    }

    if (g_libraryMode && g_vehHandle) {
        RemoveVectoredExceptionHandler(g_vehHandle);
        g_vehHandle = nullptr;
    } else {
        SetUnhandledExceptionFilter(g_previousFilter);
        g_previousFilter = nullptr;
    }
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

void CrashHandler_SetLibraryMode(bool enabled) {
    g_libraryMode = enabled;
}

void CrashHandler_SetCrashDir(const char* dir) {
    if (!dir || dir[0] == '\0') {
        g_crashBaseDir[0] = '\0';
        return;
    }
    std::strncpy(g_crashBaseDir, dir, MAX_PATH - 1);
    g_crashBaseDir[MAX_PATH - 1] = '\0';
    UZ_LOG(CrashHandler, Info, "Crash output directory set to: {}", g_crashBaseDir);
}

} // namespace uz
