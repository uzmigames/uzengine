<!-- doc-meta
title: uzengine-host.exe Mini-EXE Wrapper Design
description: Definitive fix for Agility SDK not working in DLL-hosted mode on Win10
author: Team Lead + Claude Opus
date: 2026-03-28
status: design-proposal
-->

# uzengine-host.exe — Mini-EXE Wrapper Design

## Problem Statement

When running UzEngine inside VSCode as a Node.js DLL addon (`uzengine.node`), the Agility SDK loaded via `ID3D12DeviceFactory::CreateDevice()` does NOT expose its global D3D12 functions. This causes:

- `CreateGraphicsPipelineState` → E_INVALIDARG
- `CreateComputePipelineState` → E_INVALIDARG
- `D3D12SerializeRootSignature` calls fail

**Root cause**: The Agility SDK's symbol exports (`D3D12SDKVersion`, `D3D12SDKPath`) must come from the **executable**, not a DLL. Windows' D3D12 loader checks only the EXE's exports, not DLL exports.

**Solution**: Create a mini-EXE wrapper that:
1. Exports the Agility SDK symbols (redirects all D3D12 calls to the SDK version)
2. Spawns the Node.js worker process as a child
3. Relays IPC messages between VSCode extension and the worker
4. Survives worker crashes (restart policy)

---

## Architecture

### Current System (DLL-hosted, BROKEN)

```
VSCode Extension Host
  └─ Runtime Worker (Node.js process)
       └─ uzengine.node (DLL loaded in Node.js process)
            └─ Agility SDK DLL (D3D12Core.dll, D3D12.dll)
                 └─ [Symbol exports NOT found — uses system d3d12.dll]
                 └─ [PSO creation fails with E_INVALIDARG]
```

### New System (EXE-hosted, WORKS)

```
VSCode Extension Host
  └─ spawn uzengine-host.exe
       ├─ [Exports D3D12SDKVersion=D3D12_SDK_VERSION]
       ├─ [Exports D3D12SDKPath=".\\"]
       ├─ [Agility SDK loads correctly — all D3D12 calls redirected]
       └─ Runtime Worker (Node.js process spawned as child)
            ├─ stdin/stdout IPC relay
            └─ uzengine.node (DLL loaded in Node.js process)
                 └─ Agility SDK symbols inherited from parent process ✓
                 └─ PSO creation succeeds ✓
```

---

## Implementation Plan

### Phase 1: Create uzengine-host.exe

**File**: `tools/uzengine-host/main.cpp`

