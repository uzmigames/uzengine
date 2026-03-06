# Crash Reporting & Error Telemetry

> **Status**: Planning
> **Phase**: 1
> **Priority**: High — essential for AI-assisted debugging and development

---

## Overview

The Crash Reporting system captures unhandled exceptions, fatal errors, and engine panics into **structured crash dumps** for later analysis. Every crash includes:

- **Minidump** (Windows native): Full memory snapshot for debugger inspection
- **Crash log**: Formatted error data (timestamp, exception code, call stack, Lua state)
- **System info**: OS version, GPU, RAM, UzEngine version
- **Engine state**: Active scene, recent IPC commands, resource counts
- **Lua state** (if applicable): Last Lua error, stack trace, active scripts

**Design principle**: Crashes are **never silent**. Every unhandled exception is captured and logged, and developers are notified with a path to the crash report.

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│ Main Game Loop (render.exe)                       │
│                                                     │
│  while running:                                     │
│    try:                                             │
│      update() → render() → present()                │
│    except UnhandledException:                       │
│      ↓ (Windows: SetUnhandledExceptionFilter)      │
└────────────┬────────────────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────────────────┐
│ Exception Handler (SEH / C++ std::terminate)       │
│                                                     │
│  1. Log crash header (time, exception code)         │
│  2. Capture system info (OS, CPU, GPU, RAM)        │
│  3. Dump call stack (stack unwinding)              │
│  4. Capture Lua state (if script error)            │
│  5. Save engine state (scene, resources, memory)   │
│  6. Write minidump (Windows native)                │
│  7. Create zip package                             │
└────────────┬────────────────────────────────────────┘
             │
             ▼
    %APPDATA%/UzEngine/<project>/crashes/
    ├── crash_2025-03-01T14_30_45_Z.zip
    │   ├── crash.json          (structured log)
    │   ├── crash.dmp           (minidump)
    │   ├── engine.log          (tail, last 1000 lines)
    │   └── state.json          (engine state snapshot)
    │
    └── crash_2025-03-01T12_15_22_Z.zip
        └── ...
```

---

## Crash Capture Process

### 1. Exception Handler Installation

**Windows (MSVC)**:
```cpp
// src/core/crash_reporting.h

namespace crash_reporting {

// Install global exception handler (call at engine startup)
void install_handler();

// Manually trigger crash report (for fatal errors)
void report_fatal_error(const char* format, ...);

}  // namespace crash_reporting
```

**Implementation**:
```cpp
// src/core/crash_reporting.cpp

#include <Windows.h>
#include <DbgHelp.h>

