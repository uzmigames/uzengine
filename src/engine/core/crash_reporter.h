#pragma once

// CrashReporter -- portable signal/exception handler that writes a structured
// crash report to disk before the process terminates.
//
// Usage:
//   CrashReporter::install();   // Call once at engine startup
//   CrashReporter::set_output_dir("/tmp/crashes"); // optional
//
// On Linux: installs handlers for SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS.
// On Windows: sets a top-level SEH exception filter via SetUnhandledExceptionFilter.
//
// The crash report format is plain-text:
//   [crash]
//   timestamp = 2026-03-03T12:00:00Z
//   signal = SIGSEGV
//   address = 0x0000000000000000
//   [stack]
//   0x00007f1234567890  (symbolic if available)
//   ...
//   [engine]
//   version = 0.1.0
//   build = Debug

#include <cstdint>
#include <functional>
#include <string>

namespace uz {

class CrashReporter {
public:
    // Install signal/exception handlers. Call once at startup.
    static void install(const char* output_dir = nullptr);

    // Uninstall handlers (for tests). Restores previous handlers.
    static void uninstall();

    // Set the directory where crash reports are written.
    static void set_output_dir(const char* dir);

    // Returns the directory where crash reports are written.
    [[nodiscard]] static const char* output_dir();

    // Write a crash report manually (e.g. for caught exceptions).
    // Returns path to the written file, or empty string on failure.
    [[nodiscard]] static std::string write_report(const char* reason,
                                                  const char* details = nullptr);

    // Set a pre-crash callback (called before writing report, from signal handler).
    // IMPORTANT: callback must be async-signal-safe on Linux.
    // Keep callbacks simple -- no heap allocation, no stdio.
    using PreCrashCallback = void(*)();
    static void set_pre_crash_callback(PreCrashCallback cb);

    // Returns true if handlers are currently installed.
    [[nodiscard]] static bool is_installed() noexcept;

    // Returns path of the last written crash report (empty if none).
    [[nodiscard]] static std::string last_report_path();
};

} // namespace uz
