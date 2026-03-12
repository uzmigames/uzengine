# Observability — Stat Counters

> **Status**: Done
> **Source**: `runtime/core/profiling/profiler.h` (StatCounter, StatGroup, macros)
> **Depends on**: Nothing (standalone, uses only `<atomic>`)

## Purpose

Stat counters provide lightweight, always-available numeric metrics for every subsystem. Unlike Tracy profiling (which is stripped in shipping), stat counters are always active — they cost ~1 nanosecond per increment and give you instant visibility into engine health: draw calls, triangle counts, memory allocations, cache hit rates, entity counts.

## Design

```cpp
struct StatGroup {
    const char* name;   // "Render", "Physics", "Memory"
};

struct StatCounter {
    const char* name;             // "DrawCalls", "Allocations"
    const StatGroup* group;       // Owning group
    std::atomic<int64_t> value;   // Atomic, relaxed ordering

    void Increment(int64_t delta = 1);  // Thread-safe +delta
    void Reset();                        // Set to 0
    int64_t Get() const;                 // Snapshot read
};
```

### Why Relaxed Ordering

`std::memory_order_relaxed` means:
- No ordering guarantees between threads (cheapest atomic operation)
- On x86, compiles to a plain `lock xadd` (single instruction, ~1ns)
- Perfect for counters where we don't need sequential consistency — we just need the final aggregate at frame end

### Macro API

```cpp
// ── Header ─────────────────────────────────
UZ_DECLARE_STAT(Rendering, DrawCalls);      // extern StatCounter Stat_Rendering_DrawCalls

// ── Source ─────────────────────────────────
UZ_DEFINE_STAT(Rendering, DrawCalls);       // StatCounter { "DrawCalls", &StatRendering }

// ── Usage ──────────────────────────────────
UZ_STAT_INCREMENT(Rendering, DrawCalls);         // +1
UZ_STAT_INCREMENT(Rendering, DrawCalls, 5);      // +5
int64_t n = UZ_STAT_GET(Rendering, DrawCalls);   // Read
UZ_STAT_RESET(Rendering, DrawCalls);             // Zero
```

## Built-in Groups

| Variable | Group Name | Intended Counters |
|----------|-----------|-------------------|
| `StatRendering` | Rendering | DrawCalls, Triangles, StateChanges, MaterialBinds |
| `StatRHI` | RHI | FenceWaits, DescriptorAllocations, Barriers, BufferCreates |
| `StatPhysics` | Physics | Collisions, ConstraintSolves, Raycasts, Bodies |
| `StatScene` | Scene | Entities, ComponentQueries, Transforms |
| `StatMemory` | Memory | Allocations, Deallocations, PoolOccupancy, LinearResets |
| `StatIO` | IO | FileReads, AssetLoads, CacheHits, CacheMisses |

## Typical Usage Pattern

```cpp
// ── Frame start ──────────────────────────────
UZ_STAT_RESET(Rendering, DrawCalls);
UZ_STAT_RESET(Rendering, Triangles);

// ── During frame (multiple threads OK) ───────
void Renderer::DrawMesh(const Mesh& mesh) {
    cmdList->DrawIndexed(mesh.indexCount);
    UZ_STAT_INCREMENT(Rendering, DrawCalls);
    UZ_STAT_INCREMENT(Rendering, Triangles, mesh.indexCount / 3);
}

// ── Frame end ────────────────────────────────
UZ_LOG(Renderer, Debug, "Frame {}: {} draws, {} tris, {:.1f} ms",
    frameNumber,
    UZ_STAT_GET(Rendering, DrawCalls),
    UZ_STAT_GET(Rendering, Triangles),
    frameTimeMs);

// Feed to Tracy plot for live visualization
UZ_PROFILE_VALUE("DrawCalls", static_cast<double>(UZ_STAT_GET(Rendering, DrawCalls)));
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Macro | `INC_DWORD_STAT(STAT_DrawCalls)` | `Performance::get_monitor(RENDER_DRAW_CALLS)` | `UZ_STAT_INCREMENT(Rendering, DrawCalls)` |
| Storage | TLS-based stat collection + aggregation thread | Singleton counters | `std::atomic<int64_t>` per counter |
| Groups | `DECLARE_STATS_GROUP` with hierarchical names | Fixed enum `Performance::Monitor` | `StatGroup` with string name |
| Overhead | Higher (TLS collection + thread flush) | Low | Minimal (~1ns relaxed atomic) |
| Visualization | Stat overlay + Unreal Insights | Performance panel | Log + Tracy plots |

## Rules

1. **ALWAYS** reset per-frame counters at frame boundaries — stale values cause misleading metrics
2. **ALWAYS** define stat counters in the subsystem that owns them — not in a central file
3. **NEVER** use stat counters for high-frequency per-vertex/per-pixel counting — the atomic overhead accumulates
4. **ALWAYS** feed key stats to `UZ_PROFILE_VALUE()` when Tracy is available — enables live timeline graphs
