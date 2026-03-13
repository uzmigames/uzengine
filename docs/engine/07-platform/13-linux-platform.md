# Platform — Linux Platform

> **Status**: Implemented (Phase 4.10.0)
> **Source**: `platform/linux/` — X11 window, file I/O, crash handler, system info
> **Depends on**: X11 (Xlib), POSIX (unistd, fcntl, sys/stat), Linux-specific (inotify, /proc, /sys)
> **Supports**: X11 only (Wayland planned for Phase 4.10.1)

## Purpose

Linux is the second target platform (Steam Deck, SteamOS, desktops, CI/CD systems). The platform abstraction interfaces enable multi-platform support without changing engine code. UzEngine uses X11 for window management (stable, widely available) with Wayland support planned as a follow-up.

The Linux backend provides:
- **X11 window system**: Native Xlib window creation, event handling, fullscreen via EWMH
- **POSIX file I/O**: UTF-8 paths, directory enumeration, memory-mapped files
- **inotify directory watching**: Hot-reload support with background thread
- **Process management**: fork/execvp with piped output, process monitoring
- **System information**: CPU (CPUID + /proc/cpuinfo), memory (/proc/meminfo), GPU (/sys/class/drm), OS (uname)
- **Crash diagnostics**: Signal handlers, backtrace capture, crash.txt + crash.json reports

## Architecture

### Module Organization

```
platform/linux/
├── Window & Input
│   ├── platform_window.h/cpp     — X11 window creation and event handling
│   ├── input.h/cpp                — Keyboard/mouse state tracking
│   └── (keysym → VK_* translation)
│
├── File System
│   ├── file_system.h/cpp          — POSIX file read/write/enumerate
│   ├── file_watcher.h/cpp         — inotify-based directory watching
│   ├── memory_mapped_file.h/cpp   — mmap for zero-copy file access
│   └── shared_memory.h/cpp        — shm_open/mmap for IPC
│
├── Process & DLL
│   ├── process.h/cpp              — fork/execvp with pipe capture
│   └── dll_loader.h/cpp           — dlopen/dlsym for plugins
│
├── Diagnostics
│   ├── crash_handler.h/cpp        — Signal handlers, crash reporting
│   └── stack_walker.h/cpp         — backtrace, dladdr, __cxa_demangle
│
└── System & Environment
    ├── system_info.h/cpp          — CPU, memory, OS, GPU, battery, locale
    ├── environment.h/cpp          — getenv/setenv
    └── (paths resolved via XDG standard)
```

### Data Flow (Window → Game)

```
X11 Server
    │
    └─→ XNextEvent (KeyPress, MotionNotify, ConfigureNotify, etc.)
        │
        ├─→ PlatformWindow::PumpMessages() processes each event
        │
        ├─→ For KeyPress: XLookupKeysym → TranslateKeysym → VK_*
        │   └─→ InputSystem::OnKeyDown(vk)
        │
        ├─→ For MotionNotify:
        │   └─→ InputSystem::OnMouseMove(x, y)
        │
        ├─→ For ConfigureNotify:
        │   └─→ Update width/height, set m_resized = true
        │
        ├─→ For ClientMessage (WM_DELETE_WINDOW):
        │   └─→ Set m_open = false (graceful close)
        │
        ├─→ For FocusIn/FocusOut:
        │   └─→ Update m_focused flag
        │
        └─→ For MapNotify/UnmapNotify:
            └─→ Track m_minimized state
```

## X11 Window System

### PlatformWindow Implementation

The `PlatformWindow` class wraps X11 window creation and event dispatch:

```cpp
/// Linux/X11 window implementation.
class PlatformWindow {
    Display* m_display = nullptr;         ///< X11 server connection (XOpenDisplay)
    ::Window m_window = 0;                ///< X11 window ID (XCreateSimpleWindow)
    Atom m_wmDeleteMessage = 0;           ///< WM_DELETE_WINDOW atom for close protocol
    InputSystem* m_input = nullptr;       ///< Routed input events
    int32_t m_width, m_height = 0;
    bool m_open, m_resized = false;
    bool m_minimized = false;
    bool m_focused = true;

public:
    /// Creates and maps an X11 window.
    /// - XOpenDisplay(nullptr) connects to the default display
    /// - XCreateSimpleWindow creates an unmapped window
    /// - XSelectInput registers for KeyPress, KeyRelease, ButtonPress, ButtonRelease,
    ///   PointerMotion, StructureNotify (ConfigureNotify, MapNotify, UnmapNotify),
    ///   FocusChange, and ClientMessage events
    /// - WM_DELETE_WINDOW protocol enables graceful close on window manager request
    /// - XSizeHints set PMinSize/PMaxSize for non-resizable windows
    /// - _NET_WM_STATE_FULLSCREEN EWMH atom set for fullscreen request
    bool Create(const WindowDesc& desc);

    /// Processes all pending X11 events (non-blocking via XPending).
    /// Dispatches to InputSystem for input events.
    /// Updates m_resized, m_open, m_minimized, m_focused flags based on events.
    bool PumpMessages();

    /// X11-specific accessors for Vulkan surface creation.
    Display* GetDisplay() const { return m_display; }
    ::Window GetWindow() const { return m_window; }
};
```

