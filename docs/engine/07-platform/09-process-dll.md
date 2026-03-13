# Platform — Process & DLL Management

> **Status**: Implemented
> **Source**: `platform/windows/dll_loader.h`, `platform/windows/process.h`, `platform/windows/shared_memory.h`, `platform/windows/environment.h`
> **Depends on**: Win32 `LoadLibraryW`/`FreeLibrary`/`GetProcAddress`, `CreateProcessW`, `CreateFileMapping`/`MapViewOfFile`, `GetEnvironmentVariable`/`SetEnvironmentVariable`

## Purpose

The engine dynamically loads plugins (game.dll hot-reload, optional render backends), spawns child processes (asset cooker, shader compiler), and shares memory for IPC. `IPlatformProcess` abstracts platform differences, enabling hot-reload, plugin systems, and external tool integration without platform-specific code in game logic.

## Architecture

```
IPlatformProcess
    │
    ├── DLL/Shared Object Loading
    │   ├── LoadDll(path) → handle
    │   ├── GetDllSymbol(handle, name) → function pointer
    │   └── FreeDll(handle)
    │
    ├── Process Creation
    │   ├── CreateProcess(exe, args, workDir) → handle
    │   ├── WaitForProcess(handle) → exit_code
    │   └── KillProcess(handle)
    │
    ├── Pipes & IPC
    │   ├── CreatePipe() → (read_handle, write_handle)
    │   └── CaptureProcessOutput(handle) → stdout/stderr
    │
    ├── Environment Variables
    │   ├── GetEnv(name) → value
    │   └── SetEnv(name, value)
    │
    └── Shared Memory
        ├── CreateSharedMemory(name, size) → handle
        └── OpenSharedMemory(name) → handle
```

## DLL Loading

Wraps Win32 LoadLibrary/FreeLibrary/GetProcAddress with UTF-8 path conversion and error logging.

### Load and Symbol Resolution

```cpp
/// Loads a DLL from a UTF-8 file path.
///
/// Internally converts to wide string and calls LoadLibraryW.
/// Logs error with GetLastError() on failure.
///
/// # Arguments
/// * `path` — UTF-8 encoded path to the DLL. Must not be nullptr.
///
/// # Returns
/// - Valid handle on success (cast of HMODULE).
/// - `nullptr` on failure (DLL not found, dependency missing, permission denied).
NativeDllHandle LoadDll(const char* path);

/// Retrieves the address of an exported symbol from a loaded DLL.
///
/// Calls GetProcAddress. Logs a warning on failure.
///
/// # Arguments
/// * `handle` — Handle returned by LoadDll(). Must not be nullptr.
/// * `symbolName` — Null-terminated ASCII symbol name (e.g., "GameInit", "OnUpdate").
///
/// # Returns
/// - Non-null pointer to the symbol on success.
/// - `nullptr` if symbol not found.
void* GetDllSymbol(NativeDllHandle handle, const char* symbolName);

/// Frees a previously loaded DLL.
///
/// Calls FreeLibrary. Null-safe (no-op if handle is nullptr).
///
/// # Arguments
/// * `handle` — Handle returned by LoadDll(), or nullptr.
void FreeDll(NativeDllHandle handle);
```

### Usage Example

```cpp
// Hot-reload game.dll
uz::NativeDllHandle game_dll = uz::LoadDll("game.dll");
if (!game_dll) {
    UZ_LOG(Engine, Error, "Failed to load game.dll");
    return;
}

// Get exported function pointer
using GameInitFn = void (*)(void);
auto game_init = reinterpret_cast<GameInitFn>(uz::GetDllSymbol(game_dll, "GameInit"));
if (!game_init) {
    UZ_LOG(Engine, Error, "game.dll missing GameInit export");
    uz::FreeDll(game_dll);
    return;
}

// Call the exported function
game_init();

// Later: hot-reload by loading new version
uz::FreeDll(game_dll);      // Unload old version
game_dll = uz::LoadDll("game_new.dll");  // Load new build
```

