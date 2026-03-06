#pragma once

// CrashReporter -- portable signal/exception handler that writes a structured
// crash report to disk before the process terminates.
//
// Usage:
//   CrashReporter::install();   // Call once at engine startup
//   CrashReporter::set_output_dir(".uzengine/crash_reports");
//
// On Linux: installs handlers for SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS.
// On Windows: sets a top-level SEH exception filter via SetUnhandledExceptionFilter.
//
// On crash, generates:
//   latest.json -- Machine-readable structured JSON report (stack, GPU state, etc.)
//   latest.dmp  -- Windows MiniDump for offline debugging.

#include <cstdint>
#include <functional>
#include <string>

namespace uz {

/// Snapshot of engine state, set periodically so crash reports have context.
struct EngineStateSnapshot {
    char scene_path[260]   = {};
    float camera_pos[3]    = {};
    float camera_yaw       = 0.0f;
    float camera_pitch     = 0.0f;
    uint64_t frame_number  = 0;
    char selected_node[128] = {};
};

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

    // Update the engine state snapshot (called from main loop or on scene change).
    // This data is included in crash reports for context.
    static void update_engine_state(const EngineStateSnapshot& state);

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

    // Read the latest.json crash report and return its contents as a string.
    // Returns empty string if no report exists.
    [[nodiscard]] static std::string read_latest_report();

    // Set custom metadata key-value pair for crash context (à la UE5 SetGameData).
    // Thread-safe. Data is included in crash reports under "metadata".
    static void set_metadata(const std::string& key, const std::string& value);

    // Clear all custom metadata.
    static void clear_metadata();
};

} // namespace uz