### Event Handling

The message pump drains all pending X11 events and updates engine state:

| X11 Event | Engine Action | Notes |
|-----------|---------------|-------|
| `ConfigureNotify` | Update width/height, set `m_resized=true` | Window size or position changed |
| `ClientMessage` | Check `WM_DELETE_WINDOW`, set `m_open=false` | User clicked close button via WM |
| `FocusIn` | Set `m_focused=true` | Window gained focus (throttle rendering?) |
| `FocusOut` | Set `m_focused=false` | Window lost focus |
| `MapNotify` | Set `m_minimized=false`, `m_resized=true` | Restored from minimized state |
| `UnmapNotify` | Set `m_minimized=true` | Window minimized (surface will be 0x0) |
| `KeyPress` | Translate keysym → VK_*, call `InputSystem::OnKeyDown(vk)` | Physical key pressed |
| `KeyRelease` | Detect auto-repeat pairs, translate keysym, call `InputSystem::OnKeyUp(vk)` | Physical key released |
| `MotionNotify` | Call `InputSystem::OnMouseMove(x, y)` | Mouse moved |
| `ButtonPress` | Translate Button1–5 to MouseButton, call `InputSystem::OnMouseButton(btn, true)` or `OnMouseScroll(delta)` | Mouse button or scroll wheel down |
| `ButtonRelease` | Translate Button1–3, call `InputSystem::OnMouseButton(btn, false)` | Mouse button released (not scroll) |

### Keysym Translation

X11 keysyms are translated to Windows-compatible VK_* codes so the `InputSystem` and game code work identically across platforms:

```cpp
static uint16_t TranslateKeysym(KeySym keysym) {
    if (keysym >= XK_a && keysym <= XK_z)
        return static_cast<uint16_t>(keysym - XK_a + 'A');  // Uppercase
    if (keysym >= XK_0 && keysym <= XK_9)
        return static_cast<uint16_t>(keysym);               // ASCII digits
    if (keysym >= XK_F1 && keysym <= XK_F12)
        return static_cast<uint16_t>(0x70 + (keysym - XK_F1));  // VK_F1..VK_F12

    switch (keysym) {
    case XK_Escape:       return 0x1B;    // VK_ESCAPE
    case XK_space:        return 0x20;    // VK_SPACE
    case XK_Return:       return 0x0D;    // VK_RETURN
    case XK_BackSpace:    return 0x08;    // VK_BACK
    case XK_Tab:          return 0x09;    // VK_TAB
    case XK_Shift_L:      return 0xA0;    // VK_LSHIFT
    case XK_Shift_R:      return 0xA1;    // VK_RSHIFT
    case XK_Control_L:    return 0xA2;    // VK_LCONTROL
    case XK_Control_R:    return 0xA3;    // VK_RCONTROL
    case XK_Alt_L:        return 0xA4;    // VK_LMENU
    case XK_Alt_R:        return 0xA5;    // VK_RMENU
    case XK_Left:         return 0x25;    // VK_LEFT
    case XK_Up:           return 0x26;    // VK_UP
    case XK_Right:        return 0x27;    // VK_RIGHT
    case XK_Down:         return 0x28;    // VK_DOWN
    // ... more mappings ...
    default:              return 0;       // Unknown
    }
}
```

### Auto-Repeat Handling

X11 sends spurious `KeyRelease`/`KeyPress` pairs for auto-repeat. We detect and skip the release:

```cpp
case KeyRelease: {
    // Check if next event is immediate KeyPress with same keycode (same timestamp)
    if (XPending(m_display)) {
        XEvent next = {};
        XPeekEvent(m_display, &next);
        if (next.type == KeyPress
            && next.xkey.time == event.xkey.time
            && next.xkey.keycode == event.xkey.keycode) {
            // Auto-repeat pair — consume the next event, treat as key held
            XNextEvent(m_display, &next);
            break;
        }
    }
    // Real key release
    m_input->OnKeyUp(vk);
    break;
}
```

