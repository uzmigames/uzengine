// crash_reporter.cpp -- CrashReporter implementation.
//
// Linux: installs POSIX signal handlers (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS).
// Windows: SetUnhandledExceptionFilter with StackWalk64, JSON report, MiniDump.
//
// The signal handler path uses only async-signal-safe functions: open(), write(),
// close(), _exit(). The manual write_report() path uses stdio for convenience.

#include "engine/core/crash_reporter.h"
#include "engine/core/platform.h"

#include <cstdio>
#include <cstdlib>
#include "common/safe_string.h"
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

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
#include <dbghelp.h>
#include <psapi.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")
#endif

// ---------------------------------------------------------------------------
// Shared state -- static storage, no heap
// ---------------------------------------------------------------------------

namespace {

// Max path length for output directory (directory component only).
constexpr size_t kMaxDirLen = 460;

// Max path length for full file paths (dir + filename).
constexpr size_t kMaxPathLen = 512;

// Output directory for crash reports.
char s_output_dir[kMaxDirLen] = ".uzengine/crash_reports";

// Path of the most recently written crash report.
char s_last_report_path[kMaxPathLen] = {};

// Whether handlers are currently installed.
bool s_installed = false;

// Pre-crash callback (must be async-signal-safe on Linux).
uz::CrashReporter::PreCrashCallback s_pre_crash_callback = nullptr;

// Engine state snapshot for crash context.
uz::EngineStateSnapshot s_engine_state = {};

// Custom metadata key-value pairs (à la UE5 SetGameData).
std::mutex s_metadata_mutex;
std::unordered_map<std::string, std::string> s_metadata;

// Engine version and build type -- set at compile time.
constexpr const char* kEngineVersion = "0.3.0";

#if defined(UZENGINE_DEBUG) || !defined(NDEBUG)
constexpr const char* kBuildType = "Debug";
#else
constexpr const char* kBuildType = "Release";
#endif

// Format current time as ISO 8601 into a buffer. Returns number of chars written.
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

// Read the tail of crash_log.txt (last N lines) for inclusion in crash report.
std::string read_log_tail(int max_lines = 100) {
    // Find crash_log.txt next to the executable.
    char exe_path[MAX_PATH] = {};
#if defined(UZ_PLATFORM_WINDOWS)
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
#endif
    // Find last slash.
    char* last_slash = exe_path;
    for (char* p = exe_path; *p; ++p) {
        if (*p == '\\' || *p == '/') last_slash = p;
    }
    last_slash[1] = '\0';
    strcat_s(exe_path, sizeof(exe_path), "crash_log.txt");

    std::ifstream f(exe_path);
    if (!f) return "[]";

    // Read all lines, keep last max_lines.
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) {
        lines.push_back(line);
        if (static_cast<int>(lines.size()) > max_lines * 2) {
            // Trim periodically to avoid huge memory usage on very large logs.
            lines.erase(lines.begin(),
                        lines.begin() + static_cast<int>(lines.size()) - max_lines);
        }
    }

    // Keep only last max_lines.
    int start = 0;
    if (static_cast<int>(lines.size()) > max_lines) {
        start = static_cast<int>(lines.size()) - max_lines;
    }

    std::ostringstream oss;
    oss << "[";
    for (int i = start; i < static_cast<int>(lines.size()); ++i) {
        if (i > start) oss << ",";
        // JSON-escape the line (minimal: escape quotes and backslashes).
        oss << "\"";
        for (char c : lines[i]) {
            if (c == '"') oss << "\\\"";
            else if (c == '\\') oss << "\\\\";
            else if (c == '\n') oss << "\\n";
            else if (c == '\r') { /* skip */ }
            else if (static_cast<unsigned char>(c) < 0x20) oss << " ";
            else oss << c;
        }
        oss << "\"";
    }
    oss << "]";
    return oss.str();
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