### Thread Safety

Each function is thread-safe (Win32 loader lock serializes internally). However, the loaded DLL's internal state may not be thread-safe — coordinate with game code.

## Process Creation

Spawns child processes with optional stdout/stderr pipe capture. Provides non-blocking reads, wait, and termination.

### Process Lifecycle

```cpp
/// Opaque handle bundle for a child process and its I/O pipes.
///
/// All fields are Win32 HANDLEs cast to `void*`.
/// Zero-initialized = invalid/closed.
struct ProcessHandle {
    void* hProcess = nullptr;     ///< Win32 process handle (HANDLE).
    void* hStdoutRead = nullptr;  ///< Read end of child stdout pipe.
    void* hStderrRead = nullptr;  ///< Read end of child stderr pipe.
    uint32_t processId = 0;       ///< Win32 process ID (PID).
};

/// Creates a child process with piped stdout and stderr.
///
/// Spawns the executable via CreateProcessW. The child's stdout and stderr
/// are redirected to anonymous pipes readable via ProcessReadStdout/Stderr.
///
/// # Arguments
/// * `exe` — UTF-8 path to the executable. Must not be nullptr.
/// * `args` — Optional UTF-8 command-line arguments (appended after exe).
/// * `workDir` — Optional UTF-8 working directory. nullptr = inherit.
///
/// # Returns
/// - Valid ProcessHandle on success (hProcess != nullptr).
/// - Zero-initialized ProcessHandle on failure (check logs).
///
/// # Errors
/// Logs Platform::Error with GetLastError() on pipe or process creation failure.
ProcessHandle ProcessCreate(const char* exe, const char* args = nullptr, const char* workDir = nullptr);

/// Checks whether the child process is still running.
///
/// # Returns
/// - `true` if the process has not yet exited.
/// - `false` if exited or handle is invalid.
bool ProcessIsRunning(const ProcessHandle& proc);

/// Waits for the child process to exit and returns its exit code.
///
/// # Arguments
/// * `proc` — Handle from ProcessCreate().
/// * `timeoutMs` — Maximum wait time in milliseconds. 0 = wait forever (INFINITE).
///
/// # Returns
/// - Process exit code on normal termination.
/// - -1 if the wait timed out or an error occurred.
int ProcessWait(const ProcessHandle& proc, uint32_t timeoutMs = 0);

/// Forcibly terminates the child process.
///
/// Calls TerminateProcess with exit code 1. ProcessClose() must still
/// be called afterward to release handles.
///
/// # Arguments
/// * `proc` — Handle to terminate. hProcess is NOT nulled (call ProcessClose).
void ProcessTerminate(ProcessHandle& proc);

/// Closes all handles in the ProcessHandle and zeros them.
///
/// Must be called when the process is no longer needed, whether it exited
/// normally, was terminated, or failed to start. Safe to call multiple times.
void ProcessClose(ProcessHandle& proc);
```

### Output Capture

```cpp
/// Reads available stdout data from the child process (non-blocking).
///
/// Uses PeekNamedPipe to check availability, then ReadFile.
/// Returns immediately with 0 if no data is available.
///
/// # Arguments
/// * `proc` — Handle from ProcessCreate().
/// * `buffer` — Destination buffer for output bytes.
/// * `bufferSize` — Maximum bytes to read.
///
/// # Returns
/// Number of bytes actually read (0 if nothing available or pipe closed).
size_t ProcessReadStdout(const ProcessHandle& proc, char* buffer, size_t bufferSize);

/// Reads available stderr data from the child process (non-blocking).
///
/// Same semantics as ProcessReadStdout but for the stderr pipe.
///
/// # Returns
/// Number of bytes actually read (0 if nothing available or pipe closed).
size_t ProcessReadStderr(const ProcessHandle& proc, char* buffer, size_t bufferSize);
```