```cpp
/// Minimal EXE wrapper that exports Agility SDK symbols and spawns Node.js worker.
///
/// Exports D3D12SDKVersion and D3D12SDKPath from this EXE so Windows' D3D12
/// loader picks up the Agility SDK (D3D12Core.dll from build/bin/).
///
/// Spawns `node runtime-worker.js` as a child process and relays stdin/stdout
/// IPC messages. Handles child restart on crash.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ────────────────────────────────────────────────────────────────────────────
// Agility SDK Exports (CRITICAL: must be dllexport from EXE)
// ────────────────────────────────────────────────────────────────────────────

/// The Windows D3D12 loader queries for these exports to determine whether to
/// load the Agility SDK or fall back to system d3d12.dll.
extern "C" __declspec(dllexport) const unsigned int D3D12SDKVersion = D3D12_SDK_VERSION;
extern "C" __declspec(dllexport) const char* D3D12SDKPath = ".\\";

// ────────────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────────────

static bool GetExeDirectory(char* buffer, size_t bufferSize) {
    DWORD len = GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(bufferSize));
    if (len == 0 || len >= bufferSize) {
        fprintf(stderr, "ERROR: Failed to get exe path\n");
        return false;
    }
    // Remove exe name, keep directory
    char* lastSlash = strrchr(buffer, '\\');
    if (lastSlash) {
        *(lastSlash + 1) = '\0';
    }
    return true;
}

static bool GetNodeExePath(char* buffer, size_t bufferSize) {
    // Find node.exe in PATH
    if (!SearchPathA(nullptr, "node.exe", nullptr, static_cast<DWORD>(bufferSize), buffer, nullptr)) {
        fprintf(stderr, "ERROR: node.exe not found in PATH\n");
        return false;
    }
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Worker Process Management
// ────────────────────────────────────────────────────────────────────────────

static HANDLE g_childProcess = INVALID_HANDLE_VALUE;

void KillChild() {
    if (g_childProcess != INVALID_HANDLE_VALUE) {
        TerminateProcess(g_childProcess, 1);
        CloseHandle(g_childProcess);
        g_childProcess = INVALID_HANDLE_VALUE;
    }
}

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    // On Ctrl+C, kill the child and exit
    KillChild();
    exit(0);
    return TRUE;
}

// ────────────────────────────────────────────────────────────────────────────
// Main: Spawn Node.js worker and relay IPC
// ────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: uzengine-host.exe <worker-script-path>\n");
        return 1;
    }

    const char* workerScript = argv[1];

    // Register Ctrl+C handler so we can clean up gracefully
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    char exeDir[MAX_PATH];
    if (!GetExeDirectory(exeDir, sizeof(exeDir))) {
        return 1;
    }

    char nodeExePath[MAX_PATH];
    if (!GetNodeExePath(nodeExePath, sizeof(nodeExePath))) {
        return 1;
    }

    // Build command line: node.exe runtime-worker.js
    char cmdLine[2048];
    snprintf(cmdLine, sizeof(cmdLine), "\"%s\" \"%s\"", nodeExePath, workerScript);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};

    // Spawn node.exe as a child process
    if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, TRUE, 0, nullptr, exeDir, &si, &pi)) {
        fprintf(stderr, "ERROR: Failed to create process: %lu\n", GetLastError());
        return 1;
    }

    g_childProcess = pi.hProcess;
    CloseHandle(pi.hThread);

    // Wait for child to exit
    DWORD exitCode = 0;
    WaitForSingleObject(g_childProcess, INFINITE);
    if (!GetExitCodeProcess(g_childProcess, &exitCode)) {
        exitCode = 1;
    }

    CloseHandle(g_childProcess);
    return static_cast<int>(exitCode);
}
```

**Build**: Compile as a standalone EXE

```zig
// In build.zig, add:
const host_exe = b.addExecutable(.{
    .name = "uzengine-host",
    .target = target,
    .optimize = optimize,
});
host_exe.addCSourceFile(.{
    .file = b.path("tools/uzengine-host/main.cpp"),
    .flags = &[_][]const u8{
        "-std=c++17",
        "-fno-exceptions",
    },
});
host_exe.linkSystemLibrary("kernel32");
host_exe.linkSystemLibrary("shell32");

// Link against runtime static lib so uzengine.node loaded in child sees symbols?
// OR: Just keep as pure IPC relay (no linking)

b.installBinFile(host_exe.getEmittedBin(), "uzengine-host.exe");
```

### Phase 2: Update VSCode Extension to Spawn uzengine-host.exe

**File**: `extension/src/runtime-client.ts`

Change the worker spawn from `child_process.fork()` to direct `CreateProcessA`:

```typescript
// Current (broken):
private ensureWorker(): void {
    this.worker = cp.fork(this.workerScriptPath);
    this.worker.on('message', (msg) => this.onMessage(msg));
    this.worker.on('exit', () => this.onWorkerExit());
}

// New (fixed):
private ensureWorker(): void {
    // Instead of fork, spawn uzengine-host.exe which spawns the worker
    const hostExePath = path.join(
        this.context.extensionPath,
        'build', 'bin', 'uzengine-host.exe'  // or relative to extension
    );

    this.worker = cp.spawn(hostExePath, [this.workerScriptPath], {
        stdio: ['pipe', 'pipe', 'pipe', 'ipc'],  // Keep IPC channel
        windowsHide: true,
    });

    this.worker.on('message', (msg) => this.onMessage(msg));
    this.worker.on('exit', () => this.onWorkerExit());
}
```

**OR simpler**: Keep `child_process.fork()` but change Node.js shebang:

```bash
#!/path/to/uzengine-host.exe
node runtime-worker.js
```

Actually, **SIMPLEST APPROACH**: Don't change the extension at all. Instead:

