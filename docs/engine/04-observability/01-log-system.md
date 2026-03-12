# Observability — Log System

> **Status**: Done
> **Source**: `runtime/core/logging/log.h`, `log.cpp`
> **Depends on**: spdlog (header-only, `thirdparty/spdlog/`)

## Purpose

Every subsystem must be observable. The logging system is the first line of defense for debugging — it's the cheapest diagnostic tool and the one engineers reach for first. UzEngine's logger provides category-based, compile-time filtered logging via the `UZ_LOG` macro. In shipping builds, Trace and Debug messages are stripped at compile time with zero overhead.

## Architecture

```
UZ_LOG(Category, Level, fmt, args...)
   │
   ├── Compile-time gate: Level >= category's compileTimeMin?
   │   └── No → entire macro is dead code (optimized away)
   │
   ├── Runtime gate: Level >= runtime minimum (LogSetLevel)?
   │   └── No → discarded at runtime (cheap branch)
   │
   └── Yes → spdlog formats and dispatches to sinks:
       ├── Console sink (colored stdout)
       └── Rotating file sink (10MB per file, 3 files max)
```

### Two-Level Filtering

| Gate | When | Cost When Filtered |
|------|------|-------------------|
| **Compile-time** | `UZ_LOG` macro expansion | Zero — compiler removes dead branch |
| **Runtime** | `LogSetLevel()` / spdlog level check | One branch instruction |

In shipping builds (`UZ_SHIPPING`), the global compile-time minimum is `Info` — all `Trace` and `Debug` messages across all categories are eliminated by the compiler. No function call, no format string evaluation, no branch.

## API

### Category Declaration

Every subsystem declares its own log category:

```cpp
// ── my_system.h ──────────────────────────────────────
UZ_DECLARE_LOG_CATEGORY(MySystem);  // extern LogCategory LogMySystem

// ── my_system.cpp ────────────────────────────────────
UZ_DEFINE_LOG_CATEGORY(MySystem, Trace);  // compile-time min = Trace
// In shipping: everything below Info is stripped regardless of this setting
```

### Logging

```cpp
UZ_LOG(MySystem, Info, "Initialized with {} items", count);
UZ_LOG(MySystem, Warning, "Fallback to default: key='{}' not found", key);
UZ_LOG(MySystem, Error, "Failed to load: path='{}', reason='{}'", path, err);
UZ_LOG(MySystem, Fatal, "Unrecoverable: {}", reason);  // flushes + aborts
```

**Fatal** level is special: it flushes the logger and calls `std::abort()` after logging. Use sparingly — only for truly unrecoverable states.

### Initialization

```cpp
uz::LogInit("uzengine.log");   // Console + rotating file
uz::LogInit(nullptr);          // Console only (tests)
// ... engine runs ...
uz::LogShutdown();             // Flush + cleanup
```

`LogInit` is idempotent (safe to call multiple times). `LogShutdown` flushes all pending messages before destroying loggers.

### Runtime Level Control

```cpp
uz::LogSetLevel(uz::LogLevel::Warning);  // Suppress Info in noisy scenarios
uz::LogSetLevel(uz::LogLevel::Trace);    // Full verbosity for debugging
```

## Severity Levels

| Level | Purpose | Shipped? |
|-------|---------|----------|
| `Trace` | Very detailed diagnostic output (per-frame data, state dumps) | **No** — stripped in shipping |
| `Debug` | Debug messages (resource creation details, state transitions) | **No** — stripped in shipping |
| `Info` | General informational messages (startup, shutdown, major events) | Yes |
| `Warning` | Non-fatal conditions that may indicate problems | Yes |
| `Error` | Recoverable error conditions | Yes |
| `Fatal` | Unrecoverable errors — **aborts after logging** | Yes |

## Built-in Categories

The engine provides 14 categories out of the box:

| Category | Subsystem |
|----------|-----------|
| `Core` | Engine startup, shutdown, tick loop |
| `RHI` | DX12/Vulkan/Metal backend operations |
| `Renderer` | Passes, materials, lighting |
| `Scene` | Scene graph, entity management |
| `Physics` | Collision, dynamics, queries |
| `Audio` | Playback, mixing, spatial |
| `Input` | Keyboard, mouse, gamepad |
| `Platform` | OS, windowing abstraction |
| `Memory` | Allocators, pools, tracking |
| `IO` | File I/O, streaming, VFS |
| `Config` | Configuration loading/parsing |
| `Window` | Window creation, resize, focus |
| `Engine` | Top-level orchestration |
| `Asset` | Import, cook, cache |

New subsystems should declare their own category — never reuse an existing one for unrelated logs.

## Backend: spdlog

spdlog is the underlying logging library. UzEngine wraps it completely — application code never touches spdlog directly.

**Why spdlog:**

| Option | Pros | Cons |
|--------|------|------|
| **spdlog** (chosen) | Fast (lockfree queue), fmt-lib format, rotating files, header-only option | External dependency |
| `printf/stdout` | Zero deps | No categories, no sinks, no rotation, no color |
| Custom | Full control | Reinventing the wheel, bugs |
| UE5's `FOutputDevice` | Battle-tested | Tied to UE5 infrastructure |

**Engine Comparison:**

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Macro | `UE_LOG(LogRHI, Display, TEXT("..."), ...)` | `print_verbose("...")` / `ERR_PRINT("...")` | `UZ_LOG(RHI, Info, "...", ...)` |
| Categories | `DECLARE_LOG_CATEGORY_EXTERN` | Hardcoded error classes | `UZ_DECLARE_LOG_CATEGORY` |
| Compile-time strip | Yes (via ELogVerbosity) | No | Yes (`UZ_SHIPPING` strips Trace/Debug) |
| Format | `TEXT("%s")` printf-style | Godot's Variant string | fmt-lib `"{}"` style |
| Sinks | OutputLog, File, IDE Output | Console, File, IDE | Console (color), rotating file |

## Log Message Guidelines

```cpp
// ❌ WRONG — useless on its own
UZ_LOG(Asset, Error, "Load failed");

// ✅ CORRECT — what failed, why, what input caused it
UZ_LOG(Asset, Error, "Failed to load texture: path='{}', reason='{}'", path, err);

// ❌ WRONG — logging inside hot loop (per-vertex)
for (auto& v : vertices) {
    UZ_LOG(Renderer, Trace, "Vertex: {}", v.position);
}

// ✅ CORRECT — aggregate stat at frame end
UZ_LOG(Renderer, Debug, "Frame {}: {} draw calls, {} triangles", frame, draws, tris);
```

## Rules

1. **ALWAYS** use `UZ_LOG(Category, Level, ...)` — NEVER use `spdlog::info/error/warn` directly
2. **ALWAYS** declare a new category for new subsystems — never reuse existing categories for unrelated logs
3. **ALWAYS** include context in error messages: what failed, why, what input caused it
4. **NEVER** log per-vertex or per-pixel data — even Trace level has runtime cost when enabled
5. **ALWAYS** log at system boundaries: init, shutdown, resource create/destroy, external API calls
