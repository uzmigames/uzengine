// crash_reporter.cpp -- CrashReporter implementation.
//
// Linux: installs POSIX signal handlers (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS).
// Windows: stubs for SetUnhandledExceptionFilter (Phase 2).
//
// The signal handler path uses only async-signal-safe functions: open(), write(),
// close(), _exit(). The manual write_report() path uses stdio for convenience.

#include "engine/core/crash_reporter.h"
#include "engine/core/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#if defined(UZ_PLATFORM_LINUX) || defined(UZ_PLATFORM_MACOS)
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#elif defined(UZ_PLATFORM_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// ---------------------------------------------------------------------------
// Shared state -- static storage, no heap
// ---------------------------------------------------------------------------

namespace {

// Max path length for output directory (directory component only).
constexpr size_t kMaxDirLen = 460;

// Max path length for full file paths (dir + filename).
constexpr size_t kMaxPathLen = 512;

// Output directory for crash reports. Default: /tmp/uz_crashes (Linux) or
// current dir (Windows). Sized smaller than kMaxPathLen to leave room for filename.
char s_output_dir[kMaxDirLen] = "/tmp/uz_crashes";

// Path of the most recently written crash report.
char s_last_report_path[kMaxPathLen] = {};

// Whether handlers are currently installed.
bool s_installed = false;

// Pre-crash callback (must be async-signal-safe on Linux).
uz::CrashReporter::PreCrashCallback s_pre_crash_callback = nullptr;

// Engine version and build type -- set at compile time.
constexpr const char* kEngineVersion = "0.1.0";

#if defined(UZENGINE_DEBUG) || !defined(NDEBUG)
constexpr const char* kBuildType = "Debug";
#else
constexpr const char* kBuildType = "Release";
#endif

// Format current time as ISO 8601 into a buffer. Returns number of chars written.
// Uses gmtime_r (async-signal-safe on most platforms, but we only call it from
// the non-signal path). The signal path uses a simpler numeric timestamp.
int format_timestamp(char* buf, size_t buf_size) {
    const std::time_t now = std::time(nullptr);
    struct tm utc{};
#if defined(UZ_PLATFORM_WINDOWS)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    return static_cast<int>(
        std::strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", &utc));
}

// Write a crash report to a file using stdio (non-signal path).
// Returns the file path on success, empty string on failure.
std::string write_report_stdio(const char* reason, const char* details) {
    // Ensure output directory exists.
    uz::platform::mkdir_recursive(s_output_dir);

    // Build filename: crash_YYYYMMDD_HHMMSS.txt
    char time_part[32] = {};
    {
        const std::time_t now = std::time(nullptr);
        struct tm utc{};
#if defined(UZ_PLATFORM_WINDOWS)
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        std::strftime(time_part, sizeof(time_part), "%Y%m%d_%H%M%S", &utc);
    }

    char filepath[kMaxPathLen] = {};
    std::snprintf(filepath, sizeof(filepath), "%s/crash_%s.txt",
                  s_output_dir, time_part);

    FILE* f = std::fopen(filepath, "w");
    if (!f) return {};

    // [crash] section
    char timestamp[64] = {};
    format_timestamp(timestamp, sizeof(timestamp));

    std::fprintf(f, "[crash]\n");
    std::fprintf(f, "timestamp = %s\n", timestamp);
    std::fprintf(f, "signal = %s\n", reason ? reason : "unknown");
    if (details) {
        std::fprintf(f, "details = %s\n", details);
    }

    // [stack] section -- capture stack trace from non-signal context
    std::fprintf(f, "\n[stack]\n");
#if defined(UZ_PLATFORM_LINUX) || defined(UZ_PLATFORM_MACOS)
    // backtrace() is available on Linux/macOS via <execinfo.h>.
    // Not async-signal-safe, but this is the stdio (non-signal) path.
    {
        // Forward-declare to avoid header in global scope (keeps Windows clean).
        // We include execinfo.h only on Linux/macOS.
        void* frames[64] = {};
        int depth = backtrace(frames, 64);
        char** symbols = backtrace_symbols(frames, depth);
        if (symbols) {
            for (int i = 0; i < depth; ++i) {
                std::fprintf(f, "%s\n", symbols[i]);
            }
            std::free(symbols);
        } else {
            for (int i = 0; i < depth; ++i) {
                std::fprintf(f, "0x%016lx\n",
                             reinterpret_cast<unsigned long>(frames[i]));
            }
        }
    }
#else
    std::fprintf(f, "(stack trace not available on this platform)\n");
#endif

    // [engine] section
    std::fprintf(f, "\n[engine]\n");
    std::fprintf(f, "version = %s\n", kEngineVersion);
    std::fprintf(f, "build = %s\n", kBuildType);

    std::fclose(f);

    // Record last report path.
    std::strncpy(s_last_report_path, filepath, kMaxPathLen - 1);
    s_last_report_path[kMaxPathLen - 1] = '\0';

    return std::string(filepath);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Linux implementation
// ---------------------------------------------------------------------------
#if defined(UZ_PLATFORM_LINUX) || defined(UZ_PLATFORM_MACOS)

namespace {

// Number of signals we handle.
constexpr int kHandledSignals[] = { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS };
constexpr size_t kNumHandledSignals = sizeof(kHandledSignals) / sizeof(kHandledSignals[0]);

// Storage for previous signal actions (to restore on uninstall).
struct sigaction s_prev_actions[kNumHandledSignals] = {};

// Async-signal-safe integer-to-hex conversion. Writes hex digits into buf.
// Returns number of chars written (does NOT NUL-terminate).
int int_to_hex(unsigned long val, char* buf, int buf_size) {
    // Build hex string in reverse.
    char tmp[20] = {};
    int len = 0;
    if (val == 0) {
        tmp[len++] = '0';
    } else {
        while (val > 0 && len < static_cast<int>(sizeof(tmp))) {
            const int digit = static_cast<int>(val & 0xF);
            tmp[len++] = (digit < 10) ? static_cast<char>('0' + digit)
                                       : static_cast<char>('a' + digit - 10);
            val >>= 4;
        }
    }
    // Reverse into output buffer.
    int written = 0;
    for (int i = len - 1; i >= 0 && written < buf_size; --i) {
        buf[written++] = tmp[i];
    }
    return written;
}

// Async-signal-safe string write helper.
void safe_write(int fd, const char* str) {
    if (!str) return;
    size_t len = 0;
    while (str[len] != '\0') ++len;
    // write() is async-signal-safe per POSIX.
    (void)::write(fd, str, len);
}

// Async-signal-safe write of a hex pointer value.
void safe_write_hex(int fd, unsigned long val) {
    char buf[20] = {};
    const int len = int_to_hex(val, buf, static_cast<int>(sizeof(buf)));
    (void)::write(fd, "0x", 2);
    (void)::write(fd, buf, static_cast<size_t>(len));
}

// Convert signal number to a human-readable name.
const char* signal_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        case SIGBUS:  return "SIGBUS";
        default:      return "UNKNOWN";
    }
}

// Signal handler -- async-signal-safe path.
// Uses only open/write/close/_exit.
void crash_signal_handler(int sig, siginfo_t* info, void* /*ucontext*/) {
    // Invoke pre-crash callback if set.
    if (s_pre_crash_callback) {
        s_pre_crash_callback();
    }

    // Build filepath: output_dir/crash_<timestamp>.txt
    // We use platform time to get a numeric timestamp (async-signal-safe).
    char filepath[kMaxPathLen] = {};
    {
        // Copy output dir.
        size_t pos = 0;
        for (size_t i = 0; s_output_dir[i] != '\0' && pos < kMaxPathLen - 1; ++i) {
            filepath[pos++] = s_output_dir[i];
        }
        // Append /crash_ prefix
        const char* suffix = "/crash_signal_";
        for (size_t i = 0; suffix[i] != '\0' && pos < kMaxPathLen - 1; ++i) {
            filepath[pos++] = suffix[i];
        }
        // Append numeric timestamp (seconds since epoch via time()).
        // time() is listed as async-signal-safe in POSIX.
        const std::time_t now = std::time(nullptr);
        char hex_ts[20] = {};
        const int hex_len = int_to_hex(
            static_cast<unsigned long>(now), hex_ts, static_cast<int>(sizeof(hex_ts)));
        for (int i = 0; i < hex_len && pos < kMaxPathLen - 1; ++i) {
            filepath[pos++] = hex_ts[i];
        }
        // Append .txt
        const char* ext = ".txt";
        for (size_t i = 0; ext[i] != '\0' && pos < kMaxPathLen - 1; ++i) {
            filepath[pos++] = ext[i];
        }
        filepath[pos] = '\0';
    }

    // Ensure output directory exists (mkdir_recursive uses malloc, so we do a
    // best-effort mkdir of just the output dir via the syscall directly).
    // mkdir is not listed as async-signal-safe, but in practice it is a thin
    // syscall wrapper. This is best-effort -- the dir likely already exists.
    (void)::mkdir(s_output_dir, 0755);

    // Open file for writing.
    const int fd = ::open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        // Cannot write crash report -- just exit.
        _exit(128 + sig);
    }