static LONG WINAPI exception_filter(EXCEPTION_POINTERS* pExceptionInfo) {
    crash_reporting::capture_crash(pExceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;  // Exit process
}

void crash_reporting::install_handler() {
    SetUnhandledExceptionFilter(exception_filter);
    std::set_terminate([]() {
        // C++ std::terminate (from exception or abort)
        crash_reporting::capture_crash(nullptr);
        std::exit(EXIT_FAILURE);
    });
}
```

### 2. Crash Data Capture

**Capture order**:
1. Record exception timestamp
2. Get exception code and address
3. Walk call stack (using StackWalk64 on Windows)
4. Capture system info (OS version, CPU, GPU)
5. Snapshot engine state (memory, resources, active scene)
6. Dump Lua stack trace (if in Lua code)
7. Capture engine.log tail (last 1000 lines)
8. Write minidump file (Windows native)
9. Create `.zip` package with all data

### 3. Crash Report Directory

Crash reports are stored in:
```
%APPDATA%/UzEngine/<project_name>/crashes/crash_<timestamp>.zip
```

**Example path**:
```
C:\Users\bolado\AppData\Local\UzEngine\my-game\crashes\
  crash_2025-03-01T14_30_45_Z.zip
  crash_2025-03-01T12_15_22_Z.zip
```

---

## Crash Report Format

Each `.zip` contains:

### `crash.json` — Structured Crash Data

```json
{
  "metadata": {
    "timestamp": "2025-03-01T14:30:45Z",
    "engine_version": "0.3.1",
    "engine_commit": "a7e09a1",
    "build_config": "Debug",
    "process_id": 8492,
    "uptime_seconds": 127.4
  },

  "exception": {
    "code": "0xC0000005",
    "code_name": "ACCESS_VIOLATION",
    "address": "0x7FF8A2C3D410",
    "flags": 1,
    "parameters": ["0x1", "0x7FF8A2C3D410"]
  },

  "callstack": [
    {
      "address": "0x7FF8A2C3D410",
      "module": "render.exe",
      "symbol": "Renderer::render_shadow_pass",
      "file": "src/renderer/shadow_pass.cpp",
      "line": 142,
      "offset": 0x2B3C
    },
    {
      "address": "0x7FF8A1F5A2E0",
      "module": "render.exe",
      "symbol": "Renderer::render_frame",
      "file": "src/renderer/renderer.cpp",
      "line": 89,
      "offset": 0x4D5E
    },
    {
      "address": "0x7FF8A0C2B100",
      "module": "render.exe",
      "symbol": "main_loop",
      "file": "src/core/main.cpp",
      "line": 156,
      "offset": 0x1A2B3C
    }
  ],

  "system": {
    "os": {
      "name": "Windows",
      "version": "10",
      "build": 19045,
      "platform": "x64"
    },
    "cpu": {
      "brand": "Intel",
      "model": "Core i9-13900K",
      "cores": 24,
      "frequency_ghz": 3.0
    },
    "gpu": {
      "vendor": "NVIDIA",
      "name": "GeForce RTX 4090",
      "vram_gb": 24
    },
    "memory": {
      "total_gb": 64,
      "available_gb": 48
    }
  },

  "engine_state": {
    "scene": {
      "path": "res://scenes/level1.uzscene",
      "node_count": 287,
      "loaded_assets": 1542
    },
    "memory": {
      "frame_arena_mb": 42,
      "level_arena_mb": 256,
      "permanent_arena_mb": 214,
      "total_allocated_mb": 512,
      "total_capacity_mb": 2048,
      "high_water_mark_mb": 654
    },
    "renderer": {
      "draw_calls": 156,
      "triangles": 2400000,
      "gpu_memory_mb": 1024
    },
    "lua": {
      "heap_mb": 8,
      "active_scripts": 23
    }
  },

  "ipc_commands": [
    {
      "timestamp": "2025-03-01T14:30:30Z",
      "command": "console.execute",
      "args": {"command": "set_timescale 0.5"}
    },
    {
      "timestamp": "2025-03-01T14:30:35Z",
      "command": "engine.screenshot"
    },
    {
      "timestamp": "2025-03-01T14:30:40Z",
      "command": "scene.set_rotation",
      "args": {"rotation_y": 1.57}
    }
  ],

  "lua_error": null
}
```

### Lua Error Example

If the crash occurred in Lua code:

```json
{
  "lua_error": {
    "error_message": "attempt to index a nil value (global 'player')",
    "traceback": [
      {
        "file": "scripts/level_manager.lua",
        "line": 42,
        "function": "spawn_enemies"
      },
      {
        "file": "scripts/game.lua",
        "line": 18,
        "function": "on_scene_load"
      }
    ],
    "lua_version": "5.1 (LuaJIT)",
    "active_scripts": [
      "scripts/game.lua",
      "scripts/player.lua",
      "scripts/level_manager.lua"
    ]
  }
}
```

### `crash.dmp` — Minidump File

**Windows native minidump** (created by `MiniDumpWriteDump`):
- Full process memory snapshot (can be inspected in Visual Studio or WinDbg)
- Symbol information (if available)
- Exception context
- Thread state
- Module list

**Use**: Open in Visual Studio to inspect variables, step through code, understand root cause.

### `engine.log` — Log Tail

Last **1000 lines** of the engine log (plain text):

```
[2025-03-01 14:30:20] [DEBUG] Loading scene: res://scenes/level1.uzscene
[2025-03-01 14:30:20] [DEBUG] Loaded 127 nodes
[2025-03-01 14:30:21] [INFO]  Scene loaded in 1.2 seconds
[2025-03-01 14:30:25] [WARN]  Shader compilation warning in lighting.hlsl line 42
[2025-03-01 14:30:30] [DEBUG] Executing console command: set_timescale 0.5
[2025-03-01 14:30:35] [DEBUG] Frame 4250: 144 FPS, 156 draw calls
...
[2025-03-01 14:30:45] [ERROR] Access violation at 0x7FF8A2C3D410
[2025-03-01 14:30:45] [ERROR] Crash report saved to: C:\Users\...\crashes\crash_2025-03-01T14_30_45_Z.zip
```

### `state.json` — Engine State Snapshot

Serialized engine state at crash time:

```json
{
  "resources": {
    "textures_loaded": 234,
    "meshes_loaded": 89,
    "materials_loaded": 156,
    "shaders_loaded": 42
  },
  "active_animations": 12,
  "physics_bodies": 87,
  "audio_sources": 5,
  "particle_emitters": 3
}
```

---

## Crash File Structure

```
crash_2025-03-01T14_30_45_Z.zip
├── crash.json          (Structured crash data — most important)
├── crash.dmp           (Windows minidump for debugger)
├── engine.log          (Engine log tail)
└── state.json          (Engine state snapshot)
```

**Total size**: ~50–200 MB (depending on scene complexity and log size)

---

## IPC: Engine Crash Detection

The VSCode extension detects when the engine process exits unexpectedly:

```cpp
// IPC handler: query for last crash
ipc_registry_.register("engine.get_last_crash", [](const json& args) {
    if (auto last_crash = crash_reporting::get_last_crash_path()) {
        return json{
            {"ok", true},
            {"crash_path", last_crash->string()},
            {"crash_data", crash_reporting::read_crash_json(last_crash.value())}
        };
    } else {
        return json{{"ok", false}};
    }
});
```

**Extension behavior**:
1. Engine process exits (non-zero exit code)
2. Extension calls `engine.get_last_crash` IPC
3. If available, shows notification: *"Engine crashed — crash report saved to C:\Users\...\crashes\crash_*.zip"*
4. User clicks "Open report" → opens `crash.json` in editor

---

## System Info Capture

```cpp
// src/platform/system_info.h

struct SystemInfo {
    struct OS {
        std::string name;         // "Windows"
        int version_major;        // 10
        int version_minor;        // 0
        int build;                // 19045
        bool is_64bit;            // true
    } os;

    struct CPU {
        std::string brand;        // "Intel"
        std::string model;        // "Core i9-13900K"
        int physical_cores;
        int logical_cores;
        float frequency_ghz;
    } cpu;

    struct GPU {
        std::string vendor;       // "NVIDIA"
        std::string name;         // "GeForce RTX 4090"
        uint64_t vram_bytes;
    } gpu;

    struct Memory {
        uint64_t total_bytes;
        uint64_t available_bytes;
    } memory;
};

SystemInfo get_system_info();
```

---

## Memory Capture

```cpp
// src/core/memory_tracking.h

struct MemorySnapshot {
    struct {
        size_t allocated;
        size_t capacity;
        size_t high_water_mark;
    } frame_arena, level_arena, permanent_arena;

    size_t lua_heap_bytes;
    size_t gpu_memory_bytes;
};

MemorySnapshot capture_memory_state();
```

---

## Lua State Capture

When a Lua error occurs, the crash reporter captures:

```cpp
// src/scripting/lua_crash_info.h

struct LuaCrashInfo {
    std::string error_message;

    struct StackFrame {
        std::string file;
        int line;
        std::string function;
    };
    std::vector<StackFrame> traceback;

    std::vector<std::string> active_scripts;
    std::string lua_version;
};

LuaCrashInfo capture_lua_crash_info();
```

**Retrieved via** `lua_getstack()`, `lua_getinfo()`, and traversing the Lua call stack.

---

## IPC Command History

The crash report includes the last **10 executed IPC commands**, allowing developers to understand what operations preceded the crash:

```json
{
  "ipc_commands": [
    {
      "timestamp": "2025-03-01T14:30:30Z",
      "command": "console.execute",
      "args": {"command": "set_timescale 0.5"}
    },
    {
      "timestamp": "2025-03-01T14:30:35Z",
      "command": "engine.screenshot"
    },
    {
      "timestamp": "2025-03-01T14:30:40Z",
      "command": "scene.set_rotation",
      "args": {"rotation_y": 1.57}
    }
  ]
}
```

The IPC layer maintains a ring buffer of recent commands that gets dumped on crash.

---

## Safety & Privacy

### Crash Report Contents

Crash reports contain **sensitive information**:
- **Memory snapshot**: Could include player data, secrets, sensitive strings
- **Log files**: May contain file paths, usernames, internal state
- **IPC history**: Recent operations the developer performed
- **Lua source code**: If embedded in memory

**Best practice**: Treat crash reports as **development-only artifacts**. Don't distribute publicly without scrubbing sensitive data.

### Shipping Builds

In **Shipping** builds, consider:
- Minidump generation can be **disabled** (only JSON + log)
- Crash reports are saved but **not auto-uploaded** anywhere
- Developers must manually retrieve from `%APPDATA%/UzEngine/`
- Game can prompt user to report crashes (with consent)

---

## Cleanup

Crash reports accumulate in the crashes directory. The engine can optionally clean up old reports:

```cpp
// src/core/crash_reporting.h

void cleanup_old_crashes(int max_reports = 50, int max_age_days = 30);
```

**Lifecycle**:
- Keep last 50 crash reports
- Delete reports older than 30 days
- Run on engine startup (optional, can be manual)

---

## Logging Integration

All crash data is also written to the **engine.log** file:

```
[2025-03-01 14:30:45] [FATAL] ========================================
[2025-03-01 14:30:45] [FATAL] UNHANDLED EXCEPTION
[2025-03-01 14:30:45] [FATAL] Code: 0xC0000005 (ACCESS_VIOLATION)
[2025-03-01 14:30:45] [FATAL] Address: 0x7FF8A2C3D410
[2025-03-01 14:30:45] [FATAL] Callstack:
[2025-03-01 14:30:45] [FATAL]   0: Renderer::render_shadow_pass @ shadow_pass.cpp:142
[2025-03-01 14:30:45] [FATAL]   1: Renderer::render_frame @ renderer.cpp:89
[2025-03-01 14:30:45] [FATAL]   2: main_loop @ main.cpp:156
[2025-03-01 14:30:45] [FATAL] ========================================
[2025-03-01 14:30:45] [FATAL] Crash report saved to:
[2025-03-01 14:30:45] [FATAL] C:\Users\bolado\AppData\Local\UzEngine\my-game\crashes\
[2025-03-01 14:30:45] [FATAL]   crash_2025-03-01T14_30_45_Z.zip
[2025-03-01 14:30:45] [FATAL] ========================================
```

---

## AI-Assisted Debugging

The crash report structure is **designed for AI analysis**:

1. **Claude Code** receives crash report (JSON)
2. Claude parses exception code, callstack, engine state
3. Claude searches codebase for root cause patterns
4. Claude suggests fixes based on crash data

**Example Claude interaction**:
```
User: "The game crashes with ACCESS_VIOLATION at 0x7FF8A2C3D410"
Claude: "Based on the crash report, the fault is in Renderer::render_shadow_pass() line 142.
The callstack shows null pointer dereference in shadow_system_.render().
Let me check the shadow system code..."
```

---

## Compression & Storage

Crash reports are stored as **`.zip`** files for:
- **Compression**: Minidump + JSON + log can be 50–200 MB; zipped ~10–50 MB
- **Portability**: Single file containing all crash data
- **Organization**: Each crash timestamped and isolated

---

## Performance Impact

- **Exception handling installation**: ~0 ms (one-time at startup)
- **Normal execution**: No impact (handler only triggered on exception)
- **Crash capture**: ~100–500 ms (minidump write, file I/O)
- **Total crash-to-shutdown**: ~1–2 seconds

---

## CMake Integration

```cmake
# CMakeLists.txt

if(MSVC)
    target_compile_definitions(UzEngine PRIVATE UZ_CRASH_REPORTING=1)
    target_link_libraries(UzEngine PRIVATE DbgHelp.lib)
endif()

option(UZ_MINIDUMP "Enable minidump generation (disable for Shipping)" ON)
if(UZ_MINIDUMP)
    target_compile_definitions(UzEngine PRIVATE UZ_MINIDUMP=1)
endif()
```

---

## Crash Report Analysis Workflow

### Developer Workflow

1. **Engine crashes** during game development
2. **Extension notification**: "Engine crashed — Open Report"
3. **Open crash.json**: View exception code, callstack, engine state
4. **Open crash.dmp in VS**: Inspect variables, set breakpoints, debug
5. **Fix root cause** in source code
6. **Rebuild** and test

### AI-Assisted Workflow

1. **Claude Code** loads crash report via IPC
2. **Parse crash.json**: Exception code, callstack, engine state
3. **Search codebase**: Look for null dereference, memory issue patterns
4. **Suggest fix**: "The shadow system wasn't initialized. Add nullptr check at line 142."
5. **Apply fix** (optional): Claude modifies source code
6. **Rebuild** and test

---

## Related Documentation

- [../engine/lifecycle-and-concurrency.md](../engine/lifecycle-and-concurrency.md) — Exception handling, thread safety
- [../guides/ipc-mcp.md](../guides/ipc-mcp.md) — IPC handlers, extension integration
- [../debug-profiling.md](../debug-profiling.md) — Assert system, logging