### Fullscreen via EWMH

Fullscreen is requested using the Extended Window Manager Hints (EWMH) standard:

```cpp
if (desc.fullscreen) {
    Atom wmState = XInternAtom(m_display, "_NET_WM_STATE", False);
    Atom wmFullscreen = XInternAtom(m_display, "_NET_WM_STATE_FULLSCREEN", False);

    XChangeProperty(m_display, m_window, wmState, XA_ATOM, 32,
                    PropModeReplace,
                    reinterpret_cast<unsigned char*>(&wmFullscreen), 1);
}
```

The window manager is responsible for interpreting this hint and switching the window to fullscreen mode. This is more portable than using X11 root-window hacks.

## File I/O on Linux

### POSIX File Operations

The file system module provides stateless, thread-safe file operations using POSIX open/read/write:

```cpp
// Read entire file into a byte vector
std::vector<uint8_t> FileReadAll(const char* path);
std::string FileReadAllText(const char* path);

// Write bytes to file (creates parents if needed)
FileResult FileWriteAll(const char* path, const void* data, size_t size);
FileResult FileWriteAllText(const char* path, const char* text);

// Query file properties
bool FileExists(const char* path);
int64_t FileGetSize(const char* path);  // Returns -1 on error
FileResult FileDelete(const char* path);

// Directory operations
FileResult DirectoryCreate(const char* path);  // mkdir -p equivalent
bool IsDirectory(const char* path);
std::vector<FileEntry> DirectoryEnumerate(const char* dir, const char* pattern = "*");
std::vector<FileEntry> DirectoryEnumerateRecursive(const char* dir, const char* pattern = "*");
```

All operations:
- Accept UTF-8 paths (native on Linux)
- Log errors via the `FileSystem` log category with errno context
- Use `O_CLOEXEC` to prevent FD leaks to child processes
- Handle EINTR (signal interruption) in read/write loops
- Create parent directories automatically on write operations

### Directory Enumeration

Non-recursive enumeration uses `opendir()` / `readdir()` with `fnmatch()` for glob pattern matching:

```cpp
std::vector<FileEntry> DirectoryEnumerate(const char* dir, const char* pattern) {
    DIR* dh = opendir(dir);
    if (!dh) return {};  // Silent return for non-existent directories

    std::vector<FileEntry> results;
    struct dirent* entry;
    while ((entry = readdir(dh)) != nullptr) {
        if (IsDotEntry(entry->d_name)) continue;  // Skip . and ..
        if (fnmatch(pattern, entry->d_name, FNM_NOESCAPE) != 0) continue;
        results.push_back(MakeFileEntry(dir, entry->d_name));
    }
    closedir(dh);
    return results;
}
```

Recursive enumeration traverses the directory tree depth-first, emitting all matching entries:

```cpp
std::vector<FileEntry> DirectoryEnumerateRecursive(const char* dir, const char* pattern) {
    std::vector<FileEntry> results;
    EnumerateRecursiveImpl(dir, pattern, results);
    return results;  // Includes both files and directories if they match
}
```

Each `FileEntry` contains the full path, file size, and a flag indicating whether it's a directory.

### Memory-Mapped Files

For zero-copy read access to large files:

```cpp
class MemoryMappedFile {
public:
    /// Opens and maps a file for reading.
    /// - Uses PROT_READ, MAP_PRIVATE (copy-on-write, no sharing)
    /// - File descriptor is closed after mmap (file stays accessible via mapping)
    /// - Returns false if file is empty or cannot be opened/mapped
    bool Open(const char* path);

    /// Unmaps the file (munmap) and closes resources.
    void Close();

    /// Pointer to the mapped file data (nullptr if not open)
    const void* Data() const { return m_data; }
    size_t Size() const { return m_size; }
    bool IsOpen() const { return m_data != nullptr; }

private:
    void* m_data = nullptr;  ///< Base address from mmap()
    size_t m_size = 0;       ///< Size for munmap()
};
```

**Use case**: Large asset files where avoiding a copy improves load times. For small files, `FileReadAll()` is simpler.

### Directory Watching (inotify)

Hot-reload is powered by `DirectoryWatcher`, which monitors directory changes using Linux inotify:

```cpp
class DirectoryWatcher {
public:
    /// Starts watching a directory for file changes.
    /// - Spawns a background thread running poll() over inotify fd and self-pipe
    /// - Recursive: if true, watches all subdirectories as well
    /// - callback is invoked on the watcher thread for each event
    bool Start(const char* directory, bool recursive, FileWatchCallback callback, void* userData = nullptr);

    /// Stops watching and joins the background thread.
    void Stop();

    bool IsWatching() const { return m_watching; }

private:
    int m_inotifyFd = -1;           ///< inotify instance
    int m_pipeFd[2] = {-1, -1};     ///< Self-pipe for clean shutdown
    std::thread* m_thread = nullptr;

    /// Background thread: poll() on inotify and pipe, dispatch callbacks
    void WatchThread();
};
```

### inotify Events

The watcher monitors these inotify mask flags:

| Mask | Engine Event | Use |
|------|--------------|-----|
| `IN_CLOSE_WRITE` | `Modified` | File fully written (safer than `IN_MODIFY` for editors) |
| `IN_CREATE` | `Created` | New file/directory created |
| `IN_DELETE` | `Deleted` | File/directory deleted |
| `IN_MOVED_TO` | `Renamed` | Rename/atomic-save target |
| `IN_MOVED_FROM` | `Renamed` | Rename/atomic-save source |

The background thread drains the inotify event buffer in a single `read()` call, parsing the variable-length `inotify_event` chain and invoking the callback for each event with event type and filename.

**Stop mechanism**: When `Stop()` is called, a single byte is written to the self-pipe, which wakes the `poll()` call without relying on signals or busy-wait.

## Crash Handler on Linux

The Linux crash handler uses POSIX signal handlers to catch crashes and write structured reports:

### Installation

```cpp
/// Installs signal handlers for SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL.
/// Uses sigaction() with SA_SIGINFO | SA_RESETHAND flags.
/// SA_RESETHAND ensures that a second crash invokes the default handler
/// (avoiding infinite recursion).
void InstallCrashHandler();

/// Removes the crash signal handlers and restores SIG_DFL.
void UninstallCrashHandler();
```

### CrashContext

The global crash context is updated by engine systems each frame and read by the signal handler:

```cpp
struct CrashContext {
    uint32_t exceptionCode;     ///< Signal number (SIGSEGV=11, SIGABRT=6, etc.)
    void* exceptionAddress;     ///< Instruction pointer (si_addr from siginfo_t)
    uint64_t frameNumber;       ///< Engine frame counter (set by renderer)
    char sceneName[256];        ///< Active scene name (set by scene system)
    char lastRhiOp[256];        ///< Last GPU operation (set by RHI)
    char gpuAdapter[256];       ///< GPU adapter name (set at RHI init)
    bool gpuDeviceRemoved;      ///< Device-removed flag
    void* stackFrames[64];      ///< Raw instruction pointers
    int stackFrameCount;        ///< Number of valid stack frames
};
```

### Signal Handler Execution

The signal handler is **async-signal-safe** — it uses only non-blocking system calls and writes directly to file descriptors:

1. **Capture raw stack trace**: `backtrace(ctx.stackFrames, 64)` — extremely fast, lock-free
2. **Copy global state**: `memcpy()` global buffers into stack-local `CrashContext`
3. **Get executable path**: `readlink("/proc/self/exe", ...)` — async-signal-safe
4. **Format timestamp**: `localtime_r()` + `snprintf()` (safe with SA_RESETHAND)
5. **Create crash directory**: `mkdir()` calls for `{exe_dir}/crashes/{timestamp}/`
6. **Write crash.txt**: `snprintf()` + `write()` for human-readable report
7. **Write crash.json**: `snprintf()` + `write()` for structured telemetry

### Crash Output

**`crash.txt`** — Human-readable report:

```
=== UzEngine Crash Report ===

Signal:     SIGSEGV (segmentation fault) (11)
Address:    0x0000000000000000
Frame:      12345
Scene:      gameplay_level_01
Last RHI:   vkQueueSubmit
GPU:        NVIDIA GeForce RTX 4090
Device Removed: no

Stack Trace (8 frames):
  [ 0] /usr/lib/x86_64-linux-gnu/libvulkan.so.1(vkQueueSubmit+0x...) [0x00007f...]
  [ 1] ./game_binary(_ZN3uz3RHI14SubmitDrawCallERK...) [0x0000555...]
  [ 2] ./game_binary(main+0x...) [0x0000555...]
  ...
```

**`crash.json`** — Machine-readable for telemetry:

