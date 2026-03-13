# Platform — Crash Handling & Diagnostics

> **Status**: Done (Version 4.5.0)
> **Source**: `platform/windows/crash_handler.h/cpp`, `platform/windows/stack_walker.h/cpp`
> **Depends on**: Win32 SEH, DbgHelp, MiniDump, DRED

## Purpose

When the engine crashes, developers need actionable diagnostics: stack trace, GPU state (DRED breadcrumbs), active scene, frame number, and system information. UzEngine captures crashes via SEH (Structured Exception Handling), writes minidumps for post-mortem debugging, and produces human-readable crash logs with all context. This enables diagnosing user crashes on remote machines without interactive debugging.

The crash handler is exception-safe: all buffers are stack-local or global, with no heap allocation in the crash path (exception filter runs with severely limited resources).

## Architecture

The crash handler implements a three-stage pipeline triggered by unhandled exceptions:

```
                Windows Exception (e.g., 0xC0000005)
                          │
                          ▼
            ┌─────────────────────────────┐
            │  SEH Exception Filter       │
            │  (SetUnhandledExceptionFilter)
            └─────────────────────────────┘
                          │
                          ▼
        ┌───────────────────────────────────────┐
        │ 1. Capture Stack Trace (Exception)    │
        │    - CaptureRawStackTrace() [lock-free]
        │    - No DbgHelp (too slow, not safe) │
        │    - 64-frame depth buffer (stack)    │
        └───────────────────────────────────────┘
                          │
                          ▼
        ┌───────────────────────────────────────┐
        │ 2. Populate CrashContext              │
        │    - exceptionCode, exceptionAddress  │
        │    - frameNumber, sceneName, lastRhiOp│
        │    - gpuAdapter, gpuDeviceRemoved     │
        │    - stackFrames[] + stackFrameCount  │
        └───────────────────────────────────────┘
                          │
                          ▼
        ┌───────────────────────────────────────┐
        │ 3. Write Output Files                 │
        │    (all to {exe_dir}/crashes/YYYYMMDD_HHMMSS/)
        │    ├── crash.dmp (minidump)           │
        │    ├── crash.txt (human-readable)     │
        │    └── crash.json (structured)        │
        └───────────────────────────────────────┘
```

**Key Design Decisions:**

- **No heap allocation in exception filter** — all buffers are stack-local (`CrashContext` has fixed-size char arrays, `stackFrames` is a 64-element void* array)
- **No DbgHelp in exception context** — symbol resolution happens later during crash report analysis (via PDB files), not in the handler
- **Raw stack trace capture only** — `CaptureRawStackTrace()` uses native `CaptureStackBackTrace()`, lock-free and safe
- **Thread-frozen exception handler** — all other threads are suspended when the exception filter runs, so no race conditions on global crash context

## CrashContext Struct

The `CrashContext` structure holds all state captured at crash time. All string fields use fixed-size buffers (256 bytes) to avoid heap allocation:

```cpp
struct CrashContext {
    // Exception information (from EXCEPTION_POINTERS)
    uint32_t exceptionCode;           // Windows exception code (0xC0000005 = AV)
    void* exceptionAddress;           // Instruction pointer where crash occurred

    // Engine state (updated by subsystems each frame)
    uint64_t frameNumber;             // Frame counter from renderer
    char sceneName[256];              // Active scene (from scene system)
    char lastRhiOp[256];              // Last GPU operation (from RHI)

    // Hardware information (from RHI init)
    char gpuAdapter[256];             // GPU description (e.g., "NVIDIA GeForce RTX 4090")
    bool gpuDeviceRemoved;            // True if DXGI_ERROR_DEVICE_REMOVED detected

    // Stack trace (raw addresses, no symbols in crash context)
    static constexpr int kMaxStackFrames = 64;
    void* stackFrames[kMaxStackFrames];  // Raw instruction pointers
    int stackFrameCount;                 // Number of valid frames captured
};
```

### Field Initialization

Each field is populated by different engine subsystems:

| Field | Source | When | Example |
|-------|--------|------|---------|
| `exceptionCode` | Windows EXCEPTION_POINTERS | Exception occurs | `0xC0000005` (access violation) |
| `exceptionAddress` | Windows EXCEPTION_POINTERS | Exception occurs | `0x00007FF123456789` (instruction pointer) |
| `frameNumber` | `CrashHandler_SetFrameNumber()` | Each render frame | `12345` |
| `sceneName` | `CrashHandler_SetSceneName()` | Scene load | `"gameplay_level_01"` |
| `lastRhiOp` | `CrashHandler_SetLastRhiOp()` | Before GPU submit | `"DrawIndexedInstanced"`, `"Present"` |
| `gpuAdapter` | `CrashHandler_SetGpuAdapter()` | RHI device init | `"NVIDIA GeForce RTX 4090"` |
| `gpuDeviceRemoved` | `CrashHandler_SetGpuDeviceRemoved()` | DXGI error detection | `true` / `false` |
| `stackFrames[]` | `CaptureRawStackTrace()` | Exception filter | Array of void* (raw addresses) |
| `stackFrameCount` | `CaptureRawStackTrace()` | Exception filter | Frame count (0–64) |

