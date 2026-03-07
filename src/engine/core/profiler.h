#pragma once

// ============================================================================
// profiler.h — Lightweight built-in CPU profiler for UzEngine.
//
// Usage:
//   void Renderer::render() {
//       UZ_PROFILE_FUNCTION();                   // profiles entire function
//       {
//           UZ_PROFILE_ZONE("shadow_pass");      // profiles a sub-scope
//           drawShadows();
//       }
//       UZ_PROFILE_FRAME_MARK();                 // marks frame boundary
//   }
//
//   // Print report to stderr (sorted by total time):
//   uz::profiler_dump();
//
//   // Auto-dump only when a scope exceeds a threshold:
//   uz::profiler_dump_if_slow(16.0);  // dump if any scope > 16ms
//
// Overhead:
//   ~40ns per scope (QPC read + atomic add). Zero when disabled.
//   No heap allocations in the hot path.
//
// Thread safety:
//   All functions are thread-safe. Lock-free recording for known scopes.
//   A mutex is only taken when a NEW scope name is seen for the first time.
//
// Tracy integration:
//   When UZ_PROFILING=1 and Tracy is available, the macros delegate to Tracy.
//   Otherwise the built-in profiler is used.
// ============================================================================

#include <cstdint>

#if defined(UZ_PROFILING) && UZ_PROFILING && __has_include(<tracy/Tracy.hpp>)
// ---- Tracy backend ----
#include <tracy/Tracy.hpp>
#define UZ_PROFILE_FUNCTION()           ZoneScoped
#define UZ_PROFILE_ZONE(name)           ZoneScopedN(name)
#define UZ_PROFILE_FRAME_MARK()         FrameMark
#define UZ_PROFILE_VALUE(name, val)     TracyPlot(name, (double)(val))
#define UZ_PROFILE_MESSAGE(str, len)    TracyMessage(str, len)

#else
// ---- Built-in profiler ----

namespace uz {

// ---- Stats snapshot for a single scope ----
struct ProfileStats {
    const char* name      = nullptr;
    uint32_t call_count   = 0;
    double total_ms       = 0.0;
    double min_ms         = 0.0;
    double max_ms         = 0.0;
    double avg_ms         = 0.0;
    double last_frame_ms  = 0.0;
};

// ---- Core API ----

/// High-resolution tick counter (QPC on Windows).
int64_t profiler_now();

/// Convert tick delta to milliseconds.
double profiler_ticks_to_ms(int64_t ticks);

/// Record a completed scope timing. Called by ProfileZone destructor.
void profiler_record(const char* name, int64_t start_ticks);

/// Mark a frame boundary — snapshots per-frame accumulators.
void profiler_frame_mark();

/// Print a sorted report to stderr.
void profiler_dump();

/// Print report ONLY if any scope's last-frame time exceeded threshold_ms.
/// Returns true if a dump was triggered.
bool profiler_dump_if_slow(double threshold_ms);

/// Get stats for all recorded scopes. Returns actual count written.
int profiler_get_stats(ProfileStats* out, int max_count);

/// Get the number of active scope entries.
int profiler_scope_count();

/// Reset all accumulated stats.
void profiler_reset();

/// Enable/disable profiling at runtime.
void profiler_set_enabled(bool enabled);

/// Check if profiling is currently enabled.
bool profiler_enabled();

// ---- RAII scope timer ----
class ProfileZone {
public:
    explicit ProfileZone(const char* name) : name_(name) {
        if (name_) start_ = profiler_now();
    }
    ~ProfileZone() {
        if (name_) profiler_record(name_, start_);
    }
    ProfileZone(const ProfileZone&)            = delete;
    ProfileZone& operator=(const ProfileZone&) = delete;
private:
    const char* name_  = nullptr;
    int64_t     start_ = 0;
};

} // namespace uz

// ---- Macros ----
#define UZ_PROFILE_CAT_(a, b) a##b
#define UZ_PROFILE_CAT(a, b) UZ_PROFILE_CAT_(a, b)

#define UZ_PROFILE_ZONE(name) \
    ::uz::ProfileZone UZ_PROFILE_CAT(_uz_pz_, __LINE__)(::uz::profiler_enabled() ? (name) : nullptr)

#define UZ_PROFILE_FUNCTION() \
    UZ_PROFILE_ZONE(__FUNCTION__)

#define UZ_PROFILE_FRAME_MARK() \
    ::uz::profiler_frame_mark()

#define UZ_PROFILE_VALUE(name, val) ((void)0)

#define UZ_PROFILE_MESSAGE(str, len) ((void)0)

#endif // UZ_PROFILING / built-in