```json
{
  "engine": "UzEngine",
  "timestamp": "2026-03-13_14-35-22",
  "exception": {
    "code": 11,
    "name": "SIGSEGV",
    "address": "0x0000000000000000"
  },
  "context": {
    "frameNumber": 12345,
    "sceneName": "gameplay_level_01",
    "lastRhiOp": "vkQueueSubmit",
    "gpuAdapter": "NVIDIA GeForce RTX 4090",
    "gpuDeviceRemoved": false
  },
  "stackTrace": [
    {"index": 0, "address": "0x00007f..."},
    {"index": 1, "address": "0x0000555..."},
    ...
  ]
}
```

### Integration Points

Engine systems update crash context via these APIs:

```cpp
/// Called once per frame from the renderer's main loop
void CrashHandler_SetFrameNumber(uint64_t frame);

/// Called when a scene is loaded
void CrashHandler_SetSceneName(const char* name);

/// Called before major GPU operations
void CrashHandler_SetLastRhiOp(const char* op);

/// Called once at RHI initialization
void CrashHandler_SetGpuAdapter(const char* adapter);

/// Called when device removal is detected
void CrashHandler_SetGpuDeviceRemoved(bool removed);
```

All functions are thread-safe (atomic stores or single-writer assumptions).

## Standard Directories (XDG)

The file system module respects the Linux XDG Base Directory specification for user data locations:

```cpp
/// Returns ~/.local/share/UzEngine/ (or $XDG_DATA_HOME/UzEngine/)
/// Creates directory if it doesn't exist.
std::string GetUserDataDir();

/// Returns ~/.config/UzEngine/ (or $XDG_CONFIG_HOME/UzEngine/)
/// Creates directory if it doesn't exist.
std::string GetAppDataDir();

/// Returns /tmp/UzEngine/ (or $TMPDIR/UzEngine/)
/// Creates directory if it doesn't exist.
std::string GetTempDir();
```

These use the standard environment variables:
- `$XDG_DATA_HOME` (fallback: `~/.local/share`) — user-local application data
- `$XDG_CONFIG_HOME` (fallback: `~/.config`) — user configuration
- `$XDG_CACHE_HOME` (fallback: `~/.cache`) — temporary cache (not implemented, but directories auto-clean)
- `$TMPDIR` (fallback: `/tmp`) — process-local temporary files

**Example paths**:
```
~/.local/share/UzEngine/      — Save games, app data
~/.config/UzEngine/            — Settings, preferences
/tmp/UzEngine/                 — Temp assets, hot-reload staging
```

## Dynamic Library Loading

The DLL loader wraps POSIX `dlopen`/`dlsym`/`dlclose` for plugin support:

```cpp
/// Loads a shared library (.so file).
/// Uses RTLD_NOW to resolve all symbols immediately (fail-fast).
/// Logs errors with dlerror() context.
NativeDllHandle LoadDll(const char* path);

/// Retrieves a symbol address from a loaded library.
/// Logs a warning if the symbol is not found.
void* GetDllSymbol(NativeDllHandle handle, const char* symbolName);

/// Unloads a shared library (reference-counted by the kernel).
void FreeDll(NativeDllHandle handle);
```

**Usage**:

```cpp
// Load a Vulkan extension or optional library
NativeDllHandle wayland_lib = LoadDll("libwayland-client.so.0");
if (wayland_lib) {
    auto wl_display_connect =
        (struct wl_display*(*)())GetDllSymbol(wayland_lib, "wl_display_connect");
    // Use wl_display_connect if available
    FreeDll(wayland_lib);
}
```

## Process Management

Spawn child processes with piped stdout/stderr capture:

```cpp
/// Launches a child process with captured output.
/// - Uses fork() to spawn and execvp() to run the executable.
/// - Creates anonymous pipes for stdout and stderr.
/// - Parent's pipe ends are set to non-blocking (fcntl O_NONBLOCK).
ProcessHandle ProcessCreate(const char* exe, const char* args = nullptr, const char* workDir = nullptr);

/// Non-blocking reads of child stdout and stderr.
size_t ProcessReadStdout(const ProcessHandle& proc, char* buffer, size_t bufferSize);
size_t ProcessReadStderr(const ProcessHandle& proc, char* buffer, size_t bufferSize);

/// Wait for child exit, optionally with timeout.
/// Returns exit code on success, -1 on timeout or signalled process.
int ProcessWait(const ProcessHandle& proc, uint32_t timeoutMs = 0);

/// Checks if child is still running (non-blocking).
bool ProcessIsRunning(const ProcessHandle& proc);

/// Forcibly terminate with SIGKILL.
void ProcessTerminate(ProcessHandle& proc);

/// Close all pipe FDs and reap the zombie.
/// MUST call when done, whether process exited normally or was killed.
void ProcessClose(ProcessHandle& proc);
```

