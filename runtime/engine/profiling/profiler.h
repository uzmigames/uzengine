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
///
/// Creates a Tracy ZoneScopedN RAII guard with the given name. Zone lifts on scope exit or exception throw.
/// Expands to zero-cost `((void)0)` when profiling is disabled.
///
/// # Arguments
/// * `Name` — Identifier (not a string literal) used for zone naming
///
/// # Examples
/// ```cpp
/// void RenderFrame() {
///     UZ_PROFILE_SCOPE(RenderFrame);
///     // Zone recorded in Tracy timeline
/// }
/// ```
#define UZ_PROFILE_SCOPE(Name) ZoneScopedN(#Name)

/// Mark the current function as a profiled scope.
///
/// Equivalent to UZ_PROFILE_SCOPE but auto-generates zone name from `__func__`.
/// Expands to zero-cost `((void)0)` when profiling is disabled.
///
/// # Examples
/// ```cpp
/// void ProcessPhysics() {
///     UZ_PROFILE_FUNCTION();  // Zone named "ProcessPhysics"
/// }
/// ```
#define UZ_PROFILE_FUNCTION() ZoneScoped

/// Signal end of frame to Tracy profiler.
///
/// Call once per frame to create frame boundaries and enable frame-relative analysis in the timeline.
/// Expands to zero-cost `((void)0)` when profiling is disabled.
#define UZ_PROFILE_FRAME_MARK() FrameMark

/// Set thread name in Tracy for identification.
///
/// Sets the current thread's name visible in the Tracy timeline for easier thread identification.
/// Expands to zero-cost `((void)0)` when profiling is disabled.
///
/// # Arguments
/// * `Name` — Thread name string literal (e.g., "RenderThread", "JobWorker-3")
#define UZ_PROFILE_THREAD(Name) tracy::SetThreadName(Name)

/// Plot a numerical value to Tracy timeline (appears as a time-series graph).
///
/// Low-overhead data point for graphing frame-relative metrics. Data is collected and displayed
/// as a time-series plot in the Tracy UI (frame time, memory usage, GPU utilization, etc.).
/// Expands to zero-cost `((void)0)` when profiling is disabled.
///
/// # Arguments
/// * `Name` — Identifier (not a string literal) for the graph series
/// * `Val` — Numeric value to plot (int or float)
#define UZ_PROFILE_VALUE(Name, Val) TracyPlot(Name, Val)

/// Annotate current zone with text (displayed in timeline).
///
/// Adds a text annotation to the currently active Tracy zone. Text is copied immediately.
/// Useful for logging error codes, state names, or conditional diagnostics within a profiled region.
/// Expands to zero-cost `((void)0)` when profiling is disabled.
///
/// # Arguments
/// * `Text` — C-string (non-owning; copied immediately)
///
/// # Examples
/// ```cpp
/// UZ_PROFILE_SCOPE(ShaderCompile);
/// if (error) {
///     UZ_PROFILE_TAG("compilation failed");  // Annotates the zone
/// }
/// ```
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
    ///
    /// # Side Effects
    /// Calls `profiler.EndTimestamp(cmdList, name)`. Errors are silent (no exception).
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

/// Categorization namespace for stat counters.
///
/// Used to organize and filter related statistics in debug UI and telemetry reports.
/// Built-in groups include `StatRendering`, `StatRHI`, `StatPhysics`, `StatScene`, `StatMemory`, `StatIO`.
///
/// # Examples
/// ```cpp
/// // Reference in stat macro:
/// UZ_DECLARE_STAT(Rendering, DrawCalls);  // Uses StatRendering group
/// ```
struct StatGroup {
    const char* name; ///< Group name (e.g., "Rendering", "RHI", "Physics") — used for UI categorization
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
    const char* name;            ///< Counter name matching UZ_DECLARE_STAT(Group, Name) expansion. Use CamelCase (e.g., "DrawCalls")
    const StatGroup* group;      ///< Parent group for UI filtering. Set by UZ_DEFINE_STAT; must never be nullptr
    std::atomic<int64_t> value{0}; ///< Current counter value (incremented atomically, may be stale)

