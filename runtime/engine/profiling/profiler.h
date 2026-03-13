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
#define UZ_PROFILE_SCOPE(Name) ZoneScopedN(#Name)

/// Mark the current function as a profiled scope.
/// Equivalent to UZ_PROFILE_SCOPE but auto-names from __func__.
#define UZ_PROFILE_FUNCTION() ZoneScoped

/// Signal end of frame to Tracy profiler.
/// Call once per frame to create frame boundaries in timeline.
#define UZ_PROFILE_FRAME_MARK() FrameMark

/// Set thread name in Tracy for identification.
/// Helpful for debugging multi-threaded code.
#define UZ_PROFILE_THREAD(Name) tracy::SetThreadName(Name)

/// Plot a numerical value to Tracy timeline (graph).
/// Useful for tracking frame time, memory, temperature, etc.
#define UZ_PROFILE_VALUE(Name, Val) TracyPlot(Name, Val)

/// Annotate current zone with text (shows in timeline).
/// Useful for logging errors or state within a profiled zone.
#define UZ_PROFILE_TAG(Text) ZoneText(Text, std::strlen(Text))

#else
/// No-op: profiling disabled or Tracy not installed.
#define UZ_PROFILE_SCOPE(Name) ((void)0)
/// No-op: profiling disabled or Tracy not installed.
#define UZ_PROFILE_FUNCTION() ((void)0)
/// No-op: profiling disabled or Tracy not installed.
#define UZ_PROFILE_FRAME_MARK() ((void)0)
/// No-op: profiling disabled or Tracy not installed.
#define UZ_PROFILE_THREAD(Name) ((void)0)
/// No-op: profiling disabled or Tracy not installed.
#define UZ_PROFILE_VALUE(Name, Val) ((void)0)
/// No-op: profiling disabled or Tracy not installed.
#define UZ_PROFILE_TAG(Text) ((void)0)
#endif

// ────────────────────────────────────────────────────────────────────────────
// GPU Profiling — RAII timestamp scope
// ────────────────────────────────────────────────────────────────────────────

namespace uz {

/// RAII guard that records a GPU begin timestamp on construction and end timestamp on destruction.
///
/// Templated to avoid including backend-specific headers. Works with any profiler type
/// that provides `BeginTimestamp(CmdList*, const char*)` and `EndTimestamp(CmdList*, const char*)`.
///
/// # Template Parameters
/// * `ProfilerT` — GPU profiler type (e.g., `DX12GpuProfiler`).
/// * `CmdListT` — Command list type (e.g., `ID3D12GraphicsCommandList`).
///
/// # Example
/// ```cpp
/// GpuProfileScope<DX12GpuProfiler, ID3D12GraphicsCommandList> scope(profiler, cmdList, "GBuffer");
/// ```
template <typename ProfilerT, typename CmdListT>
struct GpuProfileScope {
    /// Begins a GPU timestamp with the given name.
    ///
    /// # Arguments
    /// * `profiler` — GPU profiler instance (must outlive this scope).
    /// * `cmdList` — Active command list for timestamp recording.
    /// * `name` — Scope name (string literal, not copied).
    GpuProfileScope(ProfilerT& profiler, CmdListT* cmdList, const char* name)
        : m_profiler(profiler), m_cmdList(cmdList), m_name(name) {
        m_profiler.BeginTimestamp(m_cmdList, m_name);
    }

    /// Records the end timestamp on scope exit.
    ~GpuProfileScope() {
        m_profiler.EndTimestamp(m_cmdList, m_name);
    }

    GpuProfileScope(const GpuProfileScope&) = delete;
    GpuProfileScope& operator=(const GpuProfileScope&) = delete;

private:
    ProfilerT& m_profiler;
    CmdListT* m_cmdList;
    const char* m_name;
};

} // namespace uz

#if defined(UZ_PROFILING_ENABLED)

/// RAII GPU timestamp scope — records begin timestamp on construction, end on destruction.
/// Use within a frame between BeginFrame/EndFrame on a DX12GpuProfiler.
///
/// # Arguments
/// * `Profiler` — GPU profiler instance reference.
/// * `CmdList` — Active command list pointer.
/// * `Name` — Scope name (string literal).
#define UZ_PROFILE_GPU_SCOPE(Profiler, CmdList, Name) \
    ::uz::GpuProfileScope _gpuScope_##__LINE__((Profiler), (CmdList), (Name))

#else
/// No-op: profiling disabled.
#define UZ_PROFILE_GPU_SCOPE(Profiler, CmdList, Name) ((void)0)
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
    const char* name; ///< Group name (e.g., "Render", "Physics")
};