**Use case**: Running shader compiler, asset converter, or build system from within the engine.

**Example**:

```cpp
ProcessHandle dxc = ProcessCreate("dxc", "-T vs_6_6 shader.hlsl -Fo output.o");
char buf[4096];
while (ProcessIsRunning(dxc)) {
    size_t n = ProcessReadStdout(dxc, buf, sizeof(buf));
    if (n > 0) {
        UZ_LOG(Shader, Info, "DXC: {}", std::string(buf, n));
    }
}
int exitCode = ProcessWait(dxc);
ProcessClose(dxc);
```

## Stack Walking & Symbol Resolution

For diagnostic stack traces outside the crash handler context:

```cpp
struct StackFrame {
    void* address;            ///< Instruction pointer
    char functionName[256];   ///< Demangled C++ function name (or "???")
    char fileName[256];       ///< Source file (empty — requires addr2line)
    int lineNumber;           ///< Source line number (0 — requires addr2line)
};

/// Captures and resolves the current thread's stack trace.
/// Uses backtrace() to capture, dladdr() for symbols, __cxa_demangle for C++ names.
int WalkStack(StackFrame* frames, int maxFrames, int skipFrames = 1);

/// Formats resolved frames as human-readable multi-line string.
std::string FormatStackTrace(const StackFrame* frames, int count);

/// Captures raw instruction pointers without symbol resolution (async-signal-safe).
int CaptureRawStackTrace(void* frames[], int maxFrames, int skipFrames = 1);
```

**Limitations**: Without `addr2line` or dwarf debug info, only function names and addresses are available. File/line numbers require additional processing post-crash.

## System Information Queries

The system_info module queries CPU, memory, GPU, OS, battery, and locale information for adaptive quality, telemetry, and diagnostics:

### CPU Information

```cpp
struct CpuInfo {
    char brand[128];            ///< "Intel Core i7-13700K"
    uint32_t physicalCores;     ///< Actual core count (no hyperthreads)
    uint32_t logicalCores;      ///< Logical CPUs (includes hyperthreads)
    uint32_t l1CacheKB;         ///< L1 data cache size
    uint32_t l2CacheKB;         ///< L2 cache per core
    uint32_t l3CacheKB;         ///< L3 shared cache
    bool hasSSE42, hasAVX, hasAVX2, hasAVX512;  ///< SIMD features
    bool hasFMA, hasAESNI;
};

CpuInfo GetCpuInfo();
```

Reads from:
- `/proc/cpuinfo` — brand name, physical topology via "core id" uniqueness
- `sysconf(_SC_NPROCESSORS_ONLN)` — logical CPU count
- `/sys/devices/system/cpu/cpu0/cache/` — cache sizes per level
- `__cpuid_count()` — SIMD feature flags (SSE4.2, AVX, AVX2, AVX-512, FMA, AES-NI)

### Memory Information

```cpp
struct MemoryInfo {
    uint64_t totalPhysicalMB;       ///< Total installed RAM
    uint64_t availablePhysicalMB;   ///< Currently available RAM
    uint64_t totalVirtualMB;        ///< Total virtual address space
    uint32_t usagePercent;          ///< Memory load 0–100
};

MemoryInfo GetMemoryInfo();
```

Parses `/proc/meminfo` for physical RAM and swap usage. Updates dynamically as system state changes.

### OS Information

```cpp
struct OsInfo {
    char name[128];                ///< "Linux 6.8.0-51-generic"
    uint32_t majorVersion;         ///< Kernel major version
    uint32_t minorVersion;         ///< Kernel minor version
    uint32_t buildNumber;          ///< Patch level
    char architecture[16];         ///< "x86_64", "aarch64", etc.
};

OsInfo GetOsInfo();
```

Calls `uname()` to get kernel name, version (parsed from `utsname.release`), and architecture. Platform-agnostic approach works on any UNIX-like OS.

### GPU Information

```cpp
struct GpuInfo {
    char name[256];                 ///< Adapter name (empty on Linux)
    char driverVersion[64];         ///< Driver version (empty)
    uint32_t vendorId;              ///< PCI vendor ID (0x10de = NVIDIA)
    uint32_t deviceId;              ///< PCI device ID
    uint64_t dedicatedVRAM_MB;      ///< Dedicated VRAM (0 on Linux)
    uint64_t sharedVRAM_MB;         ///< Shared memory (0 on Linux)
};

GpuInfo GetGpuInfo();
```

