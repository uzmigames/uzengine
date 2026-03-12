# Observability — Crash Reporting

> **Status**: Partial (assertions done, minidumps planned, full context capture planned)
> **Source**: `runtime/core/assertions/engine_assert.h` (assertions), `runtime/core/rhi/dx12/dx12_gpu_breadcrumbs.h` (GPU crash site), `dx12_device.h` (DRED)
> **Depends on**: Logging, RHI debug diagnostics

## Purpose

Crashes are inevitable during engine development. The difference between a productive debugging session and hours of frustration comes down to crash context — knowing exactly what the engine was doing when it died. UzEngine captures crash context at multiple levels: CPU assertions, GPU breadcrumbs, DRED diagnostics, and (planned) full minidumps with engine state.

## Crash Types

| Type | Cause | Detection | Current Status |
|------|-------|-----------|---------------|
| **Assertion failure** | Invariant violation (`UZ_CHECK`) | `FatalAssert` → abort | **Done** |
| **Recoverable condition** | Unexpected but non-fatal (`UZ_ENSURE`) | `AssertionFailed` → log + continue | **Done** |
| **GPU device removal** | Driver crash, TDR, invalid GPU state | `Present()` returns `DXGI_ERROR_DEVICE_REMOVED` | **Done** (DRED + breadcrumbs) |
| **Access violation** | Null pointer, bad memory access | SEH handler (Windows) | **Planned** |
| **Stack overflow** | Infinite recursion, deep call chains | SEH handler | **Planned** |
| **Out of memory** | Heap exhaustion | `std::bad_alloc` / D3D12MA failure | **Planned** |
| **Hang/TDR** | GPU timeout (>2s default) | Watchdog timer | **Planned** |

## Assertion System (Done)

Three assertion macros covering different severity and shipping behavior:

| Macro | Severity | Shipping | Returns | Use Case |
|-------|----------|----------|---------|----------|
| `UZ_CHECK(expr)` | Fatal | Stripped | No (aborts) | Invariants that must never fail |
| `UZ_CHECKF(expr, fmt, ...)` | Fatal | Stripped | No (aborts) | Invariants with formatted context |
| `UZ_ENSURE(expr)` | Non-fatal | Evaluates, no log | `bool` | Recoverable conditions |
| `UZ_VERIFY(expr)` | Fatal | **Kept** | No (aborts) | Side-effect expressions |

### Decision Tree

```
Is the expression result needed?
├── Yes → Does it have side effects?
│   ├── Yes → UZ_VERIFY(expr)     // always executes, fatal if false
│   └── No  → Can we recover?
│       ├── Yes → UZ_ENSURE(expr)  // log + continue
│       └── No  → UZ_CHECK(expr)   // fatal, stripped in shipping
└── No  → UZ_CHECK(expr)           // pure invariant check
```

### What Happens on Failure

**UZ_CHECK / UZ_VERIFY (fatal):**
1. Logs expression, file, line, custom message
2. Logs stack trace (if symbolizer available)
3. Flushes log output
4. Calls `std::abort()` → triggers SEH for minidump (when implemented)

**UZ_ENSURE (non-fatal):**
1. Logs expression, file, line to stderr
2. Returns `false` to caller
3. Execution continues

### Examples

```cpp
// ── Invariants (fatal, stripped in shipping) ──────────
UZ_CHECK(device != nullptr);
UZ_CHECK(index < buffer.size());
UZ_CHECKF(size > 0, "Buffer size must be positive, got %zu", size);

// ── Recoverable (non-fatal, stripped in shipping) ─────
if (!UZ_ENSURE(texture.IsValid())) {
    return fallbackTexture;  // Graceful degradation
}

// ── Side effects (fatal, NEVER stripped) ──────────────
UZ_VERIFY(CloseHandle(handle));
UZ_VERIFY(allocator.Free(ptr));
```

## GPU Crash Diagnostics (Done)

Documented in detail in [09-debug-diagnostics.md](../03-rhi/09-debug-diagnostics.md). Summary:

### GPU Breadcrumbs
Custom marker trail written via `WriteBufferImmediate`. On device removal, `LogLastMarker()` reports the exact last-completed GPU operation.

### DRED (Device Removed Extended Data)
D3D12 runtime feature providing:
- Auto-breadcrumb nodes (last GPU operations before hang)
- Page fault info (faulting VA, existing/freed allocations)

### Recovery Flow

```cpp
HRESULT hr = swapChain->Present(vsync);
if (hr == DXGI_ERROR_DEVICE_REMOVED) {
    // 1. Log GPU breadcrumb trail
    breadcrumbs.LogLastMarker();

    // 2. Query DRED for detailed diagnostics
    device->LogDeviceRemoved();

    // 3. Attempt recovery or shut down gracefully
    // (Full recovery = recreate device + all resources)
}
```

## Planned: Full Crash Context

The planned crash reporting system will capture comprehensive engine state:

### Scoped Crash Context (Planned)
```cpp
UZ_CRASH_CONTEXT("Loading shader: {}", shaderPath);
UZ_CRASH_CONTEXT("Rendering frame {}, pass: GBuffer", frameNumber);
// If crash happens within scope, these strings appear in the crash report
```

### Minidump Generation (Planned)
Windows SEH handler that generates `.dmp` files on unhandled exceptions:
- Full call stack with symbol resolution
- All thread states
- Register contents
- Mini heap dump (configurable size)

### Captured Data (Planned)
- Call stack with symbol resolution (all threads)
- GPU breadcrumb trail (last N operations)
- Log ring buffer (last N entries)
- System info (GPU, driver, OS, memory)
- Frame number and active scene
- Resource state snapshot (in-flight buffers/textures)

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| CPU assertions | `check()`, `ensure()`, `verify()` | `CRASH_COND()`, `ERR_FAIL_COND()` | `UZ_CHECK`, `UZ_ENSURE`, `UZ_VERIFY` |
| GPU crash | DRED + NVIDIA Aftermath + `GpuBreadcrumbs` | None (Vulkan validation) | DRED + custom breadcrumbs |
| Minidumps | `FWindowsPlatformCrashContext` → `.dmp` | OS-level core dump | **Planned** |
| Crash context | `FDebugInfo` stack with scoped context strings | Error string logging | **Planned** (`UZ_CRASH_CONTEXT`) |
| Telemetry | CrashReporter → Epic servers | None | **Planned** (local .dmp only) |

## Rules

1. **ALWAYS** use `UZ_CHECK` for invariants that indicate corrupted state — never silently continue
2. **ALWAYS** use `UZ_ENSURE` for recoverable conditions with graceful fallbacks
3. **ALWAYS** use `UZ_VERIFY` when the expression has side effects that must execute in all builds
4. **NEVER** use raw `assert()` — it doesn't log context, doesn't produce stack traces, and behaves differently across compilers
5. **ALWAYS** call `LogDeviceRemoved()` when `Present()` returns `DXGI_ERROR_DEVICE_REMOVED`
