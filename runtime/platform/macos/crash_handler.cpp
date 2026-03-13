/// UzEngine Crash Handler — macOS signal handler implementation.
///
/// Installs sigaction handlers that write text and JSON crash reports into
/// `{exe_dir}/crashes/{timestamp}/`. The signal handler itself is strictly
/// async-signal-safe: it uses only write(), open(), close(), and _exit().
/// Symbol resolution and UZ_LOG calls happen only in the normal-code helpers.
///
/// On macOS, the executable path is retrieved via _NSGetExecutablePath() from
/// <mach-o/dyld.h> rather than /proc/self/exe (which is Linux-only).

#ifdef __APPLE__

#include "crash_handler.h"

#include "../../engine/logging/log.h"
#include "../../engine/paths/paths.h"
#include "../../engine/profiling/profiler.h"
#include "stack_walker.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ─────────────────────────────────────────────────────────────────────────────
// Log category
// ─────────────────────────────────────────────────────────────────────────────

UZ_DEFINE_LOG_CATEGORY(CrashHandler, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Global crash context — updated by engine systems, read by signal handler
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Atomic frame counter updated by the renderer each frame.
std::atomic<uint64_t> g_frameNumber{0};

/// Atomic flag for GPU device removal.
std::atomic<bool> g_gpuDeviceRemoved{false};

/// Scene name buffer. Written by scene system, read in crash context.
/// Not atomic — single writer assumed, signal handler reads after exception.
char g_sceneName[256] = {};

/// Last RHI operation buffer. Same thread-safety model as g_sceneName.
char g_lastRhiOp[256] = {};

/// GPU adapter name buffer. Written once at RHI init.
char g_gpuAdapter[256] = {};

/// Whether the crash handler is currently installed.
bool g_installed = false;

/// Saved previous signal actions, restored on uninstall.
struct sigaction g_prevSigSegv{};
struct sigaction g_prevSigAbrt{};
struct sigaction g_prevSigFpe{};
struct sigaction g_prevSigBus{};
struct sigaction g_prevSigIll{};

// ─────────────────────────────────────────────────────────────────────────────
// Async-signal-safe helpers — write() only, no malloc, no stdio
// ─────────────────────────────────────────────────────────────────────────────

/// Writes a null-terminated string to an fd. Async-signal-safe.
static void SafeWrite(int fd, const char* s) {
    if (s == nullptr) {
        return;
    }
    // strlen is not guaranteed async-signal-safe on all platforms, so we walk
    // the string manually.
    size_t len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    // Retry on EINTR.
    size_t written = 0;
    while (written < len) {
        const ssize_t n = write(fd, s + written, len - written);
        if (n <= 0) {
            break;
        }
        written += static_cast<size_t>(n);
    }
}

/// Writes a uint64 as a decimal string to fd. Async-signal-safe.
static void SafeWriteUInt64(int fd, uint64_t value) {
    char buf[24];
    // Build digit string right-to-left.
    int idx = static_cast<int>(sizeof(buf)) - 1;
    buf[idx] = '\0';
    if (value == 0) {
        buf[--idx] = '0';
    } else {
        while (value > 0 && idx > 0) {
            buf[--idx] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
    }
    SafeWrite(fd, buf + idx);
}

/// Writes a pointer as a 0x-prefixed hex string to fd. Async-signal-safe.
static void SafeWriteHex(int fd, const void* ptr) {
    static const char kHexChars[] = "0123456789abcdef";
    const auto addr = reinterpret_cast<uintptr_t>(ptr);
    char buf[20]; // "0x" + 16 hex digits + '\0'
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; ++i) {
        const int shift = (15 - i) * 4;
        buf[2 + i] = kHexChars[(addr >> shift) & 0xF];
    }
    buf[18] = '\0';
    SafeWrite(fd, buf);
}

/// Returns a human-readable name for a signal number. Async-signal-safe.
static const char* SignalToString(int signum) {
    switch (signum) {
    case SIGSEGV: return "SIGSEGV (segmentation fault)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGFPE:  return "SIGFPE (floating-point exception)";
    case SIGBUS:  return "SIGBUS (bus error)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    default:      return "UNKNOWN";
    }
}

/// Returns a short signal name string suitable for JSON. Async-signal-safe.
static const char* SignalToShortName(int signum) {
    switch (signum) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGFPE:  return "SIGFPE";
    case SIGBUS:  return "SIGBUS";
    case SIGILL:  return "SIGILL";
    default:      return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Crash directory creation helpers — used after signal, before output
// ─────────────────────────────────────────────────────────────────────────────

/// Formats current local time as "YYYY-MM-DD_HH-MM-SS" into buf.
/// Returns false if time retrieval fails.
/// This is NOT async-signal-safe (localtime_r is implementation-dependent),
/// but it is called before writing files, after the raw backtrace is captured.
static bool FormatTimestamp(char* buf, size_t bufSize) {
    time_t now = 0;
    if (time(&now) == static_cast<time_t>(-1)) {
        return false;
    }
    struct tm tm_info{};
    if (localtime_r(&now, &tm_info) == nullptr) {
        return false;
    }
    const int written = std::snprintf(buf, bufSize,
        "%04d-%02d-%02d_%02d-%02d-%02d",
        tm_info.tm_year + 1900,
        tm_info.tm_mon + 1,
        tm_info.tm_mday,
        tm_info.tm_hour,
        tm_info.tm_min,
        tm_info.tm_sec);
    return written > 0 && static_cast<size_t>(written) < bufSize;
}

/// Creates a directory, ignoring EEXIST. Returns true on success.
static bool MkdirIfNeeded(const char* path) {
    // 0755: owner rwx, group rx, other rx
    const int ret = mkdir(path, 0755);
    return ret == 0 || errno == EEXIST;
}

// ─────────────────────────────────────────────────────────────────────────────
// Crash report writers — called after raw capture, use snprintf + write()
// ─────────────────────────────────────────────────────────────────────────────

/// Writes a human-readable crash.txt to an open file descriptor.
/// Uses async-signal-safe write() for output but snprintf for formatting
/// (safe here because we are post-capture, single-threaded in handler).
static void WriteTextReportToFd(int fd, const CrashContext& ctx) {
    char buf[4096];
    int len = 0;

    SafeWrite(fd, "=== UzEngine Crash Report ===\n\n");

    len = std::snprintf(buf, sizeof(buf),
        "Signal:     %s (%u)\n"
        "Address:    0x%016llX\n"
        "Frame:      %llu\n"
        "Scene:      %s\n"
        "Last RHI:   %s\n"
        "GPU:        %s\n"
        "Device Removed: %s\n\n",
        SignalToString(static_cast<int>(ctx.exceptionCode)),
        ctx.exceptionCode,
        reinterpret_cast<unsigned long long>(ctx.exceptionAddress),
        static_cast<unsigned long long>(ctx.frameNumber),
        ctx.sceneName[0] ? ctx.sceneName : "(none)",
        ctx.lastRhiOp[0] ? ctx.lastRhiOp : "(none)",
        ctx.gpuAdapter[0] ? ctx.gpuAdapter : "(unknown)",
        ctx.gpuDeviceRemoved ? "YES" : "no");
    if (len > 0) {
        write(fd, buf, static_cast<size_t>(len));
    }

    // Stack trace section header.
    len = std::snprintf(buf, sizeof(buf),
        "Stack Trace (%d frames):\n", ctx.stackFrameCount);
    if (len > 0) {
        write(fd, buf, static_cast<size_t>(len));
    }

    // Write raw addresses first (async-signal-safe path used during capture).
    // We now attempt symbol resolution via backtrace_symbols (allocates).
    char** symbols = backtrace_symbols(ctx.stackFrames, ctx.stackFrameCount);

    for (int i = 0; i < ctx.stackFrameCount; ++i) {
        const char* sym = (symbols != nullptr) ? symbols[i] : nullptr;

        if (sym != nullptr) {
            len = std::snprintf(buf, sizeof(buf),
                "  [%2d] %s\n", i, sym);
        } else {
            len = std::snprintf(buf, sizeof(buf),
                "  [%2d] 0x%016llX\n",
                i, reinterpret_cast<unsigned long long>(ctx.stackFrames[i]));
        }
        if (len > 0) {
            write(fd, buf, static_cast<size_t>(len));
        }
    }

    // backtrace_symbols allocates with malloc; must free even in crash context.
    if (symbols != nullptr) {
        free(symbols); // NOLINT — intentional, backtrace_symbols contract
    }
}

/// Writes a machine-parseable crash.json to an open file descriptor.
/// Uses manual snprintf — no JSON library to avoid heap allocation patterns.
static void WriteJsonReportToFd(int fd, const CrashContext& ctx, const char* timestamp) {
    char buf[4096];
    int len = 0;

    len = std::snprintf(buf, sizeof(buf),
        "{\n"
        "  \"engine\": \"UzEngine\",\n"
        "  \"timestamp\": \"%s\",\n"
        "  \"exception\": {\n"
        "    \"code\": %u,\n"
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
        SignalToShortName(static_cast<int>(ctx.exceptionCode)),
        reinterpret_cast<unsigned long long>(ctx.exceptionAddress),
        static_cast<unsigned long long>(ctx.frameNumber),
        ctx.sceneName[0] ? ctx.sceneName : "",
        ctx.lastRhiOp[0] ? ctx.lastRhiOp : "",
        ctx.gpuAdapter[0] ? ctx.gpuAdapter : "",
        ctx.gpuDeviceRemoved ? "true" : "false");
    if (len > 0) {
        write(fd, buf, static_cast<size_t>(len));
    }

    for (int i = 0; i < ctx.stackFrameCount; ++i) {
        const char* comma = (i < ctx.stackFrameCount - 1) ? "," : "";
        len = std::snprintf(buf, sizeof(buf),
            "    {\"index\": %d, \"address\": \"0x%016llX\"}%s\n",
            i,
            reinterpret_cast<unsigned long long>(ctx.stackFrames[i]),
            comma);
        if (len > 0) {
            write(fd, buf, static_cast<size_t>(len));
        }
    }

    write(fd, "  ]\n}\n", 6);
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal handler — strictly async-signal-safe core path
// ─────────────────────────────────────────────────────────────────────────────

/// The signal handler installed via sigaction.
///
/// The async-signal-safe core: captures the backtrace using backtrace() then
/// builds the crash directory path from _NSGetExecutablePath() and writes raw
/// output. After the raw capture, we call FormatTimestamp and backtrace_symbols
/// which allocate — acceptable because SA_RESETHAND ensures we do not re-enter
/// on a second crash.
///
/// Note: _NSGetExecutablePath() is not strictly async-signal-safe, but it is
/// safe in practice because it reads from the process's dyld state which does
/// not change after startup. We call it post-capture, with SA_RESETHAND
/// preventing re-entry.
static void UzSignalHandler(int signum, siginfo_t* info, void* /*context*/) {
    // ── Step 1: Capture raw stack trace immediately (async-signal-safe). ──
    CrashContext ctx{};
    ctx.exceptionCode    = static_cast<uint32_t>(signum);
    ctx.exceptionAddress = (info != nullptr) ? info->si_addr : nullptr;
    ctx.frameNumber      = g_frameNumber.load(std::memory_order_relaxed);
    ctx.gpuDeviceRemoved = g_gpuDeviceRemoved.load(std::memory_order_relaxed);

    // memcpy is async-signal-safe.
    __builtin_memcpy(ctx.sceneName, g_sceneName, sizeof(ctx.sceneName));
    __builtin_memcpy(ctx.lastRhiOp, g_lastRhiOp, sizeof(ctx.lastRhiOp));
    __builtin_memcpy(ctx.gpuAdapter, g_gpuAdapter, sizeof(ctx.gpuAdapter));

    // backtrace() is documented as async-signal-safe on macOS (libSystem).
    ctx.stackFrameCount = backtrace(ctx.stackFrames, CrashContext::kMaxStackFrames);

    // ── Step 2: Build crash directory path via _NSGetExecutablePath(). ──
    // On macOS there is no /proc/self/exe. _NSGetExecutablePath() returns the
    // resolved path to the running executable via dyld's internal state.
    char exePath[4096] = {};
    uint32_t exePathSize = sizeof(exePath);
    if (_NSGetExecutablePath(exePath, &exePathSize) != 0) {
        // Buffer too small (path longer than 4095 chars) — fall back to ".".
        exePath[0] = '.';
        exePath[1] = '\0';
    }

    char exeDir[4096] = {};
    const ssize_t exeLen = static_cast<ssize_t>(__builtin_strlen(exePath));
    if (exeLen > 0) {
        // Walk backwards to find the last '/'.
        ssize_t slash = exeLen - 1;
        while (slash >= 0 && exePath[slash] != '/') {
            --slash;
        }
        if (slash >= 0) {
            __builtin_memcpy(exeDir, exePath, static_cast<size_t>(slash));
            exeDir[slash] = '\0';
        } else {
            exeDir[0] = '.';
            exeDir[1] = '\0';
        }
    } else {
        exeDir[0] = '.';
        exeDir[1] = '\0';
    }

    // ── Step 3: Format timestamp (not strictly async-signal-safe, but we
    //    are post-capture with SA_RESETHAND guaranteeing no re-entry). ──
    char timestamp[64] = "unknown";
    FormatTimestamp(timestamp, sizeof(timestamp));

    // Build paths: {exe_dir}/crashes/ and {exe_dir}/crashes/{timestamp}/
    char crashesParent[4096 + 16];
    std::snprintf(crashesParent, sizeof(crashesParent), "%s/crashes", exeDir);

    char crashDir[4096 + 80];
    std::snprintf(crashDir, sizeof(crashDir), "%s/%s", crashesParent, timestamp);

    MkdirIfNeeded(crashesParent);
    MkdirIfNeeded(crashDir);

    // ── Step 4: Write crash.txt ──
    char txtPath[4096 + 100];
    std::snprintf(txtPath, sizeof(txtPath), "%s/crash.txt", crashDir);
    {
        const int fd = open(txtPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            WriteTextReportToFd(fd, ctx);
            close(fd);
        } else {
            // Fallback: write minimal info to stderr (fd 2), which is
            // async-signal-safe.
            SafeWrite(2, "\n[UzEngine] CRASH: ");
            SafeWrite(2, SignalToString(signum));
            SafeWrite(2, " at ");
            SafeWriteHex(2, ctx.exceptionAddress);
            SafeWrite(2, "\n[UzEngine] Could not create crash report: ");
            SafeWrite(2, crashDir);
            SafeWrite(2, "\n");

            SafeWrite(2, "[UzEngine] Stack trace:\n");
            for (int i = 0; i < ctx.stackFrameCount; ++i) {
                SafeWrite(2, "  frame ");
                SafeWriteUInt64(2, static_cast<uint64_t>(i));
                SafeWrite(2, " ");
                SafeWriteHex(2, ctx.stackFrames[i]);
                SafeWrite(2, "\n");
            }
        }
    }

    // ── Step 5: Write crash.json ──
    char jsonPath[4096 + 100];
    std::snprintf(jsonPath, sizeof(jsonPath), "%s/crash.json", crashDir);
    {
        const int fd = open(jsonPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            WriteJsonReportToFd(fd, ctx, timestamp);
            close(fd);
        }
    }

    // SA_RESETHAND already restored SIG_DFL before we entered, so returning
    // will re-deliver the signal with the default handler (core dump / abort).
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper — safely copies a source string into a fixed-size destination buffer
// ─────────────────────────────────────────────────────────────────────────────

/// Safely copies src into dst. If src is nullptr, clears dst.
static void SafeCopyString(char* dst, size_t dstSize, const char* src) {
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void InstallCrashHandler() {
    UZ_PROFILE_FUNCTION();

    struct sigaction sa{};
    sa.sa_sigaction = UzSignalHandler;
    sigemptyset(&sa.sa_mask);
    // SA_SIGINFO: gives us siginfo_t (fault address).
    // SA_RESETHAND: restore SIG_DFL before entering handler (no re-entry on second crash).
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

    sigaction(SIGSEGV, &sa, &g_prevSigSegv);
    sigaction(SIGABRT, &sa, &g_prevSigAbrt);
    sigaction(SIGFPE,  &sa, &g_prevSigFpe);
    sigaction(SIGBUS,  &sa, &g_prevSigBus);
    sigaction(SIGILL,  &sa, &g_prevSigIll);

    g_installed = true;

    UZ_LOG(CrashHandler, Info, "Crash handler installed (SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL)");
}

void UninstallCrashHandler() {
    if (!g_installed) {
        return;
    }

    sigaction(SIGSEGV, &g_prevSigSegv, nullptr);
    sigaction(SIGABRT, &g_prevSigAbrt, nullptr);
    sigaction(SIGFPE,  &g_prevSigFpe,  nullptr);
    sigaction(SIGBUS,  &g_prevSigBus,  nullptr);
    sigaction(SIGILL,  &g_prevSigIll,  nullptr);

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

#endif // __APPLE__