Best-effort via `/sys/class/drm/card0/device/`:
- Reads PCI vendor ID from `/sys/class/drm/card0/device/vendor`
- Reads PCI device ID from `/sys/class/drm/card0/device/device`
- Name, VRAM, and driver version require Vulkan enumeration (not done here to avoid RHI dependencies)

**Note**: Returns zero-initialized struct on headless systems.

### Battery Information

```cpp
struct BatteryInfo {
    bool hasBattery;        ///< True if /sys/class/power_supply/BAT0/ exists
    bool isCharging;        ///< True if AC adapter is online
    uint8_t percentage;     ///< Charge 0–100, or 255 if unavailable
};

BatteryInfo GetBatteryInfo();
```

Reads from `/sys/class/power_supply/BAT0/` (laptops) and `/sys/class/power_supply/AC/online` (AC status). Returns all-zero for desktops.

### Locale Information

```cpp
struct LocaleInfo {
    char language[16];      ///< "en", "pt", etc.
    char region[16];        ///< "US", "BR", etc.
};

LocaleInfo GetLocaleInfo_();
```

Queries the user's default locale via `setlocale(LC_ALL, "")` and falls back to the `LANG` environment variable. Used for language/region-specific game content and UI.

### Logging System Info

```cpp
/// Calls all query functions and logs results at engine startup.
void LogSystemInfo();
```

Example output:

```
[Platform] CPU: Intel Core i7-13700K (16 cores / 24 threads)
[Platform]  L1: 448 KB, L2: 3136 KB, L3: 24576 KB
[Platform]  Features: SSE4.2, AVX, AVX2, FMA, AES-NI
[Platform] Memory: 31.2 GB / 32.0 GB (97% available)
[Platform] OS: Linux 6.8.0-51-generic (x86_64)
[Platform] GPU: vendor=10de, device=2805 (raw sysfs IDs)
[Platform] Locale: en-US
```

## Environment Variables

Simple getenv/setenv wrapper for POSIX environments:

```cpp
std::string GetEnv(const char* name);
bool SetEnv(const char* name, const char* value);
```

**Note**: POSIX getenv/setenv are NOT thread-safe. Call from a single thread or provide external synchronization.

## Shared Memory (IPC)

For inter-process communication (e.g., runtime ↔ extension):

```cpp
struct SharedMemoryHandle {
    void* mapping;      ///< shm file descriptor (cast to void*)
    void* data;         ///< Pointer to mmap'd region
    size_t size;        ///< Region size in bytes
};

/// Creates or opens a named shared memory region.
SharedMemoryHandle SharedMemoryCreate(const char* name, size_t size);

/// Opens an existing named shared memory region.
SharedMemoryHandle SharedMemoryOpen(const char* name, size_t size);

/// Closes and unmaps a shared memory region.
/// Note: region persists until shm_unlink() is called by the creator.
void SharedMemoryClose(SharedMemoryHandle& handle);
```

**Use case**: Frame streaming from runtime to extension. Accessed as `/dev/shm/...` by both processes.

**Example**:

```cpp
// Runtime process
auto shm = SharedMemoryCreate("/uz_frame_buffer", 1024 * 1024);
memcpy(shm.data, frame_pixels, frame_size);

// Extension process
auto shm = SharedMemoryOpen("/uz_frame_buffer", 1024 * 1024);
RenderFrame((uint8_t*)shm.data);
```

## Design Decisions

### X11 vs Wayland

UzEngine starts with X11 for immediate broad compatibility:
- X11 is available on all mainstream Linux distros
- Stable, well-documented API (since 1987)
- Window managers (KDE, GNOME, i3, sway) all support X11 protocol
- Wayland adoption is increasing but not yet universal

Wayland support is planned for Phase 4.10.1 as an alternative display server backend. The design will allow runtime selection based on `$WAYLAND_DISPLAY` or explicit user preference.

### UTF-8 Paths

Linux uses UTF-8 natively. No conversion is needed unlike Windows (UTF-16). All file APIs accept `const char* path` directly.

### RTLD_NOW vs RTLD_LAZY

DLL loading uses `RTLD_NOW` (resolve all symbols immediately) instead of `RTLD_LAZY` to catch missing dependencies early rather than at first call.

### Async-Signal-Safe Crash Handler

The signal handler avoids heap allocation, locks, and UZ_LOG() to prevent deadlock on corrupted heap or infinite recursion. Only `backtrace()`, `memcpy()`, `readlink()`, `mkdir()`, `snprintf()`, and `write()` are called in the handler itself.

### inotify vs Polling

