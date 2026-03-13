# Observability — CPU Profiling

> **Status**: Done
> **Source**: `runtime/engine/profiling/profiler.h`
> **Depends on**: Tracy (optional, `thirdparty/tracy/`)

## Purpose

Know where CPU time is spent — every frame, every function, every subsystem. Without profiling, optimization is guesswork. UzEngine uses Tracy for development profiling (full timeline + flame graph) and lightweight stat counters that are always available.

## Two-Tier Approach

| Tier | When | Overhead | Data |
|------|------|----------|------|
| **Tracy profiling** | Dev builds with `UZ_PROFILING_ENABLED` | ~100ns per scope | Full call graph, timeline, flame graph |
| **Stat counters** | Always (all builds) | ~1ns per increment | Numeric aggregates (draw calls, allocs) |

## Tracy Integration

When `UZ_PROFILING_ENABLED` is defined and Tracy headers are available, all `UZ_PROFILE_*` macros expand to Tracy calls. In shipping or without Tracy, they're zero-cost no-ops.

### Macros

```cpp
UZ_PROFILE_FUNCTION();            // Auto-names from __func__
UZ_PROFILE_SCOPE(RenderGBuffer);  // Named scope
UZ_PROFILE_FRAME_MARK();          // Frame boundary marker
UZ_PROFILE_THREAD("RenderThread"); // Thread name for Tracy UI
UZ_PROFILE_VALUE("FrameTime", dt); // Plot numeric value on timeline
UZ_PROFILE_TAG("Loading albedo");  // Annotate current scope with text
```

### Usage Pattern

```cpp
void Renderer::RenderFrame() {
    UZ_PROFILE_FUNCTION();  // Tracks entire function

    {
        UZ_PROFILE_SCOPE(GBufferPass);
        // ... GBuffer rendering ...
    }

    {
        UZ_PROFILE_SCOPE(LightingPass);
        // ... Lighting ...
    }

    {
        UZ_PROFILE_SCOPE(PostProcess);
        // ... Post-processing ...
    }

    UZ_PROFILE_FRAME_MARK();  // Tells Tracy "frame ended"
}
```

### Tracy Server

Tracy is a real-time profiler — connect the Tracy Profiler GUI to the running engine over a network socket. The GUI shows:

- **Timeline**: All threads, all scopes, with zoom to nanosecond precision
- **Flame graph**: Hierarchical call tree with inclusive/exclusive times
- **Frame view**: Per-frame timing with min/max/avg statistics
- **Memory view**: Allocation tracking (when instrumented)
- **Plots**: Custom numeric values over time

### When Tracy Is Not Available

All macros compile to `((void)0)`. The compiler eliminates them entirely — no function call, no string literal, nothing.

```cpp
// With Tracy: expands to ZoneScopedN("GBufferPass")
// Without Tracy: expands to ((void)0)
UZ_PROFILE_SCOPE(GBufferPass);
```

## Stat Counters

Lightweight always-on statistics using atomic increments. No locks, minimal overhead (~1ns per increment via `std::memory_order_relaxed`).

### API

```cpp
// ── Header ──────────────────────────────────────────
UZ_DECLARE_STAT(Rendering, DrawCalls);
UZ_DECLARE_STAT(Rendering, Triangles);
UZ_DECLARE_STAT(Memory, Allocations);

// ── Source ──────────────────────────────────────────
UZ_DEFINE_STAT(Rendering, DrawCalls);
UZ_DEFINE_STAT(Rendering, Triangles);
UZ_DEFINE_STAT(Memory, Allocations);

// ── Usage ───────────────────────────────────────────
UZ_STAT_INCREMENT(Rendering, DrawCalls);        // +1
UZ_STAT_INCREMENT(Rendering, Triangles, count); // +count

int64_t draws = UZ_STAT_GET(Rendering, DrawCalls);
UZ_STAT_RESET(Rendering, DrawCalls);  // Reset at frame boundary
```

### Built-in Stat Groups

| Group | Subsystem | Typical Counters |
|-------|-----------|-----------------|
| `StatRendering` | Draw pipeline | Draw calls, triangles, state changes |
| `StatRHI` | GPU backend | Fence waits, descriptor usage, barriers |
| `StatPhysics` | Physics simulation | Collisions, constraint solves, raycasts |
| `StatScene` | Entity/Component | Entity count, component queries |
| `StatMemory` | Allocators | Allocations, deallocations, pool usage |
| `StatIO` | File system | File reads, asset loads, cache hits |

### Thread Safety

`StatCounter` uses `std::atomic<int64_t>` with relaxed ordering. Multiple threads can increment the same counter concurrently with no locks and minimal cache-line bouncing (relaxed ordering allows the CPU to coalesce increments).

### Frame Lifecycle

```cpp
// Start of frame: reset all per-frame stats
UZ_STAT_RESET(Rendering, DrawCalls);
UZ_STAT_RESET(Rendering, Triangles);

// During frame: subsystems increment as they work
for (auto& mesh : visibleMeshes) {
    DrawMesh(mesh);
    UZ_STAT_INCREMENT(Rendering, DrawCalls);
    UZ_STAT_INCREMENT(Rendering, Triangles, mesh.triangleCount);
}

// End of frame: read and display
UZ_LOG(Renderer, Debug, "Frame: {} draws, {} tris",
       UZ_STAT_GET(Rendering, DrawCalls),
       UZ_STAT_GET(Rendering, Triangles));
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Profiler | Unreal Insights (custom, extremely powerful) | Built-in profiler (basic) | **Tracy** (external, real-time) |
| Scope macros | `SCOPED_NAMED_EVENT`, `TRACE_CPUPROFILER_EVENT_SCOPE` | None (manual timing) | `UZ_PROFILE_SCOPE`, `UZ_PROFILE_FUNCTION` |
| Stat counters | `DECLARE_STATS_GROUP` / `INC_DWORD_STAT` | `Performance::get_monitor()` | `UZ_DECLARE_STAT` / `UZ_STAT_INCREMENT` |
| Overhead (disabled) | Zero (macro strips) | N/A (always on, lightweight) | Zero (macro strips) |
| Live visualization | Unreal Insights app | In-editor profiler panel | Tracy Profiler app |

UE5's Unreal Insights is more powerful (distributed tracing, server-side profiling, GPU + CPU unified timeline), but it's a massive infrastructure investment. Tracy provides 80% of the value at 1% of the complexity.

## Rules

1. **ALWAYS** use `UZ_PROFILE_FUNCTION()` on every function that touches GPU, disk, or runs per-frame
2. **ALWAYS** use `UZ_PROFILE_SCOPE(Name)` around multi-line blocks within profiled functions
3. **ALWAYS** call `UZ_PROFILE_FRAME_MARK()` exactly once per frame — Tracy uses this to compute frame times
4. **ALWAYS** reset stat counters at frame boundaries — stale values cause misleading metrics
5. **NEVER** profile trivial functions (getters, simple math) — the overhead exceeds the measured time