## Installation & Shutdown

```cpp
/// Install the SEH unhandled exception filter at engine startup.
///
/// Must be called after LogInit() (crash handler uses UZ_LOG).
/// Thread-safe and idempotent (safe to call multiple times).
void InstallCrashHandler();

/// Remove the crash handler and restore the previous exception filter.
///
/// Call at engine shutdown before LogShutdown().
void UninstallCrashHandler();
```

### Integration into Engine Startup

```cpp
int main() {
    uz::LogInit();
    uz::InstallCrashHandler();  // Install SEH filter (after logging ready)

    // ... engine initialization ...
    uz::RHI_CreateDevice();     // This will call CrashHandler_SetGpuAdapter()
    uz::RenderingInit();

    // Main loop
    while (running) {
        uint64_t frameNum = GetFrameCount();
        uz::CrashHandler_SetFrameNumber(frameNum);

        // ... update scene ...
        uz::CrashHandler_SetSceneName("my_scene");

        // ... render ...
        uz::CrashHandler_SetLastRhiOp("DrawIndexedInstanced");
        RHI_Submit();

        Present();
    }

    uz::UninstallCrashHandler();
    uz::LogShutdown();
    return 0;
}
```

## Stack Walker API

The Stack Walker module provides safe stack trace capture with or without symbol resolution.

### StackFrame Struct

```cpp
struct StackFrame {
    void* address;            // Instruction pointer address
    char functionName[256];   // Demangled function name, or "???" if unresolved
    char fileName[256];       // Source file path, or "" if unavailable
    int lineNumber;           // Source line number, or 0 if unavailable
};
```

### APIs

#### 1. `WalkStack()` — Capture & Resolve with Symbols

```cpp
/// Captures and resolves the current thread's stack trace.
///
/// Uses CaptureStackBackTrace() to capture raw frames, then queries
/// DbgHelp (SymFromAddr/SymGetLineFromAddr64) for symbols and source info.
///
/// DbgHelp is protected by an internal mutex (not thread-safe natively).
/// PDB files must be present next to the executable for symbol resolution.
///
/// # Arguments
/// - frames: Output buffer for resolved StackFrame structs
/// - maxFrames: Capacity (max 64 recommended)
/// - skipFrames: Number of top frames to skip (default 1 skips WalkStack itself)
///
/// # Returns
/// Number of frames actually resolved (0 if capture fails)
///
/// # Thread Safety
/// Thread-safe via internal mutex for DbgHelp serialization
int WalkStack(StackFrame* frames, int maxFrames, int skipFrames = 1);
```

**Example — Diagnostic Stack Trace (normal code context):**

```cpp
uz::StackFrame frames[32];
int count = uz::WalkStack(frames, 32, 1);  // Skip WalkStack itself

for (int i = 0; i < count; ++i) {
    UZ_LOG(Debug, Info,
        "[{}] {} ({}:{})",
        i,
        frames[i].functionName,
        frames[i].fileName,
        frames[i].lineNumber);
}
```

#### 2. `CaptureRawStackTrace()` — Fast Capture (No Symbols)

```cpp
/// Captures raw instruction pointers without symbol resolution.
///
/// Safe to call from exception/crash context. Uses only CaptureStackBackTrace()
/// (no heap, no DbgHelp calls). Resolves symbols later from PDB files during
/// crash report analysis.
///
/// # Arguments
/// - frames: Output buffer for void* pointers
/// - maxFrames: Capacity
/// - skipFrames: Number of frames to skip (default 1)
///
/// # Returns
/// Number of frames captured
int CaptureRawStackTrace(void* frames[], int maxFrames, int skipFrames = 1);
```

**Example — Crash Handler Usage (exception context):**

```cpp
void* rawFrames[64];
int frameCount = uz::CaptureRawStackTrace(rawFrames, 64, 0);

// Store in CrashContext for later analysis
ctx.stackFrameCount = frameCount;
for (int i = 0; i < frameCount; ++i) {
    ctx.stackFrames[i] = rawFrames[i];
}
```

