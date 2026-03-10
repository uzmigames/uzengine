#pragma once

/// UzEngine CPU Profiler & Stat System
///
/// Two-tier profiling approach:
/// - **Profiling**: RAII scope-based timing via Tracy integration (development)
/// - **Stats**: Atomic counters for frame-time metrics (always-on)
///
/// # Tracy Integration
/// When `UZ_PROFILING_ENABLED` and Tracy is installed:
/// - Automatically tracks function scope entry/exit
/// - Builds call graph with frame markers
/// - Live profiling via Tracy server (network connect)
/// - Per-frame breakdown of time spent
///
/// # Stat Counters
/// Lightweight always-available statistics:
/// - Draw calls, triangles, memory allocations, cache hits
/// - Thread-safe atomic increments (zero lock contention)
/// - Reported at frame end or on-demand
///
/// # Example
/// ```cpp
/// void RenderScene() {
///     UZ_PROFILE_FUNCTION();  // Tracy tracks this scope
///     for (auto& obj : scene) {
///         UZ_PROFILE_SCOPE(RenderObject);  // Sub-scope
///         RenderMesh(obj);
///         g_statDrawCalls.Add(1);  // Increment stat
///     }
/// }
/// UZ_PROFILE_FRAME_MARK();  // Mark end of frame to Tracy
/// ```

#include <atomic>
#include <cstdint>
#include <cstring>

// ────────────────────────────────────────────────────────────────────────────
/// Tracy Integration (Development Profiling)
///
/// When UZ_PROFILING_ENABLED and <tracy/Tracy.hpp> available:
/// All UZ_PROFILE_* macros expand to Tracy calls.
///
/// In shipping or without Tracy:
/// All UZ_PROFILE_* macros are no-ops (zero overhead).
// ────────────────────────────────────────────────────────────────────────────

#if defined(UZ_PROFILING_ENABLED) && __has_include(<tracy/Tracy.hpp>)
    #include <tracy/Tracy.hpp>

    /// Mark the start of a named scope. Automatically marks exit on return/throw.
    /// Use UZ_PROFILE_SCOPE(MyFunction) to create a ZoneScopedN("MyFunction") RAII guard.
    #define UZ_PROFILE_SCOPE(Name)        ZoneScopedN(#Name)

    /// Mark the current function as a profiled scope.
    /// Equivalent to UZ_PROFILE_SCOPE but auto-names from __func__.
    #define UZ_PROFILE_FUNCTION()         ZoneScoped

    /// Signal end of frame to Tracy profiler.
    /// Call once per frame to create frame boundaries in timeline.
    #define UZ_PROFILE_FRAME_MARK()       FrameMark

    /// Set thread name in Tracy for identification.
    /// Helpful for debugging multi-threaded code.
    #define UZ_PROFILE_THREAD(Name)       tracy::SetThreadName(Name)

    /// Plot a numerical value to Tracy timeline (graph).
    /// Useful for tracking frame time, memory, temperature, etc.
    #define UZ_PROFILE_VALUE(Name, Val)   TracyPlot(Name, Val)

    /// Annotate current zone with text (shows in timeline).
    /// Useful for logging errors or state within a profiled zone.
    #define UZ_PROFILE_TAG(Text)          ZoneText(Text, std::strlen(Text))
#else
    /// No-op: profiling disabled or Tracy not installed.
    #define UZ_PROFILE_SCOPE(Name)        ((void)0)
    /// No-op: profiling disabled or Tracy not installed.
    #define UZ_PROFILE_FUNCTION()         ((void)0)
    /// No-op: profiling disabled or Tracy not installed.
    #define UZ_PROFILE_FRAME_MARK()       ((void)0)
    /// No-op: profiling disabled or Tracy not installed.
    #define UZ_PROFILE_THREAD(Name)       ((void)0)
    /// No-op: profiling disabled or Tracy not installed.
    #define UZ_PROFILE_VALUE(Name, Val)   ((void)0)
    /// No-op: profiling disabled or Tracy not installed.
    #define UZ_PROFILE_TAG(Text)          ((void)0)
#endif

// ────────────────────────────────────────────────────────────────────────────
/// Stat Counters (Always Available)
///
/// Lightweight per-frame statistics with atomic increments.
/// No locks, minimal overhead, always enabled.
// ────────────────────────────────────────────────────────────────────────────

namespace uz {

/// Stat counter group (for organization/filtering in UI).
struct StatGroup {
    const char* name;  ///< Group name (e.g., "Render", "Physics")
};

/// Atomic counter for a statistic (e.g., draw calls, memory allocation).
///
/// Thread-safe for increments via atomic operations.
/// Reset each frame or query on-demand.
struct StatCounter {
    const char* name;
    const StatGroup* group;
    std::atomic<int64_t> value{0};

    void Increment(int64_t delta = 1) { value.fetch_add(delta, std::memory_order_relaxed); }
    void Reset() { value.store(0, std::memory_order_relaxed); }
    int64_t Get() const { return value.load(std::memory_order_relaxed); }
};

/// Built-in stat groups.
extern StatGroup StatRendering;
extern StatGroup StatRHI;
extern StatGroup StatPhysics;
extern StatGroup StatScene;
extern StatGroup StatMemory;
extern StatGroup StatIO;

} // namespace uz

#define UZ_DECLARE_STAT(Group, Name) \
    extern ::uz::StatCounter Stat_##Group##_##Name

#define UZ_DEFINE_STAT(Group, Name) \
    ::uz::StatCounter Stat_##Group##_##Name { #Name, &::uz::Stat##Group }

#define UZ_STAT_INCREMENT(Group, Name, ...) \
    Stat_##Group##_##Name.Increment(__VA_ARGS__ + 0)

#define UZ_STAT_RESET(Group, Name) \
    Stat_##Group##_##Name.Reset()

#define UZ_STAT_GET(Group, Name) \
    Stat_##Group##_##Name.Get()