    FILE* f = uz::file_open(filepath, "w");
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
    {
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
    uz::str_copy(s_last_report_path, kMaxPathLen, filepath);

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
    int written = 0;
    for (int i = len - 1; i >= 0 && written < buf_size; --i) {
        buf[written++] = tmp[i];
    }
    return written;
}

void safe_write(int fd, const char* str) {
    if (!str) return;
    size_t len = 0;
    while (str[len] != '\0') ++len;
    (void)::write(fd, str, len);
}

void safe_write_hex(int fd, unsigned long val) {
    char buf[20] = {};
    const int len = int_to_hex(val, buf, static_cast<int>(sizeof(buf)));
    (void)::write(fd, "0x", 2);
    (void)::write(fd, buf, static_cast<size_t>(len));
}

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

void crash_signal_handler(int sig, siginfo_t* info, void* /*ucontext*/) {
    if (s_pre_crash_callback) {
        s_pre_crash_callback();
    }

    char filepath[kMaxPathLen] = {};
    {
        size_t pos = 0;
        for (size_t i = 0; s_output_dir[i] != '\0' && pos < kMaxPathLen - 1; ++i) {
            filepath[pos++] = s_output_dir[i];
        }
        const char* suffix = "/crash_signal_";
        for (size_t i = 0; suffix[i] != '\0' && pos < kMaxPathLen - 1; ++i) {
            filepath[pos++] = suffix[i];
        }
        const std::time_t now = std::time(nullptr);
        char hex_ts[20] = {};
        const int hex_len = int_to_hex(
            static_cast<unsigned long>(now), hex_ts, static_cast<int>(sizeof(hex_ts)));
        for (int i = 0; i < hex_len && pos < kMaxPathLen - 1; ++i) {
            filepath[pos++] = hex_ts[i];
        }
        const char* ext = ".txt";
        for (size_t i = 0; ext[i] != '\0' && pos < kMaxPathLen - 1; ++i) {
            filepath[pos++] = ext[i];
        }
        filepath[pos] = '\0';
    }

    (void)::mkdir(s_output_dir, 0755);

    const int fd = ::open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        _exit(128 + sig);
    }

    safe_write(fd, "[crash]\n");
    safe_write(fd, "signal = ");
    safe_write(fd, signal_name(sig));
    safe_write(fd, "\n");

    if (info) {
        safe_write(fd, "address = ");
        safe_write_hex(fd,
            reinterpret_cast<unsigned long>(info->si_addr));
        safe_write(fd, "\n");
    }

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

    safe_write(fd, "\n[engine]\n");
    safe_write(fd, "version = ");
    safe_write(fd, kEngineVersion);
    safe_write(fd, "\n");
    safe_write(fd, "build = ");
    safe_write(fd, kBuildType);
    safe_write(fd, "\n");

    ::close(fd);

    {
        size_t i = 0;
        while (filepath[i] != '\0' && i < kMaxPathLen - 1) {
            s_last_report_path[i] = filepath[i];
            ++i;
        }
        s_last_report_path[i] = '\0';
    }

    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
    raise(sig);

    _exit(128 + sig);
}

} // anonymous namespace

namespace uz {

void CrashReporter::install(const char* output_dir) {
    if (output_dir) {
        set_output_dir(output_dir);
    }

    for (size_t i = 0; i < kNumHandledSignals; ++i) {
        struct sigaction sa{};
        sa.sa_sigaction = crash_signal_handler;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigemptyset(&sa.sa_mask);

        for (size_t j = 0; j < kNumHandledSignals; ++j) {
            sigaddset(&sa.sa_mask, kHandledSignals[j]);
        }

        sigaction(kHandledSignals[i], &sa, &s_prev_actions[i]);
    }

    s_installed = true;
}

void CrashReporter::uninstall() {
    if (!s_installed) return;

    for (size_t i = 0; i < kNumHandledSignals; ++i) {
        sigaction(kHandledSignals[i], &s_prev_actions[i], nullptr);
    }

    s_installed = false;
}

} // namespace uz

// ---------------------------------------------------------------------------
// Windows implementation
// ---------------------------------------------------------------------------
#elif defined(UZ_PLATFORM_WINDOWS)

namespace {

LPTOP_LEVEL_EXCEPTION_FILTER s_prev_filter = nullptr;

// Convert Windows exception code to a human-readable string.
const char* exception_code_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:            return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND:  return "EXCEPTION_FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_OVERFLOW:          return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_UNDERFLOW:         return "EXCEPTION_FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:          return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_STACK_OVERFLOW:        return "EXCEPTION_STACK_OVERFLOW";
        case EXCEPTION_PRIV_INSTRUCTION:      return "EXCEPTION_PRIV_INSTRUCTION";
        default:                              return "UNKNOWN_EXCEPTION";
    }
}