#### 3. `FormatStackTrace()` — Pretty Print

```cpp
/// Formats resolved stack frames as human-readable multi-line string.
///
/// Output format:
///   [N] FunctionName (file.cpp:123) [0x00007FF123456789]
///
/// If source info is unavailable, the file/line part is omitted.
///
/// # Returns
/// Formatted string (empty if count is 0 or frames is nullptr)
std::string FormatStackTrace(const StackFrame* frames, int count);
```

**Output Example:**

```
[0] uz::Renderer::SubmitDrawCall (renderer.cpp:234) [0x00007FF123456789]
[1] uz::Scene::Render (scene.cpp:567) [0x00007FF12345678A]
[2] uz::Engine::Update (engine.cpp:89) [0x00007FF12345678B]
[3] main (main.cpp:45) [0x00007FF12345678C]
```

### Exception Safety Notes

- **`WalkStack()`** requires heap allocation (for DbgHelp internal buffers) and is **NOT safe in crash context**
- **`CaptureRawStackTrace()`** is **safe in crash context** — only uses stack and Windows APIs (no malloc)
- **Symbol resolution** happens after crash handler exits, during offline crash report analysis

## Output Files

All crash files are written to a timestamped directory: `{exe_dir}/crashes/YYYY-MM-DD_HH-MM-SS/`

### 1. `crash.dmp` — Minidump

Windows minidump format for post-mortem debugging in WinDbg or Visual Studio Debugger.

**Contents:**
- Process memory snapshot
- All thread contexts and stacks
- Loaded module list
- System information (CPU, OS version, etc.)

**Creation:**
```cpp
MiniDumpWriteDump(
    GetCurrentProcess(),
    GetCurrentProcessId(),
    dumpFileHandle,
    MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory,
    &exceptionPointers,
    nullptr,
    nullptr);
```

**Analysis:**
```
1. Open crash.dmp in Visual Studio or WinDbg
2. Load PDB symbols from the same build
3. View stack traces, local variables, memory, etc.
```

### 2. `crash.txt` — Human-Readable Report

Plain text format for developers (readable in any editor).

**Example:**

```
=== UzEngine Crash Report ===

Timestamp: 2026-03-12 14:35:22
Frame: 12345
Scene: gameplay_level_01
GPU Adapter: NVIDIA GeForce RTX 4090
GPU Device Removed: false
Last RHI Operation: DrawIndexedInstanced
Exception Code: 0xC0000005 (Access Violation)
Exception Address: 0x00007FF123456789

Stack Trace:
[0] uz::Mesh::GetVertexBuffer (mesh.cpp:123) [0x00007FF123456789]
[1] uz::Renderer::SubmitDrawCall (renderer.cpp:234) [0x00007FF12345678A]
[2] uz::Scene::Render (scene.cpp:567) [0x00007FF12345678B]
[3] main (main.cpp:45) [0x00007FF12345678C]
```

**Format Details:**
- Single exception info block
- Symbol-resolved stack trace (from crash.json stack frames + PDB files)
- Readable timestamps and GPU state
- Useful for bug reports and telemetry

### 3. `crash.json` — Structured Report

JSON format for machine parsing and telemetry systems.

**Example:**

```json
{
  "timestamp": "2026-03-12T14:35:22Z",
  "frame_number": 12345,
  "scene_name": "gameplay_level_01",
  "exception": {
    "code": "0xC0000005",
    "code_name": "EXCEPTION_ACCESS_VIOLATION",
    "address": "0x00007FF123456789"
  },
  "gpu": {
    "adapter": "NVIDIA GeForce RTX 4090",
    "device_removed": false,
    "last_rhi_op": "DrawIndexedInstanced"
  },
  "stack_frames": [
    {
      "index": 0,
      "address": "0x00007FF123456789",
      "function_name": "uz::Mesh::GetVertexBuffer",
      "file_name": "mesh.cpp",
      "line_number": 123
    },
    {
      "index": 1,
      "address": "0x00007FF12345678A",
      "function_name": "uz::Renderer::SubmitDrawCall",
      "file_name": "renderer.cpp",
      "line_number": 234
    }
  ]
}
```

**Schema Fields:**
- `timestamp`: ISO 8601 format
- `frame_number`: Engine frame counter
- `scene_name`: Active scene at crash
- `exception.code`: Windows exception code (hex)
- `exception.code_name`: Human-readable name
- `exception.address`: Instruction pointer (hex)
- `gpu.adapter`: GPU name
- `gpu.device_removed`: DXGI device removal flag
- `gpu.last_rhi_op`: Last RHI operation
- `stack_frames[]`: Array of resolved stack frames with source info

