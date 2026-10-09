# Observability — GPU Profiling

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/engine/rhi/dx12/dx12_gpu_profiler.h/.cpp`
> **Depends on**: RHI DX12 backend (command lists, query heaps)

## Purpose

CPU profiling tells you where the CPU spends time. GPU profiling tells you where the *GPU* spends time — and since the GPU runs asynchronously, its bottlenecks are often invisible to CPU-side timing. The GPU profiler records timestamp queries around rendering passes and resolves them to millisecond timings.

## Architecture

GPU timestamps use a triple-buffered readback pattern to avoid CPU stalls:

```
Frame N:     Record timestamps → Resolve to readback buffer [N % 3]
Frame N+1:   (GPU still processing frame N's resolve)
Frame N+2:   Map readback buffer [(N-2) % 3] → Read results as milliseconds
```

This 2-frame delay hides GPU latency entirely — the CPU never waits for timestamp results.

```
┌─────────────────────────────────────────────────────┐
│ ID3D12QueryHeap (TIMESTAMP, 128 queries per frame)  │
│                                                     │
│  Frame 0: [begin₀][end₀][begin₁][end₁]...          │
│  Frame 1: [begin₀][end₀][begin₁][end₁]...          │
│  Frame 2: [begin₀][end₀][begin₁][end₁]...          │
├─────────────────────────────────────────────────────┤
│ Readback Buffer (3 × 128 × 8 bytes)                │
│  Mapped by CPU 2 frames after write                 │
└─────────────────────────────────────────────────────┘
```

## API

```cpp
DX12GpuProfiler profiler;
profiler.Init(device);  // Creates query heap + readback buffer

// ── Per frame ─────────────────────────────────────────
profiler.BeginFrame(cmdList);

profiler.BeginTimestamp(cmdList, "GBuffer");
// ... GBuffer pass ...
profiler.EndTimestamp(cmdList, "GBuffer");

profiler.BeginTimestamp(cmdList, "Lighting");
// ... Lighting pass ...
profiler.EndTimestamp(cmdList, "Lighting");

profiler.BeginTimestamp(cmdList, "PostProcess");
// ... Post-processing ...
profiler.EndTimestamp(cmdList, "PostProcess");

profiler.EndFrame(cmdList);  // Resolves queries, advances frame

// ── After fence wait (frame N-2 complete) ─────────────
profiler.ResolveFrame();

for (auto& result : profiler.GetResults()) {
    UZ_LOG(RHI, Debug, "  {} = {:.2f} ms", result.name, result.ms);
}
UZ_LOG(RHI, Debug, "GPU total: {:.2f} ms", profiler.GetFrameTimeMs());
```

### Output Example

```
[RHI Debug]   GBuffer     = 1.23 ms
[RHI Debug]   Lighting    = 0.87 ms
[RHI Debug]   PostProcess = 0.34 ms
[RHI Debug] GPU total: 2.44 ms
```

## How Timestamps Work

1. `BeginTimestamp()` calls `EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, beginIndex)` — records the current GPU clock
2. `EndTimestamp()` calls `EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, endIndex)`
3. `EndFrame()` calls `ResolveQueryData()` — copies timestamp values from the query heap to the readback buffer
4. `ResolveFrame()` maps the readback buffer and computes: `ms = (endTick - beginTick) / gpuFrequency * 1000.0`

The GPU frequency is queried once at init via `ID3D12CommandQueue::GetTimestampFrequency()`.

## Limits

| Parameter | Value |
|-----------|-------|
| Max timestamp pairs per frame | 64 (`kMaxGpuTimestamps`) |
| Readback latency | 3 frames: the slot resolved at the end of frame N belongs to frame N-3, whose fence the CPU waited before recording frame N |
| Ring slots | 4 (`kProfilerFramesInFlight` = command-list frames in flight + 1; a 3-deep ring resolved frame N-2 while it could still execute, until 2026-10-05) |
| Pass name | copied into the slot (`kGpuProfilerNameCapacity` 48), so a slot is labelled with its own frame's passes |
| Precision | Nanoseconds (GPU-dependent) |
| Overhead | ~2 timestamp queries per profiled scope (~negligible) |

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| GPU timing | `FGPUProfiler` integrated with Render Dependency Graph | No built-in (use RenderDoc) | `DX12GpuProfiler` (per-pass timestamps) |
| Readback strategy | Multi-frame ring buffer, 4+ frames latency | N/A | 4-slot ring, 3 frames latency, resolved after the frame's fence |
| Integration | Automatic via RDG pass system | Manual | Automatic: `RgBuilder::Execute` brackets every render-graph pass; scopes outside the graph (RT build, cloud compute, atmosphere bake, VFX) call the device profiler directly |
| Output | Unreal Insights timeline + CSV | N/A | `--diag` report `timing` (mean / min / max ms and samples per pass, `_frame` span), `--run-manifest` `gpu_ms`, per-frame log with `r.GpuProfiler.Log` |

**Done** (2026-10-05, lighting block B item 1): the render graph inserts the timestamps around every pass, `uzengine.exe --diag` enables the profiler and reports the statistics of its measured window (`--timing-first`, default = after the first quarter of the diagnostic run), and `--run-manifest` / `--gpu-profile` enable it too. Guard: `scripts/test_diag_timing.py`.

## Rules

1. **ALWAYS** pair `BeginTimestamp`/`EndTimestamp` — unpaired timestamps produce garbage results
2. **ALWAYS** call `ResolveFrame()` only after the corresponding fence has completed — mapping an in-flight readback buffer is undefined behavior
3. **NEVER** profile more than 64 scopes per frame — exceeding `kMaxGpuTimestamps` silently drops timestamps
4. **ALWAYS** use string literals for render-graph pass names — `RgBuilder::AddPass` stores the pointer (the profiler itself copies the name into its slot since 2026-10-05, but the graph still reads the pointer at execution)
