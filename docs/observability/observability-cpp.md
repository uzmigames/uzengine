# Spec — Observability: Tracing, Logging, Profiling & Crash Reporting

> **Goal**: Complete instrumentation stack built from day one — every subsystem observable, every crash diagnosable, every performance bottleneck visible. This is NOT optional infrastructure — it ships with milestone 1.0.

---

## How the Big Engines Solve This

### Unreal Engine 5.6

#### Logging
- **`UE_LOG(Category, Verbosity, Format, ...)`** — core logging macro
- Categories declared with `DECLARE_LOG_CATEGORY_EXTERN(LogRHI, Log, All)` in headers, defined with `DEFINE_LOG_CATEGORY(LogRHI)` in .cpp
- 7 verbosity levels: `Fatal`, `Error`, `Warning`, `Display`, `Log`, `Verbose`, `VeryVerbose`
- **Output backends**: console (GLog), file (`FOutputDeviceFile`, saved to `Saved/Logs/` with rotation), IDE output window (`FOutputDeviceDebug` via `OutputDebugStringW`), in-game console
- **Thread-safe**: `FOutputDevice::Serialize()` is called from any thread. `FOutputDeviceRedirector` (`Runtime/Core/Private/Misc/OutputDeviceRedirector.cpp`) multiplexes to all registered output devices. `FAsyncWriter` (`Runtime/Core/Private/HAL/FileManagerGeneric.cpp`) uses a ring buffer with a dedicated `FRunnable` writer thread for async file I/O — log calls never block on disk.
- **Compile-time stripping**: `UE_LOG` calls above a certain verbosity are fully stripped in shipping builds via `COMPILED_IN_MINIMUM_VERBOSITY`
- **Structured logging**: Unreal Insights uses `UE_TRACE_LOG(Channel)` for structured binary tracing (separate from text logging)
- File rotation: logs rotate per session, old logs kept with timestamp suffix

#### CPU Profiling / Tracing
- **Two systems**:
  1. **Legacy Stats** (`DECLARE_CYCLE_STAT`, `SCOPE_CYCLE_COUNTER`) — older, stat-group based. `DECLARE_STATS_GROUP(STATGROUP_Rendering)`. Displayed via `stat` console commands (`stat unit`, `stat scenerendering`).
  2. **Unreal Insights** (`UE_TRACE_LOG`, `TRACE_CPUPROFILER_EVENT_SCOPE`) — modern binary tracing system. Streams to a separate **Unreal Insights** viewer app via network socket. Timeline visualization, flame graphs, memory tracking, asset loading.
- **Unreal Insights architecture**: The runtime has a trace system (`FTraceAuxiliary`) that writes binary trace events to a ring buffer. Events are streamed via TCP to the Insights viewer app (`UnrealInsights.exe`). The viewer is a **separate application** — not part of the editor.
- **Named threads**: Game Thread, Render Thread, RHI Thread each have their own profiler timeline
- **Memory tracing**: LLM (Low Level Memory) tracker tags every allocation with category. `UE_MEMSCOPE(Tag)` for scoped memory tracking. LLM data flows to Insights.

#### GPU Profiling
- **`SCOPED_GPU_TIMER(TimerName)`** / **`SCOPED_DRAW_EVENT(RHICmdList, Name)`** — GPU timestamp markers
- Triple-buffered GPU query heap (frame N reads results from frame N-3)
- Per-pass breakdown: Shadow, GBuffer, Lighting, PostProcess, etc.
- Results displayed in `stat gpu` and streamed to Insights
- **GPU breadcrumbs**: `ID3D12GraphicsCommandList::WriteBufferImmediate()` writes markers to a GPU-visible buffer. On device removal (TDR), the buffer is read back to identify the last successful GPU operation.
- **DRED** (Device Removed Extended Data): DX12 debug feature enabled in dev builds. On device removal, provides auto-breadcrumbs and page fault info.