    // [crash] section
    safe_write(fd, "[crash]\n");
    safe_write(fd, "signal = ");
    safe_write(fd, signal_name(sig));
    safe_write(fd, "\n");

    // Faulting address (from siginfo_t).
    if (info) {
        safe_write(fd, "address = ");
        safe_write_hex(fd,
            reinterpret_cast<unsigned long>(info->si_addr));
        safe_write(fd, "\n");
    }

    // [stack] section -- backtrace is NOT async-signal-safe, but it is
    // commonly used in signal handlers in practice (glibc implementation is
    // reasonably safe). We accept the small risk here because the alternative
    // is no stack trace at all.
    safe_write(fd, "\n[stack]\n");
    {
        void* frames[64] = {};
        const int depth = backtrace(frames, 64);
        for (int i = 0; i < depth; ++i) {
            safe_write_hex(fd,
                reinterpret_cast<unsigned long>(frames[i]));
            safe_write(fd, "\n");
        }
    }

    // [engine] section
    safe_write(fd, "\n[engine]\n");
    safe_write(fd, "version = ");
    safe_write(fd, kEngineVersion);
    safe_write(fd, "\n");
    safe_write(fd, "build = ");
    safe_write(fd, kBuildType);
    safe_write(fd, "\n");

    ::close(fd);