**Parsing Example (Python):**

```python
import json

with open("crash.json") as f:
    report = json.load(f)

print(f"Frame {report['frame_number']} in scene '{report['scene_name']}'")
print(f"GPU: {report['gpu']['adapter']}")
print(f"Exception: {report['exception']['code_name']}")

for frame in report['stack_frames']:
    print(f"  {frame['function_name']} ({frame['file_name']}:{frame['line_number']})")
```

## Integration Points

The crash handler API provides functions to update global engine state that will be captured if a crash occurs:

### `CrashHandler_SetFrameNumber(uint64_t frame)`

Called once per frame from the renderer's main loop.

```cpp
// In Renderer::Update() or similar
uz::CrashHandler_SetFrameNumber(frameCount);
```

**Thread Safety:** Atomic store (thread-safe)

---

### `CrashHandler_SetSceneName(const char* name)`

Called when a scene is loaded or changed.

```cpp
// In SceneManager::LoadScene()
uz::CrashHandler_SetSceneName("gameplay_level_01");

// Clear scene name if unloading
uz::CrashHandler_SetSceneName(nullptr);
```

**Behavior:**
- Copies `name` into internal 256-byte buffer
- Passing `nullptr` clears the buffer

**Thread Safety:** Safe for single-writer (scene system). Crash handler only reads after exception freezes all threads.

---

### `CrashHandler_SetLastRhiOp(const char* op)`

Called before each significant RHI/GPU operation.

```cpp
// In RHI::DrawIndexedInstanced()
uz::CrashHandler_SetLastRhiOp("DrawIndexedInstanced");

// In RHI::Present()
uz::CrashHandler_SetLastRhiOp("Present");

// Clear when not in flight
uz::CrashHandler_SetLastRhiOp(nullptr);
```

**Examples:**
- `"DrawIndexedInstanced"`
- `"Present"`
- `"CopyResource"`
- `"ExecuteCommandList"`

**Thread Safety:** Safe for single-writer (RHI thread)

---

### `CrashHandler_SetGpuAdapter(const char* adapter)`

Called once at RHI initialization after enumerating the GPU.

```cpp
// In RHI_CreateDevice()
uz::CrashHandler_SetGpuAdapter("NVIDIA GeForce RTX 4090");
```

**Thread Safety:** Safe for single-writer (RHI init)

---

### `CrashHandler_SetGpuDeviceRemoved(bool removed)`

Called when DXGI_ERROR_DEVICE_REMOVED is detected.

```cpp
// In RHI::CheckDeviceRemoval()
if (device->GetDeviceRemovedReason() != S_OK) {
    uz::CrashHandler_SetGpuDeviceRemoved(true);
    UZ_LOG(RHI, Error, "GPU device removed (TDR/driver timeout)");
}
```

**Thread Safety:** Atomic store (thread-safe)

## Exception Safety

The crash handler prioritizes safety in a severely resource-constrained environment (exception filter):

### Why Stack-Only Buffers?

When an unhandled exception occurs:
- The heap may be corrupted
- Memory allocators are not async-signal-safe
- Most C++ runtime features cannot be used
- Calling `new`, `malloc()`, or STL allocators is **unsafe and may recurse infinitely**

**Solution:** All crash handler buffers are:
- **Stack-local** (automatic storage): `CrashContext` (fixed-size struct ~800 bytes), `void* stackFrames[64]` (512 bytes)
- **Global static** (pre-allocated): EXCEPTION_POINTERS capture, crash directory path
- **Fixed-size char arrays** (256 bytes each): `sceneName`, `lastRhiOp`, `gpuAdapter`

### Why No DbgHelp in Crash Context?

DbgHelp is not designed for exception context:
- Allocates heap memory internally (unsafe during heap corruption)
- Uses mutexes (not exception-safe)
- Lookups can fail silently (no diagnostic value in crash handler)

**Solution:** Only capture raw instruction pointers (`CaptureRawStackTrace()`) in the exception filter. Symbol resolution happens later, offline, using PDB files.

### Implementation Pattern

