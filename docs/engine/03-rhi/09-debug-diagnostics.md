# RHI — Debug & Diagnostics

> **Status**: Done (Milestone 1.2)
> **Source**: `dx12/dx12_gpu_profiler.h/.cpp`, `dx12/dx12_gpu_breadcrumbs.h/.cpp`, `dx12_device.h` (DRED)
> **Depends on**: DX12 backend

## Purpose

GPU debugging is fundamentally harder than CPU debugging — there's no step debugger, no stack traces, and crashes manifest as device removal with little context. UzEngine builds three diagnostic systems directly into the RHI to catch and diagnose GPU issues:

1. **GPU Timestamp Profiler** — per-pass timing in milliseconds
2. **GPU Breadcrumbs** — crash-site identification via marker trail
3. **DRED** — Device Removed Extended Data (auto-breadcrumbs + page fault info)

## GPU Timestamp Profiler

Records begin/end timestamps around rendering passes using `ID3D12QueryHeap`. Triple-buffered: frame N writes timestamps, frame N-2's results are available for CPU readback.

### Architecture

```
Frame N:     BeginTimestamp("GBuffer") → EndTimestamp("GBuffer")
             BeginTimestamp("Lighting") → EndTimestamp("Lighting")
             ResolveQueryData → readback buffer [frame N % 3]
                 │
Frame N+2:   ResolveFrame() → maps readback buffer [frame (N-2) % 3]
                 │
                 ▼
             GpuTimingResult { name="GBuffer",  ms=1.2 }
             GpuTimingResult { name="Lighting", ms=0.8 }
```

### API

```cpp
DX12GpuProfiler profiler;
profiler.Init(device);

// Per frame:
profiler.BeginFrame(cmdList);

profiler.BeginTimestamp(cmdList, "GBuffer");
// ... GBuffer pass ...
profiler.EndTimestamp(cmdList, "GBuffer");

profiler.BeginTimestamp(cmdList, "Lighting");
// ... Lighting pass ...
profiler.EndTimestamp(cmdList, "Lighting");

profiler.EndFrame(cmdList);

// After fence wait (frame N-2 completed):
profiler.ResolveFrame();
for (auto& result : profiler.GetResults()) {
    UZ_LOG(RHI, Debug, "  {} = {:.2f} ms", result.name, result.ms);
}
float totalGpu = profiler.GetFrameTimeMs();
```

### Limits

- **64 timestamp pairs** per frame (`kMaxGpuTimestamps`)
- Results lag by 2 frames (triple-buffered readback)
- Timing resolution depends on GPU timestamp frequency (typically nanosecond precision)

## GPU Breadcrumbs

Writes sequential marker values into a GPU-visible readback buffer via `WriteBufferImmediate`. On device removal, the buffer reveals the last successfully executed marker — pinpointing the exact operation that caused the crash.

### How It Works

```
Mark(cmdList, "BeforeGBuffer")  → writes value 1 to buffer[0]
Mark(cmdList, "AfterGBuffer")   → writes value 2 to buffer[1]
Mark(cmdList, "BeforeLighting") → writes value 3 to buffer[2]
  ← GPU hangs here
Mark(cmdList, "AfterLighting")  → writes value 4 to buffer[3]  (never reached)
```

On device removal, `LogLastMarker()` maps the buffer and finds:
```
[DONE]         BeforeGBuffer   (value: 1)
[DONE]         AfterGBuffer    (value: 2)
[>>> LAST]     BeforeLighting  (value: 3)  ← Last completed marker
[NOT REACHED]  AfterLighting   (value: 4)
```

The crash happened between "BeforeLighting" and "AfterLighting" — the lighting pass caused the hang.

### API

```cpp
DX12GpuBreadcrumbs breadcrumbs;
breadcrumbs.Init(device);

// Per frame:
breadcrumbs.BeginFrame();
breadcrumbs.Mark(cmdList, "BeforeGBuffer");
// ... GBuffer ...
breadcrumbs.Mark(cmdList, "AfterGBuffer");
breadcrumbs.Mark(cmdList, "BeforeLighting");
// ... Lighting ...
breadcrumbs.Mark(cmdList, "AfterLighting");

// On device removal:
breadcrumbs.LogLastMarker();
```

### Limits

- **256 markers** per frame (`kMaxBreadcrumbs`)
- Requires `ID3D12GraphicsCommandList2` (for `WriteBufferImmediate`)
- Dev builds only — controlled by `UZ_GPU_BREADCRUMBS` define

## DRED (Device Removed Extended Data)

Built into the D3D12 runtime. When enabled, the driver automatically records breadcrumbs for every GPU operation and tracks page faults.

### What DRED Provides

1. **Auto-breadcrumb nodes**: List of the last GPU operations before the hang, with operation types (Draw, Dispatch, Copy, Barrier, etc.)
2. **Page fault info**: If the crash was caused by accessing freed or invalid memory, DRED reports the faulting GPU virtual address plus lists of existing and recently freed allocations

### Enabling

DRED is enabled during device initialization when `--gpu-breadcrumbs` is passed or the config enables it:

```cpp
// In DX12Device::Init()
if (config.enableBreadcrumbs) {
    EnableDRED();  // Enables auto-breadcrumbs + page fault tracking
}
```

### LogDeviceRemoved

When `Present()` returns `DXGI_ERROR_DEVICE_REMOVED`, `DX12Device::LogDeviceRemoved()` queries DRED and logs:

```
[RHI Error] Device removed! Querying DRED...
[RHI Error] Auto-breadcrumb: last operation = ExecuteCommandList (Draw)
[RHI Error] Page fault at GPU VA 0x00000200DEADBEEF
[RHI Error]   Existing allocation: "MeshVertexBuffer" (0x200A0000000, 64KB)
[RHI Error]   Recently freed:      "OldTexture" (0x200DEAD0000, 1MB)
```

## Engine Comparison

| Diagnostic | UE5 | Godot | UzEngine |
|------------|-----|-------|----------|
| GPU timing | `FGPUProfiler` (RDG-integrated) | None built-in (relies on RenderDoc) | `DX12GpuProfiler` (per-pass timestamps) |
| GPU breadcrumbs | `D3D12Breadcrumbs` + Aftermath (NVIDIA) | None | `DX12GpuBreadcrumbs` + DRED |
| Crash diagnostics | DRED + NVIDIA Aftermath + Minidumps | Vulkan validation layers | DRED + custom breadcrumbs + minidumps |
| Overhead | High in dev builds (Aftermath captures full state) | Minimal (no built-in GPU diagnostics) | Moderate (breadcrumbs are cheap, timestamps add ~2 queries/pass) |

## Rules

1. **ALWAYS** enable GPU breadcrumbs in dev builds — the cost is negligible and crash diagnostics are invaluable
2. **ALWAYS** call `LogDeviceRemoved()` when `Present()` fails — don't silently crash
3. **ALWAYS** pair `BeginTimestamp`/`EndTimestamp` — unpaired timestamps corrupt results
4. **NEVER** ship with GPU validation or breadcrumbs enabled — they add measurable overhead