/// Atomic counter for a single statistic (e.g., draw calls, memory allocations).
///
/// Thread-safe for increments via `std::atomic` with relaxed ordering (minimal overhead).
/// Typically reset each frame and queried for debug overlay or telemetry.
///
/// # Thread Safety
/// All methods are thread-safe. Multiple threads can increment concurrently.
///
/// # Examples
/// ```cpp
/// UZ_DEFINE_STAT(Rendering, DrawCalls);
/// // In render loop:
/// UZ_STAT_INCREMENT(Rendering, DrawCalls);
/// // At frame end:
/// int64_t count = UZ_STAT_GET(Rendering, DrawCalls);
/// UZ_STAT_RESET(Rendering, DrawCalls);
/// ```
struct StatCounter {
    const char* name;            ///< Human-readable counter name (e.g., "DrawCalls")
    const StatGroup* group;      ///< Owning group for categorization
    std::atomic<int64_t> value{0}; ///< Current counter value

    /// Atomically adds `delta` to the counter (default: +1).
    ///
    /// # Arguments
    /// * `delta` - Amount to add (can be negative for decrements)
    void Increment(int64_t delta = 1) { value.fetch_add(delta, std::memory_order_relaxed); }

    /// Resets the counter to zero. Typically called at frame boundaries.
    void Reset() { value.store(0, std::memory_order_relaxed); }

    /// Returns the current counter value.
    ///
    /// # Returns
    /// Snapshot of the counter (may be stale if other threads are incrementing).
    [[nodiscard]] int64_t Get() const { return value.load(std::memory_order_relaxed); }
};

/// Built-in stat group for rendering subsystem (draw calls, triangles, state changes).
extern StatGroup StatRendering;
/// Built-in stat group for RHI backend (GPU fence waits, descriptor heap usage).
extern StatGroup StatRHI;
/// Built-in stat group for physics subsystem (collisions, constraint solves).
extern StatGroup StatPhysics;
/// Built-in stat group for scene/ECS (entity count, component queries).
extern StatGroup StatScene;
/// Built-in stat group for memory system (allocations, deallocations, pool usage).
extern StatGroup StatMemory;
/// Built-in stat group for I/O subsystem (file reads, asset loads).
extern StatGroup StatIO;

} // namespace uz

/// Declares a stat counter in a header file.
///
/// # Arguments
/// * `Group` - Stat group name (e.g., `Rendering`, `RHI`, `Memory`)
/// * `Name` - Counter name (e.g., `DrawCalls`, `Allocations`)
#define UZ_DECLARE_STAT(Group, Name) extern ::uz::StatCounter Stat_##Group##_##Name

/// Defines a stat counter in a source file.
///
/// # Arguments
/// * `Group` - Stat group name (must match a `StatGroup` variable named `Stat<Group>`)
/// * `Name` - Counter name (must match a prior `UZ_DECLARE_STAT`)
#define UZ_DEFINE_STAT(Group, Name)                                                                \
    ::uz::StatCounter Stat_##Group##_##Name {                                                      \
        #Name, &::uz::Stat##Group                                                                  \
    }

/// Atomically increments a stat counter. Accepts optional delta (default: +1).
///
/// # Arguments
/// * `Group` - Stat group name
/// * `Name` - Counter name
/// * `...` - Optional delta value (default: 1)
#define UZ_STAT_INCREMENT(Group, Name, ...) Stat_##Group##_##Name.Increment(__VA_ARGS__ + 0)

/// Resets a stat counter to zero. Typically called at frame boundaries.
///
/// # Arguments
/// * `Group` - Stat group name
/// * `Name` - Counter name
#define UZ_STAT_RESET(Group, Name) Stat_##Group##_##Name.Reset()

/// Returns the current value of a stat counter.
///
/// # Arguments
/// * `Group` - Stat group name
/// * `Name` - Counter name
///
/// # Returns
/// `int64_t` snapshot of the counter value.
#define UZ_STAT_GET(Group, Name) Stat_##Group##_##Name.Get()

// ────────────────────────────────────────────────────────────────────────────
/// Built-in Stat Counter Declarations
///
/// Pre-declared counters for common engine metrics. Define (UZ_DEFINE_STAT)
/// in profiler.cpp. Wire UZ_STAT_INCREMENT at callsites.
// ────────────────────────────────────────────────────────────────────────────

/// Number of draw calls issued per frame.
UZ_DECLARE_STAT(Rendering, DrawCalls);
/// Number of triangles submitted per frame.
UZ_DECLARE_STAT(Rendering, Triangles);
/// Number of GPU buffer uploads per frame (vertex, index, constant).
UZ_DECLARE_STAT(IO, BufferUploads);
/// Number of shader compilations (DXC or FXC) this session.
UZ_DECLARE_STAT(RHI, ShaderCompiles);
/// Number of shader cache hits this session.
UZ_DECLARE_STAT(RHI, CacheHits);
/// Number of shader cache misses this session.
UZ_DECLARE_STAT(RHI, CacheMisses);
/// Number of VFS file reads per frame.
UZ_DECLARE_STAT(IO, VFSReads);
/// Number of PSO/pipeline state creations this session.
UZ_DECLARE_STAT(RHI, PipelineCreations);