// Capture symbolicated stack trace via StackWalk64.
// Returns a JSON array string like: [{"module":"x","function":"y","file":"z","line":N}, ...]
std::string capture_stack_trace(CONTEXT* ctx) {
    std::ostringstream oss;
    oss << "[";

    HANDLE process = GetCurrentProcess();
    HANDLE thread  = GetCurrentThread();

    // Initialize symbol handler.
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, NULL, TRUE);

    STACKFRAME64 frame = {};
    DWORD machine_type = 0;

#ifdef _M_X64
    machine_type = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset    = ctx->Rip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrFrame.Mode   = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp;
    frame.AddrStack.Mode   = AddrModeFlat;
#elif defined(_M_IX86)
    machine_type = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset    = ctx->Eip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Ebp;
    frame.AddrFrame.Mode   = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Esp;
    frame.AddrStack.Mode   = AddrModeFlat;
#elif defined(_M_ARM64)
    machine_type = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset    = ctx->Pc;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Fp;
    frame.AddrFrame.Mode   = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Sp;
    frame.AddrStack.Mode   = AddrModeFlat;
#endif

    constexpr int kMaxFrames = 64;
    bool first = true;

    for (int i = 0; i < kMaxFrames; ++i) {
        if (!StackWalk64(machine_type, process, thread, &frame, ctx,
                         NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
            break;
        }

        if (frame.AddrPC.Offset == 0) break;

        if (!first) oss << ",";
        first = false;

        oss << "{";

        // Module name.
        DWORD64 module_base = SymGetModuleBase64(process, frame.AddrPC.Offset);
        char module_name[MAX_PATH] = {};
        if (module_base) {
            GetModuleFileNameA(reinterpret_cast<HMODULE>(module_base),
                               module_name, MAX_PATH);
            // Extract just the filename.
            const char* base = module_name;
            for (const char* p = module_name; *p; ++p) {
                if (*p == '\\' || *p == '/') base = p + 1;
            }
            oss << "\"module\":\"" << base << "\"";
        } else {
            oss << "\"module\":\"unknown\"";
        }

        // Function name.
        char sym_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)] = {};
        SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buffer);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = MAX_SYM_NAME;

        DWORD64 sym_displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &sym_displacement, sym)) {
            oss << ",\"function\":\"" << sym->Name << "\"";
        }

        // File and line.
        IMAGEHLP_LINE64 line_info = {};
        line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD line_displacement = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset,
                                 &line_displacement, &line_info)) {
            // Extract just the filename from the full path.
            const char* file_base = line_info.FileName;
            for (const char* p = line_info.FileName; *p; ++p) {
                if (*p == '\\' || *p == '/') file_base = p + 1;
            }
            oss << ",\"file\":\"" << file_base << "\""
                << ",\"line\":" << line_info.LineNumber;
        }

        oss << "}";
    }

    SymCleanup(process);

    oss << "]";
    return oss.str();
}

// Write latest.dmp MiniDump file.
void write_minidump(EXCEPTION_POINTERS* exc) {
    char dmp_path[kMaxPathLen] = {};
    std::snprintf(dmp_path, sizeof(dmp_path), "%s/latest.dmp", s_output_dir);

    HANDLE file = CreateFileA(dmp_path, GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION mei = {};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = exc;
    mei.ClientPointers = FALSE;

    MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        file,
        static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData),
        exc ? &mei : NULL,
        NULL,
        NULL
    );

    CloseHandle(file);
}