    /// Atomically adds `delta` to the counter (default: +1).
    ///
    /// # Arguments
    /// * `delta` - Amount to add (can be negative for decrements)
    ///
    /// # Thread Safety
    /// Thread-safe for concurrent calls via relaxed atomic ordering. Zero lock contention.
    void Increment(int64_t delta = 1) { value.fetch_add(delta, std::memory_order_relaxed); }

    /// Resets the counter to zero (typically at frame boundaries).
    ///
    /// # Thread Safety
    /// Safe concurrent with Increment/Get. May cause a snapshot between increment and reset to be missed.
    void Reset() { value.store(0, std::memory_order_relaxed); }

    /// Returns the current counter value.
    ///
    /// # Returns
    /// Snapshot of the counter (may be stale if other threads are incrementing concurrently).
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
/// Built-in stat group for GPU memory tracking (allocations, frees, pressure events).
extern StatGroup StatGPU;

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
/// Number of compute queue fence signals per frame.
UZ_DECLARE_STAT(RHI, ComputeQueueSignals);
/// Number of VFS file reads per frame.
UZ_DECLARE_STAT(IO, VFSReads);
/// Number of PSO/pipeline state creations this session.
UZ_DECLARE_STAT(RHI, PipelineCreations);
/// Number of PSO cache hits (pipeline reuse) this session.
UZ_DECLARE_STAT(RHI, PsoCacheHits);
/// Number of PSO cache misses (new pipeline creation) this session.
UZ_DECLARE_STAT(RHI, PsoCacheMisses);
/// Number of GPU memory allocations tracked this session.
UZ_DECLARE_STAT(GPU, MemoryAllocations);
/// Number of GPU memory frees tracked this session.
UZ_DECLARE_STAT(GPU, MemoryFrees);
/// Number of memory pressure events fired (category over budget).
UZ_DECLARE_STAT(GPU, MemoryPressureEvents);
/// Number of streaming mip levels loaded (uploaded to GPU).
UZ_DECLARE_STAT(GPU, StreamingMipLoads);
/// Number of streaming mip levels evicted (freed from GPU memory).
UZ_DECLARE_STAT(GPU, StreamingMipEvictions);
/// Number of defrag pass runs triggered by fragmentation exceeding threshold.
UZ_DECLARE_STAT(GPU, DefragPassRuns);
/// Number of staging batch submissions (copy queue flushes).
UZ_DECLARE_STAT(IO, StagingBatchSubmissions);
/// Number of Vulkan descriptor set allocations from the global pool.
UZ_DECLARE_STAT(RHI, VkDescriptorAllocations);
/// Number of Vulkan descriptor pool resets (full pool exhaustion recovery).
UZ_DECLARE_STAT(RHI, VkDescriptorPoolResets);
/// Number of individual vkUpdateDescriptorSets calls per frame.
UZ_DECLARE_STAT(RHI, VkDescriptorWrites);
/// Number of frames that exceeded the target frame time (cumulative).
UZ_DECLARE_STAT(Rendering, DroppedFrames);
/// Async compute overlap ratio (0–100, scaled from 0.0–1.0 for integer stat).
UZ_DECLARE_STAT(RHI, AsyncComputeOverlap);
/// Number of mutex contention events in parallel command list acquisition.
UZ_DECLARE_STAT(RHI, CmdListContentionEvents);
/// Number of swap chain buffer count changes triggered by auto-tuning.
UZ_DECLARE_STAT(Rendering, SwapChainResizes);
/// Number of async compute mipgen dispatches per frame.
UZ_DECLARE_STAT(GPU, AsyncMipgenDispatches);
/// Number of draw list compaction dispatches per frame.
UZ_DECLARE_STAT(GPU, DrawCompactDispatches);
/// Number of mesh shader (meshlet) dispatches per frame.
UZ_DECLARE_STAT(RHI, MeshletDispatches);