#### Crash Reporting
- **Separate executable**: `CrashReportClient.exe` (under `Programs/CrashReportClient/`)
- **Architecture**: Runtime installs SEH handler (`SetUnhandledExceptionFilter` on Windows). On crash:
  1. `FPlatformCrashContext` captures: callstack (all threads), minidump (`.dmp`), log buffer (last N lines), GPU state, system info (adapter, driver, OS, RAM), crash GUID, engine version
  2. Runtime writes crash data to a temp directory (`Saved/Crashes/`)
  3. Runtime launches `CrashReportClient.exe` as a **child process** before dying
  4. CrashReportClient displays a dialog (crash description, optional user message), uploads report to backend (Sentry/BugSnagging-style), creates `.dmp` + `.log` + `.xml` crash report
- **Why separate process**: The crashing process is in an undefined state — its heap may be corrupted, its threads deadlocked. A separate process can safely read the crash data files and display UI. If the crash reporter were in-process, it might crash too.
- `FGenericCrashContext` / `FWindowsCrashContext` — platform-specific crash context classes (`Runtime/Core/Private/GenericPlatform/GenericPlatformCrashContext.cpp`)
- **`FSharedCrashContext`**: crash data passed to CrashReportClient via shared memory (`CreateFileMapping` / memory-mapped file), not via files on disk. This is faster and more reliable than writing to disk from a crashing process.
- **ensure()** — non-fatal assertion that captures callstack via `FPlatformStackWalk::CaptureStackBackTrace()` WITHOUT crashing. First occurrence logs full callstack + sends telemetry. Subsequent occurrences of the same ensure just log (no callstack — performance optimization).

#### Assertions
- `check(condition)` — fatal in debug/development, stripped in shipping (`DO_CHECK=0`)
- `checkf(condition, format, ...)` — fatal with message
- `ensure(condition)` — non-fatal, logs callstack, continues execution. NOT stripped in shipping. Uses per-location `std::atomic<bool>` (`FDebugOptionalData` in `Runtime/Core/Public/Misc/AssertionMacros.h`) keyed by file hash + line number — first trigger captures full callstack + sends telemetry, subsequent triggers at same location skip callstack (expensive).
- `ensureAlways(condition)` — like ensure but captures callstack EVERY time
- `ensureMsgf(condition, format, ...)` — non-fatal with message
- `verify(condition)` — like check but NEVER stripped (evaluates side effects in shipping)
- `verifyf(condition, format, ...)` — verify with message
- `checkNoEntry()` — marks unreachable code paths
- `unimplemented()` — marks unimplemented virtual functions

### Godot 4.x

#### Logging
- `print_line(String)`, `print_verbose(String)` — basic text output
- `ERR_PRINT(msg)`, `WARN_PRINT(msg)` — error/warning macros
- **Logger class** (`core/io/logger.h`): `StdLogger` (console), `RotatedFileLogger` (file with rotation + compression), `CompositeLogger` (multiple backends). `Logger::logf(err_type, format, ...)`.
- **No category system** — all logs go to the same channel. Filtering is by type (error/warning/message) only.
- File rotation: `RotatedFileLogger` keeps N most recent log files, rotates on engine start.
- **Thread safety**: Simple mutex around log output. No async/buffered mode.

#### CPU Profiling
- **EngineDebugger** (`core/debugger/engine_debugger.h`) + **EngineProfiler** (`core/debugger/engine_profiler.h`): profiler hooks into GDScript/C# execution. Uses TCP connection to editor for live data streaming.
- `OS::get_singleton()->benchmark_begin_measure("label")` / `benchmark_end_measure("label")`
- Editor connects to runtime profiler via **network socket** (localhost TCP). Profiler sends frame data (function timings, script timings) per frame.
- **Performance singleton** (`main/performance.h`): `Performance::get_monitor(monitor_id)` — 65+ built-in monitors: FPS, draw calls, memory, objects, physics time, etc. Fixed set, not extensible by user code.
- No flame graph / timeline visualization. Flat stat display in editor.

#### GPU Profiling
- `RenderingDevice::capture_timestamp("label")` — inserts GPU timestamp query
- Results read back after N frames (triple-buffered)
- Displayed in editor "Monitors" panel
- **No breadcrumb system** — GPU crashes produce minimal diagnostics

#### Crash Reporting
- **No separate crash reporter app**
- `CrashHandler` — platform-specific signal handlers (`SIGSEGV`, `SIGABRT` on Unix; `SetUnhandledExceptionFilter` on Windows)
- On crash: prints callstack to stderr, writes to log file, calls `abort()`
- **Minimal capture**: callstack only. No minidump, no GPU state, no structured report.
- No crash upload/telemetry system
- **This is a known weakness** — Godot crash reports are hard to diagnose vs Unreal's rich crash context