```cpp
// Inside the SEH exception filter (EXCEPTION_POINTERS available)

// ✅ SAFE: Stack-local struct, fixed buffers, no allocations
CrashContext ctx{};

// ✅ SAFE: Windows native call, lock-free, no malloc
uz::CaptureRawStackTrace(
    ctx.stackFrames,
    uz::CrashContext::kMaxStackFrames,
    0);

// ✅ SAFE: Copy global state (set by engine before crash)
ctx.frameNumber = g_crashContext.frameNumber;
memcpy(ctx.sceneName, g_crashContext.sceneName, sizeof(ctx.sceneName));

// ✅ SAFE: snprintf into stack buffer
char crashDirPath[MAX_PATH];
snprintf(crashDirPath, sizeof(crashDirPath),
    "%s\\crashes\\%04d-%02d-%02d_%02d-%02d-%02d",
    exeDirPath, year, month, day, hour, minute, second);

// ✅ SAFE: CreateDirectory, CreateFile, MiniDumpWriteDump (Windows APIs)
// (But keep error handling minimal — can't log or throw)

// ❌ NEVER: Any heap allocation
// new, malloc, std::string, std::vector, etc.

// ❌ NEVER: Mutex-protected code
// (DbgHelp, logging, etc.)

// ❌ NEVER: Exception handling or throwing
// (May cause infinite recursion)
```

## Workflow Example

Complete example showing crash handler initialization and frame-by-frame updates:

```cpp
#include "platform/windows/crash_handler.h"
#include "platform/windows/stack_walker.h"

int main() {
    // Startup
    uz::LogInit();
    uz::InstallCrashHandler();  // SEH filter active

    // RHI initialization (sets GPU info)
    uz::RHI_CreateDevice("NVIDIA GeForce RTX 4090");
    // ^ Internally calls: CrashHandler_SetGpuAdapter(...)

    // Main loop
    uint64_t frameCount = 0;
    bool running = true;
    while (running) {
        frameCount++;
        uz::CrashHandler_SetFrameNumber(frameCount);

        // Load scene
        if (shouldLoadNewScene) {
            uz::CrashHandler_SetSceneName("gameplay_level_01");
        }

        // Update & render
        scene.Update(deltaTime);
        uz::CrashHandler_SetLastRhiOp("DrawIndexedInstanced");
        renderer.SubmitDrawCalls();
        uz::CrashHandler_SetLastRhiOp("Present");
        swapChain.Present();

        // GPU health check
        if (device.GetDeviceRemovedReason() != S_OK) {
            uz::CrashHandler_SetGpuDeviceRemoved(true);
            running = false;  // Graceful shutdown on TDR
        }
    }

    // Shutdown
    uz::UninstallCrashHandler();
    uz::LogShutdown();
    return 0;
}
```

If a crash occurs (e.g., `nullptr` dereference in frame 12345):

```
1. Windows SEH triggers exception filter
2. Exception filter captures:
   - Frame: 12345
   - Scene: "gameplay_level_01"
   - Last RHI Op: "Present"
   - Stack trace (64 raw addresses)
   - GPU: RTX 4090, device_removed = false
3. Writes to {exe_dir}/crashes/2026-03-12_14-35-22/:
   - crash.dmp (minidump for WinDbg)
   - crash.txt (human-readable report)
   - crash.json (structured telemetry)
4. OS shows crash dialog
5. Developer analyzes:
   - crash.txt: "Frame 12345 in gameplay_level_01, crashed in Present()"
   - PDB files + crash.json: resolves stack to exact source location
```

## Best Practices

**DO:**
- Call `InstallCrashHandler()` immediately after `LogInit()`
- Update frame number every frame (low cost, high value)
- Update scene name on scene load (rare events)
- Update RHI op before major GPU submissions (optional but recommended)
- Include `crash.json` with telemetry uploads

**DON'T:**
- Throw exceptions from crash handler (causes infinite recursion)
- Allocate heap in exception context (may crash if heap is corrupted)
- Use DbgHelp in exception filter (not exception-safe)
- Skip `UninstallCrashHandler()` on shutdown (restores previous filter)
- Call `WalkStack()` from crash context (use `CaptureRawStackTrace()` instead)

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Handler mechanism | `FGenericPlatformMisc::HandleEnsure()` + SEH | `CrashHandlerException` + signal handlers | `SetUnhandledExceptionFilter` + SEH |
| Stack capture | DbgHelp in filter (risky) | `backtrace()` on Linux | `CaptureRawStackTrace()` (safe) |
| Symbol resolution | In filter + PDB | Post-crash via external tool | Post-crash via PDB + crash.json |
| MiniDump | Yes, with DXGI integration | No native minidump | Yes, `MiniDumpWriteDump()` |
| Context capture | Full (scene, GPU, frame) | Minimal | Frame, scene, GPU, device-removed |
| Output format | Binary `.dmp` + telemetry | Text log | `.dmp` + `.txt` + `.json` |
| Exception safety | Partial (some DbgHelp calls) | Basic | Strict (stack buffers only) |