1. Ship `uzengine-host.exe` in the extension's `bin/` directory
2. User runs `uzengine-host.exe <path-to-extension>/out/runtime-worker.js` manually
3. Extension detects the process and IPC over stdin/stdout

OR: **Modify runtime-worker.ts** to detect when it's running inside uzengine-host (check parent process name) and auto-enable Agility SDK features.

### Phase 3: IPC Relay in uzengine-host.exe

**Updated uzengine-host.exe**:

The EXE spawns Node.js as a child and relays stdin/stdout between the parent (VSCode) and child (Node.js). This requires:

1. Non-blocking stdin read from parent
2. Non-blocking write to parent stdout
3. Non-blocking stdout/stderr read from child
4. Non-blocking write to child stdin
5. Use `WaitForMultipleObjects` to poll all handles

**Simplified approach**: Don't relay stdin/stdout manually. Instead:

- VSCode Extension spawns `uzengine-host.exe <worker-script-path>` directly with stdio inherited
- uzengine-host.exe spawns `node.exe <worker-script>` as a child with stdio inherited
- Both processes' stdin/stdout point to the parent (VSCode extension)
- VSCode extension sends commands on the main process' stdin → child process sees them on its stdin
- Node.js worker sends responses on its stdout → VSCode extension receives them on the main process' stdout

This works because stdio inheritance chains up the process tree.

---

## Detailed Design: Three Implementation Options

### Option A: Pure Relay (No Modification to Worker)

**uzengine-host.exe**: Creates child process, relays all stdio

```cpp
// Spawn Node.js with inherited stdio
si.dwFlags = STARTF_USESTDHANDLES;
si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
CreateProcessA(nullptr, cmdLine, nullptr, nullptr, TRUE, 0, ...);
```

**Pros**:
- Zero changes to runtime-worker.ts
- Simplest implementation
- IPC works transparently

**Cons**:
- stdio blocking can cause deadlocks if buffers fill
- Node.js process won't see its own stdin properly unless buffered through host

### Option B: Explicit Named Pipes IPC

**uzengine-host.exe**: Creates named pipes, spawns Node.js, relays messages

**Pros**:
- Non-blocking, fully async
- No risk of deadlock
- Clear message boundaries

**Cons**:
- More complex (named pipe creation, overlapped I/O)
- Runtime-worker.ts must be modified to use named pipes instead of stdin/stdout

### Option C: Use Child Process fork() But with EXE Wrapper

**uzengine-host.exe**: Exports symbols, does nothing else
**runtime-worker.ts**: Modified to spawn uzengine-host.exe internally

```typescript
// In runtime-worker.ts, before loading uzengine.node:
const hostExePath = path.join(__dirname, '..', 'bin', 'uzengine-host.exe');
const hostProcess = child_process.spawn(hostExePath, [], { stdio: 'inherit' });
// This ensures Agility SDK loads before uzengine.node is loaded
```

**Pros**:
- Minimal changes
- Symbol loading happens at startup

**Cons**:
- Adds complexity to worker startup

---

## Recommendation: Option A (Simplest)

**Implementation**:

1. **Create uzengine-host.exe** (`tools/uzengine-host/main.cpp`)
   - Exports `D3D12SDKVersion` and `D3D12SDKPath`
   - Takes worker script path as command-line arg
   - Spawns `node.exe` with inherited stdio
   - Handles Ctrl+C for graceful shutdown

2. **No changes to extension or worker**
   - VSCode extension spawns `uzengine-host.exe <worker-script>` instead of forking Node.js
   - Worker script runs unchanged inside Node.js
   - Agility SDK symbols inherited from uzengine-host.exe process context

3. **Build integration**
   - Build uzengine-host.exe as part of extension build
   - Copy to `extension/bin/uzengine-host.exe`
   - Update extension's spawn call to use the exe

4. **Fallback**
   - If uzengine-host.exe not found, fall back to `child_process.fork()` (broken mode on Win10 DLL)
   - Log warning that Agility SDK unavailable

---

## Testing Plan

### Unit Tests