#### Assertions
- `ERR_FAIL_COND(condition)` — if true, logs error + returns from function
- `ERR_FAIL_COND_V(condition, retval)` — returns value
- `ERR_FAIL_NULL(ptr)` — null check + return
- `ERR_FAIL_INDEX(index, size)` — bounds check + return
- `CRASH_COND(condition)` — hard crash (fatal)
- `DEV_ASSERT(condition)` — stripped in release
- Error handler hooks: `ErrorHandlerList` allows registering custom error callbacks
- Non-fatal by default — `ERR_FAIL_*` logs and continues (like Unreal's `ensure`)

### Unity 6

#### Logging
- `Debug.Log/LogWarning/LogError(message)` — basic logging
- `ILogger` interface for custom loggers
- Stack traces auto-attached to `LogError`
- Console window with type filtering
- `[Conditional("DEBUG")]` attribute strips calls in release
- No category system (namespace-based convention)

#### CPU Profiling
- Built-in Profiler window: CPU, GPU, Memory, Audio, Physics, Networking modules
- `Profiler.BeginSample("name")` / `EndSample()` — manual instrumentation
- Deep Profiling mode: instruments every method call (huge overhead)
- **Timeline view**: shows per-frame breakdown
- **Profile Analyzer**: compare profiling sessions (before/after optimization)
- Memory Profiler package: detailed snapshot-based memory analysis

#### GPU Profiling
- GPU module in Profiler: per-pass timing (Shadow, GBuffer, etc.)
- `CommandBuffer.BeginSample/EndSample` for custom GPU markers
- Frame Debugger: step through draw calls
- RenderDoc integration via package

#### Crash Reporting
- **Unity Cloud Diagnostics** (crash reporting service)
- On crash: captures managed callstack, native callstack (IL2CPP), device info
- Uploads to Unity Dashboard
- **No separate crash reporter exe** — integrated into the runtime
- Android: captures via `UncaughtExceptionHandler` + native signal handlers
- iOS: symbolication via dSYM files
- **Limited GPU crash info** — no breadcrumb system, no DRED

#### Assertions
- `Debug.Assert(condition, message)` — dev-only, stripped in release
- Standard C# `try/catch/finally`
- `NullReferenceException` — common runtime error (managed)
- No macro-based assertion system (it's C#)

---

## Analysis: Tracy vs spdlog vs Custom

### Tracy Profiler

**What it is**: Open-source (BSD), real-time frame profiler with a **separate viewer application** (`tracy-profiler.exe`). Client library instruments the application; viewer connects via TCP for live visualization.

**Architecture**:
```
┌─ Game Runtime ──────────────┐      TCP socket      ┌─ Tracy Viewer ──────┐
│                              │ ──────────────────► │                      │
│  ZoneScoped;                 │  binary protocol    │  Timeline            │
│  TracyAlloc(ptr, size);      │  ~2-5% CPU overhead │  Flame graph         │
│  FrameMark;                  │                      │  Memory tracking     │
│  TracyD3D12Zone(ctx, "Pass")│                      │  GPU timeline        │
│                              │                      │  Lock contention     │
└──────────────────────────────┘                      └──────────────────────┘
```

**Pros**:
- Extremely low overhead (~2-5% CPU) — designed for always-on profiling
- GPU profiling: native DX12/Vulkan/OpenGL support (`TracyD3D12Context`, GPU zone markers)
- Memory tracking: per-allocation source tracking (callstack + zone)
- Lock contention analysis: tracks mutex wait times
- Timeline + flame graph visualization — best-in-class UI
- Frame image capture (send screenshots to viewer)
- Compile-time stripping: `TRACY_ENABLE` macro guards all instrumentation

**Cons**:
- **Separate viewer app** — requires launching `tracy-profiler.exe` alongside the game
- **Network dependency** — viewer connects via TCP socket. In N-API/VSCode, this means either localhost connection or the viewer is a separate window
- **No structured logging** — Tracy is a profiler, not a logger. No category/verbosity system.
- **No crash reporting** — completely separate concern
- **C++ only** — TypeScript extension code can't be profiled with Tracy

**DX12 integration**:
```cpp
tracy::D3D12QueueCtx* tracyCtx = tracy::CreateD3D12Context(device, commandQueue);
{
    tracy::D3D12ZoneScope zone(tracyCtx, commandList, "GBufferPass");
    // GPU commands
}
```

### spdlog

**What it is**: Fast C++ logging library. Header-only or compiled. Async mode. File rotation. Pattern formatting.

**Architecture**:
```cpp
auto logger = spdlog::rotating_logger_mt("engine", "logs/engine.log", 1024*1024*5, 3);
logger->info("Device created: {}", adapterName);    // sync
logger->flush_on(spdlog::level::err);               // auto-flush on error

// Async mode — dedicated logging thread, lock-free queue
auto async_logger = spdlog::create_async<spdlog::sinks::rotating_file_sink_mt>("async", ...);
```

**Pros**:
- Very fast: ~100ns per log call (sync), async mode even faster
- Multiple sinks: console, file (with rotation), syslog, MSVC debug output, custom
- `fmt` library formatting (same as what we'd use anyway)
- Compile-time level filtering: `SPDLOG_ACTIVE_LEVEL` strips calls below threshold
- Thread-safe with minimal contention
- Well-maintained, widely used

**Cons**:
- **No category system** — spdlog has "loggers" (named instances) but not the macro-based category pattern of UE_LOG. You'd need to create one logger per category manually.
- **No structured tracing** — text-based only. Can't feed into a timeline profiler.
- **No GPU awareness** — purely CPU-side logging
- **Overhead of generality** — we'd use maybe 20% of spdlog's features. A custom UZ_LOG macro wrapping `fmt` + our own sink system would be simpler and more integrated.

### ETW (Event Tracing for Windows)

**What it is**: Windows-native tracing infrastructure. Used by PIX, WPA (Windows Performance Analyzer), and GPU profilers.

**Pros**: Zero-overhead when no consumer attached. GPU driver events visible. PIX integration.
**Cons**: Windows-only. Complex API. Not suitable as primary profiling system (good as supplementary).

---

## Decision: What to Use

### Logging: **spdlog backend + UZ_LOG macros**

**Decision**: spdlog as the backend, wrapped in UE5-style UZ_LOG category macros.

**Rationale** (revised after source analysis):
- spdlog gives us production-quality async logging for free: lock-free MPMC queue (`spdlog::async_logger`), file rotation (`rotating_file_sink`), thread safety, multi-sink — rewriting this correctly is 2000+ lines, not "~500"
- The category gap is solved by mapping each `UZ_DECLARE_LOG_CATEGORY` to a named spdlog logger instance — `spdlog::get("LogRHI")` is O(1) lookup via `std::unordered_map`
- spdlog uses `fmt` internally — same formatting engine we'd use anyway, zero duplication
- Compile-time stripping via `SPDLOG_ACTIVE_LEVEL` + our own `UZ_MIN_LOG_LEVEL` macro layer
- **UE5 reference**: `FAsyncWriter` uses a ring buffer with dedicated writer thread (`FRunnable`), routed through `FOutputDeviceRedirector` to multiple outputs. spdlog's `async_factory` gives us the same architecture without 1500 lines of custom code
- **Godot reference**: `CompositeLogger` (StdLogger + RotatedFileLogger) — simple but no async mode, simple mutex = contention under heavy logging. spdlog is strictly better.

```cpp
// UZ_LOG macros (user-facing API — looks like UE_LOG)
UZ_DECLARE_LOG_CATEGORY(LogRHI);         // Header — declares extern spdlog::logger*
UZ_DEFINE_LOG_CATEGORY(LogRHI);          // Cpp — creates named spdlog logger with all sinks
UZ_LOG(LogRHI, Info, "Device: {}", name); // Usage — compile-time filtered, routes to spdlog

// Macro expansion (simplified)
#define UZ_LOG(Category, Level, ...) \
    do { \
        if constexpr (static_cast<int>(uz::LogLevel::Level) >= UZ_MIN_LOG_LEVEL) { \
            if (Category->should_log(uz::ToSpdLevel(uz::LogLevel::Level))) { \
                Category->log(uz::ToSpdLevel(uz::LogLevel::Level), __VA_ARGS__); \
                uz::CrashRingBuffer::Push(uz::LogLevel::Level, #Category, fmt::format(__VA_ARGS__)); \
            } \
        } \
    } while(0)
```

**Implementation**:
- **Backend**: spdlog with `async_factory` (dedicated thread + lock-free MPMC queue, 8192 slot default)
- **Sinks (all via spdlog sink API)**:
  - `spdlog::sinks::stdout_color_sink_mt` — console with color
  - `spdlog::sinks::rotating_file_sink_mt` — file rotation (5 MB × 3 files)
  - `spdlog::sinks::msvc_sink_mt` — Win32 `OutputDebugString` (dev builds)
  - Custom `NAPISink : spdlog::sinks::base_sink<std::mutex>` — streams to extension host (Phase 2)
- **Crash ring buffer**: Custom addition on top of spdlog — every UZ_LOG also pushes to a lock-free ring buffer (last 4096 entries) in pre-allocated memory for crash capture. This is NOT a spdlog sink (ring buffer must be in pre-allocated memory, not heap)
- **Compile-time filtering**: `UZ_MIN_LOG_LEVEL` strips calls below threshold at compile time. `spdlog::logger::set_level()` for runtime filtering per category.
- **Flush policy**: `spdlog::flush_on(spdlog::level::err)` — auto-flush on error/fatal. Manual flush on crash.

### Profiling: **Tracy (embedded) + Custom stat system**

**Decision**: Tracy for timeline/flame graph profiling, custom stat counters for real-time HUD.

**Rationale**:
- Tracy is the best open-source profiler available — no point reimplementing its timeline/flame graph/memory tracking
- Tracy's DX12 GPU profiling is battle-tested
- The separate viewer app is actually a **feature** for us — it doesn't interfere with VSCode, and the developer can have Tracy viewer on a second monitor
- We wrap Tracy macros in our own `UZ_PROFILE_*` macros so we can strip Tracy entirely from shipping builds
- We add a thin custom stat system for the in-editor HUD (FPS, draw calls, memory — like Unreal's `stat unit`)

```cpp
// Our macros wrap Tracy
#if UZ_PROFILING_ENABLED
  #define UZ_PROFILE_SCOPE(group, name) ZoneScopedN(#group "." #name)
  #define UZ_PROFILE_FRAME_MARK()       FrameMark
  #define UZ_GPU_ZONE(ctx, cmdList, name) tracy::D3D12ZoneScope __gpu_zone(ctx, cmdList, name)
#else
  #define UZ_PROFILE_SCOPE(group, name)
  #define UZ_PROFILE_FRAME_MARK()
  #define UZ_GPU_ZONE(ctx, cmdList, name)
#endif

// Custom stat counters (lightweight, always-on, for in-editor HUD)
UZ_STAT_INCREMENT(Rendering, DrawCalls);
UZ_STAT_SET(Rendering, TriangleCount, numTris);
UZ_STAT_SET(Memory, GPUUsageMB, gpuMem / (1024*1024));
```

**Tracy integration**:
- `TRACY_ENABLE` defined in dev/debug builds, undefined in shipping
- **`TRACY_ON_DEMAND`**: critical for N-API — Tracy only collects data when the viewer is connected. Without this, Tracy buffers grow unbounded in memory (problematic inside Electron's process)
- Tracy viewer: developer launches separately when profiling (`tracy-profiler.exe`)
- GPU context: one `tracy::D3D12QueueCtx` per command queue (Graphics, Compute, Copy)
  - **Thread safety**: Tracy issue #227 — `D3D12QueueCtx` is NOT thread-safe. Either serialize access or create one context per queue per thread. We use one context per queue, accessed only from the submission thread.
- Memory: `TracyAlloc(ptr, size)` / `TracyFree(ptr)` routed from our allocator system
- Lock tracking: `TracyLockable(std::mutex, m_renderMutex)` for contention analysis
- Frame images: optionally send viewport screenshot to Tracy viewer for visual correlation
- **Overhead**: ~2.25ns per zone (measured), acceptable for always-on in dev builds

**Phase 2 (editor)**: Stats streamed to editor webview panel for in-VSCode profiler UI. Tracy viewer remains available for deep profiling.

### GPU Profiling: **Tracy D3D12 + DRED + Breadcrumbs**

**Decision**: Tracy for GPU timeline, DRED + manual breadcrumbs for crash debugging.

```cpp
// GPU timeline profiling (Tracy)
{
    UZ_GPU_ZONE(tracyCtx, cmdList, "ShadowPass");
    // Shadow rendering commands
}

// GPU breadcrumbs (crash debugging, separate from Tracy)
UZ_GPU_BREADCRUMB(cmdList, "DrawMesh: {}", meshName);
// Writes to a GPU-visible buffer via WriteBufferImmediate
// On TDR → buffer read back → last breadcrumb identifies culprit

// DRED (DX12 debug layer)
// Enabled in dev builds: ID3D12DeviceRemovedExtendedDataSettings::SetAutoBreadcrumbsEnablement()
// On device removal: ID3D12DeviceRemovedExtendedData::GetAutoBreadcrumbsOutput()
```

### Crash Reporting: **Separate executable (like Unreal)**

**Decision**: `uzengine-crashreport.exe` — separate process launched on crash.

**Rationale**:
This is the single most important architectural decision in the crash reporting system. The arguments for a separate process:

1. **The crashing process is unreliable** — heap may be corrupted, threads deadlocked, GPU in undefined state. Any code running in-process after a crash may itself crash. Unreal learned this the hard way and moved to a separate process.

2. **File locks** — the crashing process may hold locks on files (log files, temp files). A separate process can wait for the crash, then collect files.

3. **UI** — showing a crash dialog from a crashing process is dangerous (GDI/Win32 state may be corrupted). The separate process has a clean window.

4. **N-API complication** — when running as a `.node` addon inside VSCode, a crash kills the extension host. Electron restarts it, but our crash data must survive. The separate crash reporter process outlives the extension host crash.

5. **Godot doesn't do this** — and Godot crash reports are notoriously poor (just a callstack to stderr). Unreal's crash reports are far more useful because the separate process can take its time collecting data.

**Why reporter lives inside `runtime/core/crash/reporter/` (not `tools/`)**:
- The reporter **depends directly** on shared structs (`CrashContext`, ring buffer layout, breadcrumb format) — if these change in the runtime, the reporter must change too
- Same CMake pipeline: one `add_executable(uzengine-crashreport ...)` in the runtime's `CMakeLists.txt`, compiles together automatically
- Shared headers without duplication (`crash_context.h` used by both handler and reporter)
- UE5 does the same: `CrashReportClient` lives under `Programs/` in the same repo and build system
- `tools/` is for **independent** utilities (asset viewer, shader debugger) that don't share data structures with the runtime

**Architecture**:

```
┌─ Runtime (normal execution) ──────────────────────────────────┐
│                                                                │
│  SEH handler installed at startup                              │
│  Log ring buffer (last 4096 entries) in shared memory          │
│  GPU breadcrumb buffer (mapped, CPU-readable)                  │
│  Crash context struct (pre-allocated, not on heap)             │
│                                                                │
│  ══════════ CRASH OCCURS ══════════                            │
│                                                                │
│  1. SEH handler fires                                          │
│  2. Write crash context to pre-allocated buffer:               │
│     - Exception code, address, registers                       │
│     - All thread callstacks (StackWalk64)                      │
│     - Log ring buffer snapshot                                 │
│     - GPU breadcrumb buffer snapshot                           │
│     - System info (GPU adapter, driver, OS, memory)            │
│     - Frame number, active scene, timestamp                    │
│  3. Write crash context to shared memory (CreateFileMapping)   │
│     - Faster & more reliable than file I/O from crashing proc  │
│     - Fallback: write to Saved/Crashes/{crash-guid}/ on disk   │
│  4. Write minidump to Saved/Crashes/{crash-guid}/crash.dmp     │
│  5. Launch uzengine-crashreport.exe --crash-guid <guid>        │
│  6. Process terminates (or Electron restarts extension host)   │
└────────────────────────────────────────────────────────────────┘

┌─ uzengine-crashreport.exe (separate process) ─────────────────┐
│                                                                │
│  1. Reads crash context from shared memory (or disk fallback)  │
│     Writes structured data to Saved/Crashes/{guid}/            │
│  2. Symbolicate callstacks (.pdb lookup)                       │
│  3. Display crash dialog (optional, configurable)              │
│     - Shows crash summary, callstack, GPU breadcrumbs          │
│     - User can add description                                 │
│     - "Send Report" / "Don't Send" buttons                     │
│  4. Upload to telemetry backend (optional, configurable)       │
│     - Sentry, BugSnag, or custom endpoint                      │
│  5. Write human-readable crash report to crash.txt             │
│  6. Exit                                                       │
└────────────────────────────────────────────────────────────────┘
```

**Crash data captured**:

| Data | Source | Purpose |
|------|--------|---------|
| Exception code + address | SEH handler | What crashed and where |
| All thread callstacks | `StackWalk64` + PDB symbols | Who called the crashing code |
| Minidump (.dmp) | `MiniDumpWriteDump` | Loadable in Visual Studio for full debugging |
| Log ring buffer | Last 4096 `UZ_LOG()` calls | What was happening before the crash |
| GPU breadcrumbs | `WriteBufferImmediate` buffer | Last GPU operations before TDR |
| DRED data | `ID3D12DeviceRemovedExtendedData` | DX12 device removal details |
| System info | `DXGI_ADAPTER_DESC`, `GetVersionExW` | GPU, driver, OS, RAM |
| Engine state | Pre-allocated context struct | Frame number, active scene, time |
| Crash GUID | UUID generated at crash time | Unique identifier for dedup |

**Pre-allocation**: The crash context struct, log ring buffer, and breadcrumb buffer are allocated at startup in a reserved memory region. This ensures the crash handler doesn't need to allocate memory (which would fail if the heap is corrupted).

**N-API / VSCode integration**: When running inside VSCode, the crash reporter writes to `Saved/Crashes/`. On extension host restart, the extension checks for unprocessed crash reports and offers to show/upload them. The crash reporter can also be configured to automatically upload without UI (headless mode for CI).

---

## Assertions: **Unreal-style**

```cpp
// Fatal — stripped in shipping (UZ_SHIPPING=1)
UZ_CHECK(ptr != nullptr);
UZ_CHECKF(idx < size, "Index {} out of bounds (size {})", idx, size);
UZ_CHECK_NO_ENTRY();  // Unreachable code

// Non-fatal — logs callstack + continues. NOT stripped in shipping.
// First trigger at a location captures full callstack; subsequent triggers skip it.
UZ_ENSURE(result.IsValid());
UZ_ENSUREF(handle.IsValid(), "Invalid handle: {}", handle.Id());
UZ_ENSURE_ALWAYS(result.IsValid()); // Captures callstack EVERY time (no dedup)

// Never stripped — side effects always evaluated
UZ_VERIFY(Initialize());
UZ_VERIFYF(Connect(), "Failed to connect to {}", address);
```

**ensure() implementation** (modeled on UE5's `FDebugOptionalData`): On first trigger, captures callstack via `CaptureStackBackTrace()` (Windows) → formats and logs. Uses a per-location `std::atomic<bool>` (keyed by file hash + line number, like UE5's `ensureAlways` vs `ensure` distinction) so subsequent triggers at the same location just log the message without capturing a (expensive) callstack. `UZ_ENSURE_ALWAYS` variant captures callstack every time (for critical paths where repeated failures indicate different root causes).

---

## Build Configuration Matrix

| Build Type | Logging | Profiling (Tracy) | Stats | GPU Breadcrumbs | Assertions | Crash Reporter |
|-----------|---------|-------------------|-------|-----------------|------------|----------------|
| **Debug** | All levels | Full (Tracy enabled) | Full | Enabled | check+ensure+verify | Full (dialog + upload) |
| **Development** | Info+ (Debug/Trace stripped) | Full (Tracy enabled) | Full | Enabled | check+ensure+verify | Full |
| **Test** | Warning+ | Minimal (stats only) | Stats only | Disabled | ensure+verify only | Headless (auto-upload) |
| **Shipping** | Error+ | None (Tracy stripped) | Lightweight counters | Disabled | verify only | Headless (auto-upload) |

---

## File Structure

```
runtime/core/
├── logging/
│   ├── log.h                  # UZ_LOG, UZ_DECLARE/DEFINE_LOG_CATEGORY macros
│   ├── log.cpp                # Log dispatcher, spdlog init, category registry
│   ├── log_category.h         # LogCategory class (name, runtime verbosity)
│   ├── sinks/
│   │   └── napi_sink.h        # Custom spdlog sink: stream to extension host (Phase 2)
│   └── ring_buffer.h          # Lock-free ring buffer for crash capture (pre-allocated)
├── profiling/
│   ├── profiler.h             # UZ_PROFILE_SCOPE, UZ_PROFILE_FRAME_MARK (Tracy wrappers)
│   ├── stats.h                # UZ_STAT_INCREMENT, UZ_STAT_SET (lightweight counters)
│   └── stats.cpp              # Stat storage, per-frame reset
├── gpu_profiler/
│   ├── gpu_profiler.h         # UZ_GPU_ZONE, UZ_GPU_TIMESTAMP
│   ├── gpu_profiler_dx12.cpp  # DX12 timestamp queries + Tracy D3D12 context
│   ├── gpu_breadcrumbs.h      # UZ_GPU_BREADCRUMB
│   └── gpu_breadcrumbs_dx12.cpp # WriteBufferImmediate + DRED integration
├── crash/
│   ├── crash_context.h        # Pre-allocated crash context struct (shared with reporter)
│   ├── crash_handler.h        # SEH handler installation
│   ├── crash_handler_win.cpp  # Windows SEH + MiniDumpWriteDump
│   ├── crash_handler_linux.cpp # Signal handlers + core dump
│   ├── crash_symbols.h        # PDB/DWARF symbol resolution
│   └── reporter/              # Separate exe, same build pipeline
│       ├── main.cpp           # uzengine-crashreport.exe entry point
│       ├── crash_reader.cpp   # Read shared memory / disk fallback
│       ├── crash_dialog.cpp   # Win32 crash dialog UI (optional)
│       └── crash_upload.cpp   # Upload to Sentry/BugSnag/custom
```

---

## Deliverables

| Task | Acceptance Criteria |
|------|-------------------|
| `UZ_LOG` macro system | Logs appear in console with category/level/timestamp |
| Log categories | Per-category runtime verbosity filtering works |
| File sink with rotation | Logs written to file, rotation on size/session |
| Debug output sink | Logs appear in VS Output window via OutputDebugString |
| Log ring buffer | Last 4096 entries queryable, survives until crash capture |
| Tracy integration | `UZ_PROFILE_SCOPE` zones visible in Tracy viewer |
| Tracy GPU profiling | GPU zones visible in Tracy timeline per-pass |
| Stat counters | FPS, draw calls, memory queryable for HUD display |
| GPU breadcrumbs | Breadcrumb trail readable after simulated device removal |
| DRED integration | DX12 auto-breadcrumbs enabled, data captured on TDR |
| SEH crash handler | Crash produces minidump + crash.json + log dump |
| `uzengine-crashreport.exe` | Separate process reads crash data, shows dialog, symbolicated callstack |
| `UZ_CHECK/ENSURE/VERIFY` | check fatal in debug, ensure logs callstack + continues, verify always evaluates |
| Compile-time stripping | Shipping build has no Tracy, no Debug/Trace logs, no check assertions |

---

## Dependencies

- **Part of**: 1.0 (Bootstrap) — this ships with the first milestone
- **GPU profiling requires**: 1.2 (RHI) — needs DX12 device for GPU timestamps
- **NAPI sink requires**: 2.0 (N-API Foundation) — streams logs to extension host
- **Editor profiler UI requires**: 2.1 (Viewport & Core UI) — webview profiler panel

---

## Third-Party Libraries

| Library | Purpose | Track | License |
|---------|---------|-------|---------|
| **spdlog** | Async logging backend (includes fmt) | source | MIT |
| **Tracy** | Timeline profiler (CPU + GPU + memory) | source | BSD |
| **libbacktrace** (Linux) | Stack traces for crash reports | source | BSD |

**NOT using**: ETW (supplementary only, not primary). **Using spdlog** as backend (gives us async MPMC queue, file rotation, multi-sink for free, wrapped in UZ_LOG macros for UE5-style categories).

---

*Reference: [ARCHITECTURE.md](../../ARCHITECTURE.md) | [ROADMAP.md](../../ROADMAP.md) | [1.0-bootstrap.md](../1.0-bootstrap.md) | [observability-ts.md](observability-ts.md)*