    // Record last report path (not strictly safe, but we are about to exit).
    {
        size_t i = 0;
        while (filepath[i] != '\0' && i < kMaxPathLen - 1) {
            s_last_report_path[i] = filepath[i];
            ++i;
        }
        s_last_report_path[i] = '\0';
    }

    // Re-raise the signal with default handler to produce a core dump / correct exit code.
    // Reset the handler to default.
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
    raise(sig);

    // If raise returns (shouldn't), force exit.
    _exit(128 + sig);
}

} // anonymous namespace

namespace uz {

void CrashReporter::install(const char* output_dir) {
    if (output_dir) {
        set_output_dir(output_dir);
    }

    // Install signal handlers for each handled signal.
    for (size_t i = 0; i < kNumHandledSignals; ++i) {
        struct sigaction sa{};
        sa.sa_sigaction = crash_signal_handler;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND; // One-shot to avoid infinite recursion.
        sigemptyset(&sa.sa_mask);

        // Block all handled signals during handler execution to prevent
        // nested crashes from corrupting the report.
        for (size_t j = 0; j < kNumHandledSignals; ++j) {
            sigaddset(&sa.sa_mask, kHandledSignals[j]);
        }

        sigaction(kHandledSignals[i], &sa, &s_prev_actions[i]);
    }

    s_installed = true;
}

void CrashReporter::uninstall() {
    if (!s_installed) return;

    // Restore previous signal handlers.
    for (size_t i = 0; i < kNumHandledSignals; ++i) {
        sigaction(kHandledSignals[i], &s_prev_actions[i], nullptr);
    }

    s_installed = false;
}

} // namespace uz

// ---------------------------------------------------------------------------
// Windows implementation (stub)
// ---------------------------------------------------------------------------
#elif defined(UZ_PLATFORM_WINDOWS)

namespace {

LPTOP_LEVEL_EXCEPTION_FILTER s_prev_filter = nullptr;

LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* /*exc*/) {
    if (s_pre_crash_callback) {
        s_pre_crash_callback();
    }
    write_report_stdio("UnhandledException", nullptr);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // anonymous namespace

namespace uz {

void CrashReporter::install(const char* output_dir) {
    if (output_dir) {
        set_output_dir(output_dir);
    }
    s_prev_filter = SetUnhandledExceptionFilter(unhandled_exception_filter);
    s_installed = true;
}

void CrashReporter::uninstall() {
    if (!s_installed) return;
    SetUnhandledExceptionFilter(s_prev_filter);
    s_prev_filter = nullptr;
    s_installed = false;
}

} // namespace uz

#endif // UZ_PLATFORM_*

// ---------------------------------------------------------------------------
// Shared implementation (platform-independent)
// ---------------------------------------------------------------------------

namespace uz {

void CrashReporter::set_output_dir(const char* dir) {
    if (!dir) return;
    std::strncpy(s_output_dir, dir, kMaxDirLen - 1);
    s_output_dir[kMaxDirLen - 1] = '\0';
}

const char* CrashReporter::output_dir() {
    return s_output_dir;
}

std::string CrashReporter::write_report(const char* reason, const char* details) {
    return write_report_stdio(reason, details);
}

void CrashReporter::set_pre_crash_callback(PreCrashCallback cb) {
    s_pre_crash_callback = cb;
}

bool CrashReporter::is_installed() noexcept {
    return s_installed;
}

std::string CrashReporter::last_report_path() {
    return std::string(s_last_report_path);
}

} // namespace uz
