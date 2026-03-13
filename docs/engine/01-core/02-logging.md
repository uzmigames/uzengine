# Core — Structured Logging System

> **Status**: Done
> **Source**: `runtime/engine/logging/log.h`, `runtime/engine/logging/log.cpp`
> **Depends on**: spdlog (third-party)

## Purpose

Every engine subsystem needs structured, category-filtered logging with compile-time stripping of verbose levels in shipping builds. The logging system is the first thing initialized at engine startup and the last thing shut down — all other systems depend on it for diagnostics.

## Design Philosophy

UzEngine's logging follows Unreal Engine 5's `UE_LOG` pattern: each subsystem declares its own log category, and messages are filtered by both compile-time minimum level (zero-cost for stripped levels) and runtime level (adjustable without recompile).

### Engine Comparison

| Engine | Macro | Categories | Compile-Time Stripping | Backend |
|--------|-------|-----------|----------------------|---------|
| **UE5** | `UE_LOG(LogRHI, Warning, ...)` | Per-subsystem `FLogCategoryBase` | `COMPILE_TIME_MIN_VERBOSITY` macro | Custom (FOutputDevice) |
| **Godot** | `print_verbose()`, `ERR_PRINT()` | None (flat) | `#ifdef DEV_ENABLED` | Custom (OS::print) |
| **UzEngine** | `UZ_LOG(RHI, Warning, ...)` | Per-subsystem `LogCategory` | `compileTimeMin` field | spdlog (console + rotating file) |

Key differences from Godot: UzEngine has proper per-category filtering (Godot has none). Key difference from UE5: we use spdlog instead of a custom backend, trading some control for battle-tested thread safety and formatting.

## Severity Levels

```
Fatal → Error → Warning → Info → Debug → Trace
 (5)     (4)     (3)       (2)    (1)     (0)
```

| Level | Purpose | Shipping Build |
|-------|---------|---------------|
| **Fatal** | Unrecoverable error — logs, flushes, calls `std::abort()` | Kept |
| **Error** | Recoverable error — something broke but we can continue | Kept |
| **Warning** | Non-fatal issue — performance problem, deprecated usage | Kept |
| **Info** | General lifecycle events — init, shutdown, major state changes | Kept |
| **Debug** | Detailed diagnostic output — useful during development | **Stripped** |
| **Trace** | Very verbose — per-frame data, inner loop state | **Stripped** |

In `UZ_SHIPPING` builds, the global compile-time minimum is `Info`. All `Trace` and `Debug` messages are eliminated with zero overhead (the comparison is resolved at compile time).

## API

### Category Declaration

Every subsystem declares its own log category in its header, then defines it in exactly one `.cpp` file:

```cpp
// ── In my_system.h ──
UZ_DECLARE_LOG_CATEGORY(MySystem);

// ── In my_system.cpp ──
UZ_DEFINE_LOG_CATEGORY(MySystem, Trace);  // compile-time min: Trace (all levels pass)
```

The second argument to `UZ_DEFINE_LOG_CATEGORY` sets the compile-time minimum. Messages below this level are eliminated at compile time — not just hidden, but the entire `UZ_LOG` call becomes dead code and is optimized away.

### Logging

```cpp
UZ_LOG(RHI, Info, "Device created: adapter='{}', VRAM={}MB", adapterName, vramMB);
UZ_LOG(Scene, Error, "Entity {} not found in world", entityId);
UZ_LOG(Core, Fatal, "Unrecoverable: {}", reason);  // aborts after logging
```

Format strings use [fmtlib syntax](https://fmt.dev/) (via spdlog). Named args, positional args, and all standard format specifiers are supported.

### Built-in Categories

The engine ships with these categories — custom systems MUST declare their own:

| Category | Subsystem |
|----------|-----------|
| `Core` | Core engine systems (startup, shutdown, tick) |
| `RHI` | Render Hardware Interface (DX12, Vulkan, Metal) |
| `Renderer` | High-level rendering (passes, materials, lighting) |
| `Scene` | Scene graph, entity management, transforms |
| `Physics` | Physics simulation (collision, dynamics, queries) |
| `Audio` | Audio engine (playback, mixing, spatial) |
| `Input` | Input handling (keyboard, mouse, gamepad) |
| `Platform` | Platform abstraction layer (OS, windowing) |
| `Memory` | Memory management (allocators, pools, tracking) |
| `IO` | File I/O, asset streaming, virtual filesystem |
| `Config` | Configuration loading and parsing |
| `Window` | Window creation, resize, focus events |
| `Engine` | Top-level engine orchestration |
| `Asset` | Asset pipeline (import, cook, cache) |

### Initialization

```cpp
uz::LogInit("uzengine.log");  // console + rotating file (10MB, 3 files)
// ... engine runs ...
uz::LogShutdown();             // flush + destroy all loggers
```

Both calls are safe to invoke multiple times (idempotent).

### Runtime Level Adjustment

```cpp
uz::LogSetLevel(uz::LogLevel::Warning);  // suppress Info and below at runtime
```

This affects all categories. Useful for reducing noise during performance testing.

## Output Format

Console output is colored by severity level. File output uses rotating log files (10MB max, keeps 3 rotated files):

```
[2026-03-11 14:23:01.142] [RHI] [info] Device created: adapter='NVIDIA RTX 4090', VRAM=24564MB
[2026-03-11 14:23:01.143] [RHI] [info] Swap chain created: 1920x1080, format=R8G8B8A8_UNORM, vsync=true
[2026-03-11 14:23:01.250] [Shader] [warning] Cache miss for 'pbr_standard.hlsl' — recompiling
[2026-03-11 14:23:05.100] [Scene] [error] Failed to load entity: path='enemies/boss.uzprefab', reason='file not found'
```

## Implementation Details

### Compile-Time Filtering

The `UZ_LOG` macro checks the category's `compileTimeMin` against the requested level. Because both are compile-time constants when the category is defined, the compiler eliminates the entire block for stripped levels:

```cpp
// Expands to (simplified):
if (LogLevel::Debug >= LogMySystem.compileTimeMin) {  // false if min is Info
    logger->log(spdlog::level::debug, ...);           // dead code — optimized away
}
```

### Thread Safety

- `LogGetLogger()` uses an internal mutex — safe to call from any thread
- spdlog loggers are thread-safe by design (lock-free for synchronous logging)
- Log initialization/shutdown are NOT thread-safe — call only from the main thread during engine startup/shutdown

### Fatal Handling

`UZ_LOG(Category, Fatal, ...)` flushes the logger and calls `std::abort()`. This ensures the fatal message is written to disk before the process terminates. It integrates with the crash reporting system to include the log buffer in crash dumps.

## Rules

1. **NEVER** use `spdlog::info/error/warn` directly — always use `UZ_LOG(Category, Level, ...)`
2. **ALWAYS** declare a log category for new subsystems via `UZ_DECLARE_LOG_CATEGORY` / `UZ_DEFINE_LOG_CATEGORY`
3. **ALWAYS** include context in error messages: what failed, why, and what input caused it
   - `"Failed to load texture: path='{}', reason='{}'"` — YES
   - `"Load failed"` — NEVER
4. **ALWAYS** log at boundaries: init/shutdown, external calls (GPU, disk, OS API), error paths
5. **NEVER** log in hot loops without `Trace` level — use `Debug` for per-frame, `Trace` for inner loops