### Usage Example

```cpp
// Compile shader with external tool
const char* exe = "dxc.exe";
const char* args = "-Zpr shader.hlsl -Fo shader.dxil";
const char* workDir = uz::GetExeDir();

uz::ProcessHandle proc = uz::ProcessCreate(exe, args, workDir);
if (proc.hProcess == nullptr) {
    UZ_LOG(Asset, Error, "Failed to spawn shader compiler");
    return false;
}

// Wait for completion with 10-second timeout
int exit_code = uz::ProcessWait(proc, 10000);  // timeout in ms
if (exit_code != 0) {
    UZ_LOG(Asset, Error, "Shader compilation failed with exit code {}", exit_code);
}

// Read any error output (non-blocking)
char stderr_buffer[4096];
size_t bytes_read = uz::ProcessReadStderr(proc, stderr_buffer, sizeof(stderr_buffer));
if (bytes_read > 0) {
    UZ_LOG(Asset, Warning, "Compiler stderr: {}", std::string(stderr_buffer, bytes_read));
}

uz::ProcessClose(proc);  // Release all handles
```

### Thread Safety

Each ProcessHandle is independent. Do not share a single handle across threads without external synchronization.

## Environment Variables

Get and set OS environment variables with UTF-8 conversion.

```cpp
/// Gets the value of an environment variable.
///
/// # Arguments
/// * `name` — UTF-8 variable name. Must not be nullptr.
///
/// # Returns
/// - Variable value as a UTF-8 string on success.
/// - Empty string if the variable is not set or on conversion failure.
[[nodiscard]] std::string GetEnv(const char* name);

/// Sets an environment variable for the current process.
///
/// The value is inherited by all child processes spawned afterward.
///
/// # Arguments
/// * `name` — UTF-8 variable name. Must not be nullptr.
/// * `value` — UTF-8 value to set. Must not be nullptr.
///
/// # Returns
/// - `true` on success.
/// - `false` on failure (check logs).
[[nodiscard]] bool SetEnv(const char* name, const char* value);
```

### Usage Example

```cpp
// Check for Vulkan SDK, set if missing
std::string vulkan_sdk = uz::GetEnv("VULKAN_SDK");
if (vulkan_sdk.empty()) {
    if (!uz::SetEnv("VULKAN_SDK", "C:\\VulkanSDK\\1.3.280")) {
        UZ_LOG(Platform, Error, "Failed to set VULKAN_SDK");
        return false;
    }
}

// Child processes inherit the updated environment
const char* exe = "glslc.exe";
const char* args = "shader.glsl -o shader.spv";
uz::ProcessHandle proc = uz::ProcessCreate(exe, args, nullptr);
int exit_code = uz::ProcessWait(proc);
uz::ProcessClose(proc);
```

### Thread Safety

Win32 environment functions are not thread-safe. Call from a single thread or provide external synchronization (mutex).

## Shared Memory for IPC

Named shared memory regions backed by the Windows page file, enabling inter-process communication. Both creator and consumer map the same region by name.

### API