// Write the structured JSON crash report (latest.json).
void write_json_report(EXCEPTION_POINTERS* exc) {
    uz::platform::mkdir_recursive(s_output_dir);

    char json_path[kMaxPathLen] = {};
    std::snprintf(json_path, sizeof(json_path), "%s/latest.json", s_output_dir);

    char timestamp[64] = {};
    format_timestamp(timestamp, sizeof(timestamp));

    const DWORD exc_code = exc && exc->ExceptionRecord
        ? exc->ExceptionRecord->ExceptionCode : 0;
    const char* exc_name = exception_code_name(exc_code);

    // Capture stack trace.
    std::string stack_json = "[]";
    if (exc && exc->ContextRecord) {
        // Make a copy of the context since StackWalk64 modifies it.
        CONTEXT ctx_copy = *exc->ContextRecord;
        stack_json = capture_stack_trace(&ctx_copy);
    }

    // Read log tail.
    std::string log_tail = read_log_tail(100);

    // Get OS info.
    OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    // RtlGetVersion is the reliable way to get version on Windows 10+
    using RtlGetVersionFunc = LONG(WINAPI*)(OSVERSIONINFOEXW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = reinterpret_cast<RtlGetVersionFunc>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (fn) fn(&osvi);
    }

    // Build JSON manually (no dependency on nlohmann::json in crash path).
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"timestamp\": \"" << timestamp << "\",\n";
    oss << "  \"crash_type\": \"" << exc_name << "\",\n";
    oss << "  \"exception_code\": \"0x" << std::hex << exc_code << std::dec << "\",\n";
    oss << "  \"engine_version\": \"" << kEngineVersion << "\",\n";
    oss << "  \"build_type\": \"" << kBuildType << "\",\n";

    // OS info.
    oss << "  \"os\": {\n";
    oss << "    \"name\": \"Windows\",\n";
    oss << "    \"major\": " << osvi.dwMajorVersion << ",\n";
    oss << "    \"minor\": " << osvi.dwMinorVersion << ",\n";
    oss << "    \"build\": " << osvi.dwBuildNumber << "\n";
    oss << "  },\n";

    // Command line.
    oss << "  \"command_line\": \"";
    {
        const char* cmd = GetCommandLineA();
        for (const char* p = cmd; *p; ++p) {
            if (*p == '"') oss << "\\\"";
            else if (*p == '\\') oss << "\\\\";
            else oss << *p;
        }
    }
    oss << "\",\n";

    // Memory statistics.
    oss << "  \"memory\": {\n";
    {
        MEMORYSTATUSEX mem = {};
        mem.dwLength = sizeof(mem);
        GlobalMemoryStatusEx(&mem);
        oss << "    \"physical_total_mb\": " << (mem.ullTotalPhys / (1024 * 1024)) << ",\n";
        oss << "    \"physical_available_mb\": " << (mem.ullAvailPhys / (1024 * 1024)) << ",\n";
        oss << "    \"virtual_total_mb\": " << (mem.ullTotalVirtual / (1024 * 1024)) << ",\n";
        oss << "    \"virtual_available_mb\": " << (mem.ullAvailVirtual / (1024 * 1024)) << ",\n";
        oss << "    \"memory_load_percent\": " << mem.dwMemoryLoad << "\n";
    }
    oss << "  },\n";

    // Loaded modules.
    oss << "  \"loaded_modules\": [\n";
    {
        HANDLE proc = GetCurrentProcess();
        HMODULE modules[512] = {};
        DWORD needed = 0;
        if (EnumProcessModules(proc, modules, sizeof(modules), &needed)) {
            int count = needed / sizeof(HMODULE);
            if (count > 512) count = 512;
            for (int i = 0; i < count; ++i) {
                char mod_path[MAX_PATH] = {};
                if (GetModuleFileNameExA(proc, modules[i], mod_path, MAX_PATH)) {
                    // Extract basename.
                    const char* base = mod_path;
                    for (const char* p = mod_path; *p; ++p) {
                        if (*p == '\\' || *p == '/') base = p + 1;
                    }
                    if (i > 0) oss << ",\n";
                    oss << "    \"" << base << "\"";
                }
            }
        }
    }
    oss << "\n  ],\n";

    // CPU call stack.
    oss << "  \"cpu_callstack\": " << stack_json << ",\n";

    // Engine state.
    oss << "  \"engine_state\": {\n";
    oss << "    \"scene\": \"" << s_engine_state.scene_path << "\",\n";
    oss << "    \"camera_position\": ["
        << s_engine_state.camera_pos[0] << ", "
        << s_engine_state.camera_pos[1] << ", "
        << s_engine_state.camera_pos[2] << "],\n";
    oss << "    \"camera_yaw\": " << s_engine_state.camera_yaw << ",\n";
    oss << "    \"camera_pitch\": " << s_engine_state.camera_pitch << ",\n";
    oss << "    \"selected_node\": \"" << s_engine_state.selected_node << "\",\n";
    oss << "    \"frame_number\": " << s_engine_state.frame_number << "\n";
    oss << "  },\n";

    // Custom metadata.
    oss << "  \"metadata\": {\n";
    {
        std::lock_guard<std::mutex> lock(s_metadata_mutex);
        bool first_meta = true;
        for (const auto& [k, v] : s_metadata) {
            if (!first_meta) oss << ",\n";
            first_meta = false;
            oss << "    \"" << k << "\": \"" << v << "\"";
        }
    }
    oss << "\n  },\n";

    // Log tail.
    oss << "  \"log_tail\": " << log_tail << ",\n";

    // GPU telemetry placeholder (DRED report is in separate dred_report.json).
    oss << "  \"gpu_telemetry\": null\n";
    oss << "}\n";

    std::string json_str = oss.str();

    // Generate crash GUID by hashing first 5 function names from the stack.
    // Simple FNV-1a hash for deduplication.
    uint64_t crash_hash = 14695981039346656037ULL; // FNV offset basis
    {
        // Parse the stack JSON to extract up to 5 function names.
        // Simple scan for "function":"..." patterns.
        const char* s = stack_json.c_str();
        int func_count = 0;
        while (*s && func_count < 5) {
            const char* fn = strstr(s, "\"function\":\"");
            if (!fn) break;
            fn += 12; // skip "function":"
            while (*fn && *fn != '"') {
                crash_hash ^= static_cast<uint64_t>(static_cast<unsigned char>(*fn));
                crash_hash *= 1099511628211ULL; // FNV prime
                ++fn;
            }
            func_count++;
            s = fn;
        }
    }

    char guid[24] = {};
    std::snprintf(guid, sizeof(guid), "%016llX", static_cast<unsigned long long>(crash_hash));

    // Insert crash_guid into the JSON (after the opening brace).
    // Find first newline and insert after it.
    size_t first_nl = json_str.find('\n');
    if (first_nl != std::string::npos) {
        std::string guid_line = "  \"crash_guid\": \"" + std::string(guid) + "\",\n";
        json_str.insert(first_nl + 1, guid_line);
    }

    // Write latest.json.
    FILE* f = nullptr;
    if (fopen_s(&f, json_path, "w") == 0 && f) {
        std::fwrite(json_str.c_str(), 1, json_str.size(), f);
        std::fclose(f);
    }

    // Archive: also save as crash_{guid}_{timestamp}.json for history.
    {
        char ts[32] = {};
        {
            const std::time_t now = std::time(nullptr);
            struct tm utc{};
            gmtime_s(&utc, &now);
            std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &utc);
        }
        char archive_path[kMaxPathLen] = {};
        std::snprintf(archive_path, sizeof(archive_path),
                      "%s/crash_%s_%s.json", s_output_dir, guid, ts);
        FILE* af = nullptr;
        if (fopen_s(&af, archive_path, "w") == 0 && af) {
            std::fwrite(json_str.c_str(), 1, json_str.size(), af);
            std::fclose(af);
        }
    }

    // Record last report path.
    uz::str_copy(s_last_report_path, kMaxPathLen, json_path);
}

LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* exc) {
    if (s_pre_crash_callback) {
        s_pre_crash_callback();
    }

    // Write JSON report.
    write_json_report(exc);

    // Write MiniDump.
    write_minidump(exc);

    // Also write the legacy text report for compatibility.
    const DWORD code = exc && exc->ExceptionRecord
        ? exc->ExceptionRecord->ExceptionCode : 0;
    char detail_buf[64] = {};
    std::snprintf(detail_buf, sizeof(detail_buf), "ExceptionCode=0x%08X",
                  static_cast<unsigned>(code));
    write_report_stdio(exception_code_name(code), detail_buf);

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
    uz::str_copy(s_output_dir, kMaxDirLen, dir);
}

const char* CrashReporter::output_dir() {
    return s_output_dir;
}

void CrashReporter::update_engine_state(const EngineStateSnapshot& state) {
    s_engine_state = state;
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

std::string CrashReporter::read_latest_report() {
    char json_path[kMaxPathLen] = {};
    std::snprintf(json_path, sizeof(json_path), "%s/latest.json", s_output_dir);

    std::ifstream f(json_path);
    if (!f) return {};

    return std::string(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
}

void CrashReporter::set_metadata(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(s_metadata_mutex);
    s_metadata[key] = value;
}

void CrashReporter::clear_metadata() {
    std::lock_guard<std::mutex> lock(s_metadata_mutex);
    s_metadata.clear();
}

} // namespace uz