Directory watching uses inotify instead of polling:
- More efficient (0% CPU when idle)
- Event-driven (instant callback on file change)
- Supports recursive directory watching with a single inotify instance
- Linux 2.6.13+ (ubiquitous)

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| **Display Server** | SDL2 wrapper | X11 native + Wayland | X11 native; Wayland planned |
| **Window** | FLinuxWindow | X11Window, WaylandWindow | PlatformWindow (X11) |
| **Input** | FSlateApplication | X11 events, XInput2 | InputSystem (keysym translation) |
| **File I/O** | POSIX open/read/write | FileAccess + threading | POSIX (open/read/write/fcntl) |
| **Directory Watch** | inotify wrapper | DirectoryWatcher | DirectoryWatcher (inotify + poll) |
| **Memory-Mapped Files** | mmap PROT_READ | FileAccess::read | MemoryMappedFile (mmap) |
| **Process Creation** | fork/execv | OS::execute | ProcessCreate (fork/execvp/pipes) |
| **DLL Loading** | dlopen/dlsym | load_dynamic | LoadDll/GetDllSymbol (dlopen/dlsym) |
| **Crash Handler** | Signal handler + core | CrashHandlerException | Signal handler + backtrace |
| **Stack Walking** | dl_iterate_phdr + addr2line | backtrace | backtrace + dladdr + __cxa_demangle |
| **System Paths** | FPaths | XDG standard | XDG (GetUserDataDir, etc.) |
| **CPU Info** | /proc/cpuinfo + CPUID | DetectCPUFeatures | CPUID + /proc/cpuinfo + /sys |
| **Memory Info** | /proc/meminfo | OS.get_static_memory_usage | /proc/meminfo |
| **Locale** | g_LinuxPlatformMisc | OS.get_locale | setlocale + LANG env var |

## Best Practices & Rules

### Display Server

1. **ALWAYS use X11 EWMH for window state** — enables proper fullscreen, minimize, title bar control
   - `_NET_WM_STATE` atom for fullscreen, maximized, minimized
   - `_NET_WM_NAME` for window title (more robust than XStoreName)
   - `WM_DELETE_WINDOW` protocol for graceful close
2. **NEVER assume X11 display always exists** — check `XOpenDisplay()` return and handle failure gracefully
3. **ALWAYS handle auto-repeat key pairs** — X11 sends spurious KeyRelease/KeyPress pairs; detect and skip
4. **NEVER block on X11 calls** — use XPending() + XNextEvent() in non-blocking event loop

### File System

5. **ALWAYS use `O_CLOEXEC` on open/pipe/socket** — prevents accidental FD leaks to child processes
6. **NEVER assume parent directories exist** — auto-create with `mkdir -p` equivalent before write
7. **ALWAYS respect XDG directories** — users expect `~/.local/share/UzEngine/`, not `/opt/` or custom paths
8. **NEVER hard-code library names** — use `dlopen()` with soname versioning or versioned .so files
9. **ALWAYS handle EINTR in read/write loops** — signals can interrupt system calls
10. **NEVER use `fnmatch()` on full paths** — match only filenames in directory enumeration

### Directory Watching

11. **ALWAYS use `inotify_init1(IN_NONBLOCK | IN_CLOEXEC)`** — prevents FD leaks and ensures non-blocking poll()
12. **NEVER add watch on the same fd twice** — inotify is lazy; use IN_MOVED_TO for atomic-save patterns
13. **ALWAYS drain the entire inotify buffer** — single `read()` to get all pending events

### Crash Handling

14. **ALWAYS use `SA_RESETHAND`** — ensures second crash invokes default handler (core dump) not infinite loop
15. **NEVER call malloc/UZ_LOG in signal handler** — async-signal-safe functions only
16. **ALWAYS capture raw backtrace first** — `backtrace()` is safe; symbol resolution happens post-crash
17. **NEVER use DbgHelp-style symbol resolution in signal context** — do it offline post-crash

### Process Management

18. **ALWAYS use `waitpid()` with WNOHANG** — check exit status without blocking main loop
19. **NEVER leak pipe FDs** — call ProcessClose() in all code paths (success, timeout, error)
20. **ALWAYS set stdin/stdout/stderr pipe in child** — dup2() after fork(), before execvp()
21. **NEVER rely on SIGCHLD** — some applications disable it; use explicit waitpid() polling

### Performance

22. **ALWAYS use mmap for large read-only files** — zero-copy access, faster than read()
23. **NEVER enumerate same directory repeatedly** — cache results or use inotify-based auto-reload
24. **ALWAYS use backtrace() not addr2line in signal handler** — addr2line spawns a process (deadlock risk)
