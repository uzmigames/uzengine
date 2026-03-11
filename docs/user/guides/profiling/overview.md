# Profiling & Debugging

UzEngine integrates Tracy for CPU profiling, DX12 debug layers for GPU validation,
and DRED for crash analysis. Every system exposes stat counters for runtime monitoring.

> **Headers**: `core/profiling/profiler.h`, `core/assertions/engine_assert.h`

## CPU Profiling — Tracy

Tracy integration is compile-time toggleable via `TRACY_ENABLE`:

```cpp
void RenderFrame() {
    UZ_PROFILE_SCOPE("RenderFrame");  // Appears in Tracy viewer

    {
        UZ_PROFILE_SCOPE("ShadowPass");
        RenderShadows();
    }
    {
        UZ_PROFILE_SCOPE("GBufferPass");
        RenderGBuffer();
    }
}
```

Connect the [Tracy profiler viewer](https://github.com/wolfpld/tracy) to see
frame timelines, call stacks, and memory allocations.

Tracy is compiled with `TRACY_ON_DEMAND` — no overhead unless the viewer connects.

## Stat Counters

Every system tracks health via atomic stat counters:

```cpp
UZ_STAT_INC(Memory, AllocCount);
UZ_STAT_ADD(Memory, BytesAllocated, size);
UZ_STAT_SET(Renderer, DrawCalls, count);
```

Six stat groups: Core, Memory, Renderer, Physics, Audio, IO.

## Assertions

Three assertion macros for different severities:

| Macro | On failure | Stripped in shipping? |
|-------|-----------|---------------------|
| `UZ_CHECK(expr)` | Logs + stack trace + `abort()` | No — always active |
| `UZ_ENSURE(expr)` | Logs + stack trace + continues | Yes |
| `UZ_VERIFY(expr)` | Logs + stack trace + `abort()` | No — never stripped |

```cpp
UZ_CHECK(device != nullptr);           // Fatal if null
UZ_ENSURE(buffer.size > 0);            // Warning if zero, continues
UZ_VERIFY(SUCCEEDED(hr));              // Fatal, even in release
```

Stack traces use `CaptureStackBackTrace` on Windows with debugger break support.

## GPU Debugging — DX12

### Debug Layer

Enabled by default in debug builds. Validates all DX12 API calls:

```bash
zig build run                          # Debug layer ON
zig build run -Doptimize=ReleaseFast   # Debug layer OFF
```

### GPU-Based Validation

Deep GPU-side checks (significantly slower, catches more bugs):

```bash
zig build run -- --gpu-validation
```

### DRED (Device Removed Extended Data)

Automatically enabled. On device removal (TDR), the engine logs:
- Auto-breadcrumbs: last GPU commands before the crash
- Page fault info: which resource was being accessed

```cpp
// Called automatically on device removal
device->LogDeviceRemoved();
```

### GPU Breadcrumbs

Track command execution progress for crash diagnosis:

```bash
zig build run -- --gpu-breadcrumbs
```

## Structured Logging

All systems log through the category-based logging system:

```cpp
UZ_LOG(RHI, Info, "Device created: adapter='{}', FL='{}'", name, featureLevel);
UZ_LOG(Memory, Warning, "Pool '{}' at 95% capacity ({}/{} bytes)", name, used, total);
UZ_LOG(Engine, Error, "Failed to load scene: path='{}', reason='{}'", path, error);
```

12 log categories: Core, RHI, Renderer, Scene, Physics, Audio, Input,
Platform, Memory, IO, Config, Window, Engine.

Log output goes to:
- **Console** — Colored terminal output
- **File** — Rotating log files (10 MB max, keeps 3)

## Related Pages

- [Architecture](../architecture.md) — System design
- [RHI Overview](../rhi/overview.md) — GPU debugging details