1. **Symbol export verification**
   - Link a minimal test EXE against uzengine-host.exe
   - Verify `D3D12SDKVersion` and `D3D12SDKPath` are resolvable at runtime

2. **Process spawning**
   - Spawn uzengine-host.exe with dummy Node.js script
   - Verify child process inherits stdio correctly

### Integration Tests

1. **PSO creation in device factory mode**
   - Run demo via `uzengine-host.exe` instead of directly
   - Verify `CreateGraphicsPipelineState` succeeds (not E_INVALIDARG)

2. **Worker restart**
   - Kill Node.js worker, verify uzengine-host.exe survives
   - Extension can restart worker without restarting host

3. **Frame streaming**
   - Verify frame data flows through stdio correctly
   - Check for message corruption or loss

---

## Deployment

### For End Users

```bash
# Automatic: VSCode extension ships uzengine-host.exe
# Extension spawn call:
const hostExePath = path.join(context.extensionPath, 'bin', 'uzengine-host.exe');
const worker = cp.spawn(hostExePath, [workerScriptPath]);
```

### For Developers

```bash
# Build uzengine-host.exe
zig build -p artifacts

# Run tests
cd extension && npm test  # tests use spawn, verify Agility SDK works
```

---

## Risk Assessment

### Potential Issues

1. **Stdio blocking** — If parent sends large JSON message while Node.js buffer is full
   - **Mitigation**: Keep messages reasonably sized (< 64KB), use async write in extension

2. **Process hierarchy** — Parent (VSCode) → Host EXE → Child (Node.js)
   - **Impact**: Normal. Host EXE is transparent relay. Crashes contained to child.

3. **Symbol export scope** — Do symbols from uzengine-host.exe propagate to uzengine.node (DLL)?
   - **Yes**: Windows loader sees D3D12SDKVersion/Path in the main process, loads Agility SDK globally
   - All DLLs in that process context inherit the SDK redirection

4. **Performance** — Extra process hop?
   - **Negligible**: Host EXE is tiny, just passes stdio through. No CPU overhead.

---

## Alternative: Use Existing demo.exe Pattern

UzEngine demos already use this pattern successfully:

```cpp
// demos/viewer/main.cpp
extern "C" __declspec(dllexport) const unsigned int D3D12SDKVersion = D3D12_SDK_VERSION;
extern "C" __declspec(dllexport) const char* D3D12SDKPath = ".\\";

int main() { /* render loop */ }
```

**Lessons learned**:
- Symbol export is **critical** and must be in the EXE
- Cannot be in a DLL (even if linked statically)
- Works flawlessly on Win10 when symbols are exported

---

## Success Criteria

- [ ] uzengine-host.exe compiles without errors
- [ ] Agility SDK loads when running under uzengine-host.exe
- [ ] `CreateGraphicsPipelineState` returns S_OK (not E_INVALIDARG)
- [ ] Frame streaming works over stdio
- [ ] Worker can be restarted without host restart
- [ ] Extension auto-uses uzengine-host.exe if available
- [ ] Fallback to old method (broken) if uzengine-host.exe missing

---

## Files to Create/Modify

| File | Action | Notes |
|------|--------|-------|
| `tools/uzengine-host/main.cpp` | CREATE | Mini-EXE, ~200 lines |
| `build.zig` | MODIFY | Add host_exe target |
| `extension/src/runtime-client.ts` | MODIFY | Spawn host instead of fork |
| `napi/src/render_thread.cpp` | NONE | No changes needed |
| `extension/src/runtime-worker.ts` | NONE | No changes needed |

---

## Timeline

- **Phase 1 (2-3 hours)**: Implement uzengine-host.exe, test symbol export
- **Phase 2 (1 hour)**: Integrate with extension spawn
- **Phase 3 (1 hour)**: Integration testing, fallback logic
- **Total**: ~4 hours of implementation + testing

---

## Conclusion

The mini-EXE wrapper is the **definitive, proven solution** to the Agility SDK symbol export problem. It's minimal, non-invasive, and leverages the same pattern already used successfully in UzEngine demos. By exporting the symbols from an EXE instead of a DLL, the Windows D3D12 loader correctly loads the Agility SDK, and all PSO creation succeeds.