```cpp
/// Handle to a named shared memory region.
///
/// `data` points to the mapped view. `size` is the requested region size.
/// All fields are zero/null when invalid or closed.
struct SharedMemoryHandle {
    void* mapping = nullptr;  ///< Win32 file mapping handle (HANDLE).
    void* data = nullptr;     ///< Pointer to the mapped memory view.
    size_t size = 0;          ///< Size of the mapped region in bytes.
};

/// Creates a new named shared memory region backed by the page file.
///
/// If a region with this name already exists, this opens it instead
/// (Win32 CreateFileMapping semantics).
///
/// # Arguments
/// * `name` — UTF-8 name for the region (globally visible). Must not be nullptr.
/// * `size` — Size in bytes. Must be > 0.
///
/// # Returns
/// - Valid handle with non-null `data` on success.
/// - Zero-initialized handle on failure (check logs).
SharedMemoryHandle SharedMemoryCreate(const char* name, size_t size);

/// Opens an existing named shared memory region.
///
/// Fails if no region with this name exists.
///
/// # Arguments
/// * `name` — UTF-8 name of the region to open. Must not be nullptr.
/// * `size` — Expected size for MapViewOfFile. Must be > 0.
///
/// # Returns
/// - Valid handle with non-null `data` on success.
/// - Zero-initialized handle on failure (check logs).
SharedMemoryHandle SharedMemoryOpen(const char* name, size_t size);

/// Closes a shared memory mapping and releases all resources.
///
/// Unmaps the view and closes the file mapping handle.
/// Safe to call on an already-closed or zero-initialized handle.
///
/// # Behavior
/// After this call, all fields in `handle` are zeroed.
/// The underlying region persists until ALL handles (in all processes) are closed.
void SharedMemoryClose(SharedMemoryHandle& handle);
```

### Usage Example: IPC with Child Process

```cpp
// Parent process: Create shared memory region
uz::SharedMemoryHandle mem = uz::SharedMemoryCreate("uz-render-commands", 16 * 1024 * 1024);
if (mem.data == nullptr) {
    UZ_LOG(RHI, Error, "Failed to create shared memory");
    return false;
}

// Write initial state to shared memory
struct RenderCommand {
    uint32_t type;
    uint32_t param;
};

auto* cmds = reinterpret_cast<RenderCommand*>(mem.data);
cmds[0] = {0, 0};  // Initialize first command

// Spawn child process with region name
uz::ProcessHandle child = uz::ProcessCreate("uz-render-backend.exe", "--shared-mem uz-render-commands");
if (child.hProcess == nullptr) {
    uz::SharedMemoryClose(mem);
    return false;
}

// Parent continues, child opens the same region...
// (In child process:)
// uz::SharedMemoryHandle mem = uz::SharedMemoryOpen("uz-render-commands", 16 * 1024 * 1024);

// Parent waits for child
int exit_code = uz::ProcessWait(child);
uz::ProcessClose(child);

// Cleanup
uz::SharedMemoryClose(mem);
```

### Thread Safety

Create/Open/Close are thread-safe (Win32 kernel objects). The mapped `data` pointer requires external synchronization (mutex, atomic flags) for concurrent access from multiple threads or processes.

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| DLL loading | `FPlatformProcess::GetDllHandle()` | `OS::open_dynamic_library()` | `IPlatformProcess::LoadDll()` |
| Symbol lookup | `FPlatformProcess::GetDllExport()` | `OS::get_dynamic_library_symbol()` | `GetDllSymbol()` |
| Process creation | `FPlatformProcess::CreateProc()` | `OS::execute()` | `CreateProcess()` |
| Pipe handling | `FProcHandle` + redirects | `Process::create()` with `USE_PIPE_MUTEX` | `Pipe`, `capture_stdout` |
| Output capture | `FGenericPlatformProcess::ReadPipe()` | N/A | `CaptureProcessOutput()` |
| Shared memory | `FMemory::Malloc()` + shared handle | N/A | `CreateSharedMemory()` |

## Rules

1. **ALWAYS call `FreeDll()` when done with a DLL** — failure to unload causes resource leak and prevents re-loading
2. **ALWAYS call `ProcessClose()` when done with a process** — even if it exited, handles must be released
3. **ALWAYS call `SharedMemoryClose()` when done with a region** — the underlying region persists until all handles are closed
4. **ALWAYS check handle validity** — test `hProcess != nullptr` before using ProcessHandle
5. **NEVER wait indefinitely on process exit** — use timeout to prevent deadlock
6. **NEVER assume symbol names are case-insensitive** — GetProcAddress is case-sensitive for C symbols
7. **ALWAYS use UTF-8 for paths and arguments** — all functions expect UTF-8, internal conversion to UTF-16 is automatic
