# Platform — macOS Platform

> **Status**: Implemented (Phase 4.10.0)
> **Source**: `platform/macos/` — Cocoa window, Metal rendering, FSEvents watcher, system info
> **Depends on**: Cocoa (AppKit), Metal, CoreServices, POSIX (unistd, fcntl, sys/stat), sysctl
> **Supports**: macOS 10.15+ (Intel and Apple Silicon)

## Purpose

macOS is the third target platform (iPad Pro, Mac desktop, development machines). The platform abstraction interfaces enable multi-platform support without changing engine code. UzEngine uses Cocoa for window management and CAMetalLayer for Metal rendering, enabling GPU acceleration on both Intel and Apple Silicon hardware.

The macOS backend provides:
- **Cocoa window system**: Native NSWindow + CAMetalLayer for Metal rendering
- **Input handling**: NSEvent routing through custom UZMetalView
- **POSIX file I/O**: UTF-8 paths, directory enumeration, memory-mapped files
- **FSEvents directory watching**: Real-time file change notifications with CoreServices
- **Process management**: POSIX fork/execvp with piped output, process monitoring
- **System information**: CPU (sysctl), memory (Mach VM), GPU (Metal API), OS (version)
- **Crash diagnostics**: Signal handlers, backtrace capture, crash.txt + crash.json reports
- **Platform directories**: ~/Library/Application Support, ~/Library/Preferences, $TMPDIR standards

## Architecture

### Module Organization

```
platform/macos/
├── Window & Input
│   ├── platform_window.h/.mm    — Cocoa NSWindow creation and event handling
│   ├── input.h/.cpp             — Keyboard/mouse state tracking
│   └── (NSEvent → VK_* translation)
│
├── File System
│   ├── file_system.h/.cpp       — POSIX file read/write/enumerate
│   ├── file_watcher.h/.cpp      — FSEvents-based directory watching
│   ├── memory_mapped_file.h/.cpp — mmap for zero-copy file access
│   └── shared_memory.h/.cpp     — POSIX shm_open/mmap for IPC
│
├── Process & DLL
│   ├── process.h/.cpp           — fork/execvp with pipe capture
│   └── dll_loader.h/.cpp        — dlopen/dlsym for plugins (.dylib)
│
├── Diagnostics
│   ├── crash_handler.h/.cpp     — Signal handlers, crash reporting
│   └── stack_walker.h/.cpp      — backtrace, dladdr, __cxa_demangle
│
└── System & Environment
    ├── system_info.h/.mm        — CPU, memory, OS, GPU, battery, locale
    ├── environment.h/.cpp       — getenv/setenv
    └── (paths resolved via ~/Library/ standard)
```

### Data Flow (Window → Game)

```
Cocoa Event Loop (NSApplication)
    │
    └─→ NSEvent (KeyDown, MouseMoved, WindowDidResize, etc.)
        │
        ├─→ UZMetalView receives event
        │
        ├─→ PlatformWindow::PumpMessages() processes each event
        │
        ├─→ For KeyDown: event.keyCode → TranslateKeysym → VK_*
        │   └─→ InputSystem::OnKeyDown(vk)
        │
        ├─→ For MouseMoved:
        │   └─→ InputSystem::OnMouseMove(x, y)
        │
        ├─→ For WindowDidResize (via NSWindowDelegate):
        │   └─→ Update width/height, set m_resized = true
        │
        ├─→ For WindowShouldClose:
        │   └─→ Set m_open = false (graceful close)
        │
        ├─→ For ApplicationDidBecomeActive/Inactive:
        │   └─→ Update m_focused flag
        │
        └─→ For ScrollWheel:
            └─→ InputSystem::OnMouseScroll(delta)
```

## Cocoa Window System

### PlatformWindow Implementation

The `PlatformWindow` class wraps Cocoa window creation and event dispatch:

```cpp
/// macOS/Cocoa window implementation.
class PlatformWindow {
    NSWindow* m_window = nullptr;             ///< Cocoa window (NSWindow)
    UZMetalView* m_metalView = nullptr;       ///< Custom NSView with CAMetalLayer
    CAMetalLayer* m_metalLayer = nullptr;     ///< Metal rendering surface
    InputSystem* m_input = nullptr;           ///< Routed input events
    int32_t m_width, m_height = 0;
    bool m_open = true, m_resized = false;
    bool m_minimized = false;
    bool m_focused = true;

public:
    /// Creates and displays an NSWindow with CAMetalLayer.
    /// - NSWindow allocated via [[NSWindow alloc] initWithContentRect:...]
    /// - UZMetalView created as content view (custom NSView subclass)
    /// - CAMetalLayer configured for drawable rendering
    /// - NSWindowDelegate set for resize/close callbacks
    /// - Window is key and visible after creation
    bool Create(const WindowDesc& desc);

    /// Processes all pending Cocoa events (non-blocking via NSApplication.nextEventMatchingMask).
    /// Dispatches to InputSystem for input events.
    /// Updates m_resized, m_open, m_minimized, m_focused flags based on events.
    bool PumpMessages();

    /// macOS-specific accessors for Metal surface creation.
    CAMetalLayer* GetMetalLayer() const { return m_metalLayer; }
    NSWindow* GetWindow() const { return m_window; }
};
```

### Custom UZMetalView

UZMetalView is a custom NSView subclass that manages Metal rendering and input forwarding:

```cpp
/// Custom NSView with CAMetalLayer and event routing.
class UZMetalView : public NSView {
    CAMetalLayer* m_metalLayer;
    PlatformWindow* m_window;

public:
    /// Initializes view with frame and parent window reference.
    /// Creates CAMetalLayer for Metal rendering.
    bool Init(NSRect frame, PlatformWindow* window);

    /// Metal layer is automatically sized to match view bounds.
    /// Called on resize or frame change.
    void UpdateMetalLayerSize();

    /// NSView overrides for event handling.
    /// Returns true if event was handled (prevents further dispatch).
    bool OnKeyDown(NSEvent* event);
    bool OnMouseMoved(NSEvent* event);
    bool OnScrollWheel(NSEvent* event);
};
```

### Event Handling

The message pump drains all pending Cocoa events and updates engine state:

| NSEvent Type | Engine Action | Notes |
|-------------|---------------|-------|
| `NSKeyDown` | Translate keyCode → VK_*, call `InputSystem::OnKeyDown(vk)` | Physical key pressed |
| `NSKeyUp` | Translate keyCode → VK_*, call `InputSystem::OnKeyUp(vk)` | Physical key released |
| `NSMouseMoved` | Call `InputSystem::OnMouseMove(x, y)` | Mouse moved |
| `NSLeftMouseDown` | Call `InputSystem::OnMouseButton(MouseButton::Left, true)` | Left mouse button down |
| `NSLeftMouseUp` | Call `InputSystem::OnMouseButton(MouseButton::Left, false)` | Left mouse button up |
| `NSRightMouseDown` | Call `InputSystem::OnMouseButton(MouseButton::Right, true)` | Right mouse button down |
| `NSRightMouseUp` | Call `InputSystem::OnMouseButton(MouseButton::Right, false)` | Right mouse button up |
| `NSOtherMouseDown` | Call `InputSystem::OnMouseButton(MouseButton::Middle, true)` | Middle mouse button down |
| `NSOtherMouseUp` | Call `InputSystem::OnMouseButton(MouseButton::Middle, false)` | Middle mouse button up |
| `NSScrollWheel` | Call `InputSystem::OnMouseScroll(delta)` | Mouse wheel or trackpad scroll |
| `NSWindowDidResize` | Update width/height, set `m_resized=true` | Window resized via drag |
| `NSWindowWillClose` | Set `m_open=false` | User clicked close button |
| `NSApplicationDidBecomeActive` | Set `m_focused=true` | Application became foreground |
| `NSApplicationDidResignActive` | Set `m_focused=false` | Application moved to background |

### Keycode Translation

macOS keycodes (virtual keycodes from NSEvent) are translated to Windows-compatible VK_* codes:

```cpp
static uint16_t TranslateKeysym(uint16_t keyCode) {
    // macOS virtual keycodes defined in <Carbon/Carbon.h>
    switch (keyCode) {
    case kVK_ANSI_A: return 'A';              // VK_A
    case kVK_ANSI_B: return 'B';              // VK_B
    // ... letters A-Z ...
    case kVK_ANSI_0: return '0';              // VK_0
    case kVK_ANSI_1: return '1';              // VK_1
    // ... digits 0-9 ...
    case kVK_Escape: return 0x1B;             // VK_ESCAPE
    case kVK_Space: return 0x20;              // VK_SPACE
    case kVK_Return: return 0x0D;             // VK_RETURN
    case kVK_Delete: return 0x08;             // VK_BACK (Backspace)
    case kVK_Tab: return 0x09;                // VK_TAB
    case kVK_Shift: return 0xA0;              // VK_LSHIFT
    case kVK_RightShift: return 0xA1;         // VK_RSHIFT
    case kVK_Control: return 0xA2;            // VK_LCONTROL
    case kVK_RightControl: return 0xA3;       // VK_RCONTROL
    case kVK_Option: return 0xA4;             // VK_LMENU (Alt)
    case kVK_RightOption: return 0xA5;        // VK_RMENU
    case kVK_Command: return 0x5B;            // VK_LWIN (Cmd key)
    case kVK_LeftArrow: return 0x25;          // VK_LEFT
    case kVK_UpArrow: return 0x26;            // VK_UP
    case kVK_RightArrow: return 0x27;         // VK_RIGHT
    case kVK_DownArrow: return 0x28;          // VK_DOWN
    case kVK_F1 ... kVK_F12:
        return 0x70 + (keyCode - kVK_F1);     // VK_F1 through VK_F12
    default: return 0;                        // Unknown
    }
}
```

### Fullscreen via Cocoa API

Fullscreen is implemented using NSWindow's collection behavior:

```cpp
if (desc.fullscreen) {
    [m_window setCollectionBehavior:
        NSWindowCollectionBehaviorFullScreenPrimary];
    [m_window toggleFullScreen:nil];
}
```

The window system respects macOS fullscreen conventions (separate desktop space on modern macOS).

## File I/O on macOS

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
- Accept UTF-8 paths (standard on macOS)
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

Recursive enumeration traverses the directory tree depth-first, emitting all matching entries.

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

**Use case**: Large asset files where avoiding a copy improves load times.

### Directory Watching (FSEvents)

Hot-reload is powered by `DirectoryWatcher`, which monitors directory changes using macOS FSEvents:

```cpp
class DirectoryWatcher {
public:
    /// Starts watching a directory for file changes.
    /// - Uses FSEvents with kFSEventStreamCreateFlagFileEvents
    /// - Spawns a background thread running the runloop
    /// - Recursive: if true, watches all subdirectories as well
    /// - callback is invoked on a dispatch queue for each event
    bool Start(const char* directory, bool recursive, FileWatchCallback callback, void* userData = nullptr);

    /// Stops watching and joins the background thread.
    void Stop();

    bool IsWatching() const { return m_watching; }

private:
    FSEventStreamRef m_stream = nullptr;       ///< FSEvents stream
    CFRunLoopRef m_runLoop = nullptr;          ///< Runloop for stream
    std::thread* m_thread = nullptr;

    /// Background thread: runs CFRunLoop to process FSEvents
    void WatchThread();
};
```

### FSEvents Configuration

The watcher uses FSEvents with these settings:

| Setting | Value | Purpose |
|---------|-------|---------|
| `kFSEventStreamCreateFlagFileEvents` | Enabled | Report file-level events (not just directory) |
| `kFSEventStreamCreateFlagNoDefer` | Enabled | Low-latency events (100ms), not batched |
| Recursive | Yes or No | Monitor subdirectories if recursive=true |
| Latency | 0.1s | Wait for file operations to complete |

The stream callback dispatches events via Grand Central Dispatch to a background thread, which invokes the user callback for each file change:

```cpp
static void FSEventsCallback(ConstFSEventStreamRef streamRef,
                             void* clientCallbackInfo,
                             const FSEventStreamEventFlags inFlags[],
                             const FSEventStreamEventId eventIds[],
                             size_t numEvents) {
    auto* watcher = static_cast<DirectoryWatcher*>(clientCallbackInfo);
    for (size_t i = 0; i < numEvents; ++i) {
        // Parse eventIds[i] and inFlags[i] to determine file change type
        // Invoke callback on dispatch queue
    }
}
```

**Advantages over polling**: Instant callback, zero CPU when idle, real-time file change detection.

## Crash Handler on macOS

The macOS crash handler uses POSIX signal handlers and _NSGetExecutablePath for diagnostics:

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
3. **Get executable path**: `_NSGetExecutablePath(buf, &size)` — async-signal-safe alternative to /proc
4. **Format timestamp**: `localtime_r()` + `snprintf()` (safe with SA_RESETHAND)
5. **Create crash directory**: `mkdir()` calls for `{app_dir}/crashes/{timestamp}/`
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
Last RHI:   MTLRenderCommandEncoderEndEncoding
GPU:        Apple M1
Device Removed: no

Stack Trace (8 frames):
  [ 0] /usr/lib/libsystem_c.dylib(strlen+0x...) [0x00007fff...]
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
    "lastRhiOp": "MTLRenderCommandEncoderEndEncoding",
    "gpuAdapter": "Apple M1",
    "gpuDeviceRemoved": false
  },
  "stackTrace": [
    {"index": 0, "address": "0x00007fff..."},
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

## Standard Directories (macOS)

The file system module respects macOS standard directories for user data locations:

```cpp
/// Returns ~/Library/Application Support/UzEngine/
/// Creates directory if it doesn't exist.
std::string GetUserDataDir();

/// Returns ~/Library/Preferences/UzEngine/
/// Creates directory if it doesn't exist.
std::string GetAppDataDir();

/// Returns $TMPDIR/UzEngine/ (typically /var/folders/xxx/T/)
/// Creates directory if it doesn't exist.
std::string GetTempDir();
```

These use standard macOS conventions:
- `~/Library/Application Support/` — user-local application data (save games, projects)
- `~/Library/Preferences/` — user configuration and settings
- `$TMPDIR` — process-local temporary files (unique per process)

**Example paths**:
```
~/Library/Application Support/UzEngine/   — Save games, app data
~/Library/Preferences/UzEngine/            — Settings, preferences
/var/folders/random/T/UzEngine/            — Temp assets, hot-reload staging
```

## Dynamic Library Loading

The DLL loader wraps POSIX `dlopen`/`dlsym`/`dlclose` for plugin support with .dylib files:

```cpp
/// Loads a shared library (.dylib file).
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
// Load an optional Metal extension or framework
NativeDllHandle metal_lib = LoadDll("libmetalprofiler.dylib");
if (metal_lib) {
    auto pfn_init = (void(*)())GetDllSymbol(metal_lib, "MetalProfilerInit");
    if (pfn_init) pfn_init();
    FreeDll(metal_lib);
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

## Stack Walking & Symbol Resolution

For diagnostic stack traces outside the crash handler context:

```cpp
struct StackFrame {
    void* address;            ///< Instruction pointer
    char functionName[256];   ///< Demangled C++ function name (or "???")
    char fileName[256];       ///< Source file (empty — requires dsymutil)
    int lineNumber;           ///< Source line number (0 — requires dsymutil)
};

/// Captures and resolves the current thread's stack trace.
/// Uses backtrace() to capture, dladdr() for symbols, __cxa_demangle for C++ names.
int WalkStack(StackFrame* frames, int maxFrames, int skipFrames = 1);

/// Formats resolved frames as human-readable multi-line string.
std::string FormatStackTrace(const StackFrame* frames, int count);

/// Captures raw instruction pointers without symbol resolution (async-signal-safe).
int CaptureRawStackTrace(void* frames[], int maxFrames, int skipFrames = 1);
```

**Limitations**: Without dSYM files or dwarf debug info, only function names and addresses are available. File/line numbers require additional processing post-crash.

## System Information Queries

The system_info module queries CPU, memory, GPU, OS, battery, and locale information for adaptive quality, telemetry, and diagnostics:

### CPU Information

```cpp
struct CpuInfo {
    char brand[128];            ///< "Apple M1" or "Intel Core i7-13700K"
    uint32_t physicalCores;     ///< Actual core count (P-cores on Apple)
    uint32_t logicalCores;      ///< Logical CPUs (E-cores on Apple)
    uint32_t l1CacheKB;         ///< L1 data cache size
    uint32_t l2CacheKB;         ///< L2 cache per core
    uint32_t l3CacheKB;         ///< L3 shared cache
    bool hasSSE42, hasAVX, hasAVX2, hasAVX512;  ///< SIMD features (Intel)
    bool hasFMA, hasAESNI;
};

CpuInfo GetCpuInfo();
```

Queries via:
- `sysctl("machdep.cpu.brand_string", ...)` — CPU brand name
- `sysctl("hw.physicalcpu", ...)` — physical core count (P-cores)
- `sysctl("hw.logicalcpu", ...)` — logical CPU count (P+E cores)
- `sysctl("hw.l1icachesize", "hw.l2cachesize", "hw.l3cachesize")` — cache sizes
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

Queries via:
- `sysctl("hw.memsize", ...)` — total physical RAM
- Mach VM API (`mach_host_self()`, `host_statistics64()`) — available memory and page stats

### OS Information

```cpp
struct OsInfo {
    char name[128];                ///< "macOS 14.2.1"
    uint32_t majorVersion;         ///< OS major version
    uint32_t minorVersion;         ///< OS minor version
    uint32_t buildNumber;          ///< OS build number
    char architecture[16];         ///< "arm64", "x86_64"
};

OsInfo GetOsInfo();
```

Queries via:
- `sysctl("kern.osproductname", "kern.osrelease", ...)` — OS name and version
- `sysctl("hw.machine", ...)` — architecture (arm64 for Apple Silicon, x86_64 for Intel)

### GPU Information

```cpp
struct GpuInfo {
    char name[256];                 ///< "Apple M1", "Apple M2 Pro", "NVIDIA RTX 4090"
    char driverVersion[64];         ///< Driver version
    uint32_t vendorId;              ///< PCI vendor ID (0x106b = Apple, 0x10de = NVIDIA)
    uint32_t deviceId;              ///< PCI device ID
    uint64_t dedicatedVRAM_MB;      ///< Dedicated VRAM (shared on Apple Silicon)
    uint64_t sharedVRAM_MB;         ///< Shared memory (unified on Apple Silicon)
};

GpuInfo GetGpuInfo();
```

Queries via:
- Metal API enumeration (`MTLCreateSystemDefaultDevice()`) — GPU name, VRAM capacity
- IOKit queries — PCI vendor/device IDs for discrete GPUs (Intel Macs)

**Note**: On Apple Silicon (M1/M2/etc), all VRAM is unified; dedicated/shared distinction is less meaningful.

### Battery Information

```cpp
struct BatteryInfo {
    bool hasBattery;        ///< True if laptop has battery
    bool isCharging;        ///< True if AC adapter is online
    uint8_t percentage;     ///< Charge 0–100, or 255 if unavailable
};

BatteryInfo GetBatteryInfo();
```

Queries via:
- IOKit battery service enumeration (IOKit private APIs)
- `pmset` command for AC status (fallback)

Returns all-zero for desktops.

### Locale Information

```cpp
struct LocaleInfo {
    char language[16];      ///< "en", "pt", etc.
    char region[16];        ///< "US", "BR", etc.
};

LocaleInfo GetLocaleInfo();
```

Queries via:
- `setlocale(LC_ALL, "")` — user's default locale
- `NSLocale` API for region and language preferences

Used for language/region-specific game content and UI.

### Logging System Info

```cpp
/// Calls all query functions and logs results at engine startup.
void LogSystemInfo();
```

Example output:

```
[Platform] CPU: Apple M1 (8 cores / 8 threads)
[Platform]  L1: 192 KB, L2: 4096 KB, L3: 0 KB (unified)
[Platform]  Features: SSE4.2, AVX, AVX2, FMA
[Platform] Memory: 24.0 GB / 16.0 GB (100% available)
[Platform] OS: macOS 14.2.1 (arm64)
[Platform] GPU: Apple M1 (shared memory)
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
    void* mapping;      ///< Shared memory object descriptor
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

**Use case**: Frame streaming from runtime to extension. Accessed via POSIX shm_open/mmap.

## Design Decisions

### Cocoa vs Other Frameworks

UzEngine uses native Cocoa (AppKit) for immediate compatibility:
- NSWindow is the standard macOS window API
- CAMetalLayer is the recommended Metal rendering surface
- NSEvent provides access to all input types
- Cocoa handles platform conventions (fullscreen, minimize, keyboard shortcuts)

SwiftUI and Catalyst are avoided to maintain C++ compatibility and direct Metal access.

### Metal vs OpenGL

Metal is the modern Apple graphics API:
- Required for iOS/iPadOS support
- Better performance (lower CPU overhead)
- Metal Shading Language parallels HLSL/GLSL
- Native on all current Macs and iOS devices

OpenGL is deprecated on macOS 12+.

### UTF-8 Paths

macOS uses UTF-8 natively in POSIX APIs (unlike Windows UTF-16). All file APIs accept `const char* path` directly with UTF-8 encoding.

### FSEvents vs Polling

Directory watching uses FSEvents instead of polling:
- More efficient (zero CPU when idle)
- Event-driven (instant callback on file change)
- Native macOS technology (standard since 10.7)
- Supports recursive directory watching

### Async-Signal-Safe Crash Handler

The signal handler avoids heap allocation, locks, and UZ_LOG() to prevent deadlock on corrupted heap or infinite recursion. Only `backtrace()`, `memcpy()`, `_NSGetExecutablePath()`, `mkdir()`, `snprintf()`, and `write()` are called in the handler itself.

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| **Display Server** | Metal backend | Cocoa native | Cocoa native |
| **Window** | FMacWindow | WindowDriver::Cocoa | PlatformWindow (Cocoa) |
| **Metal Layer** | CAMetalLayer | CAMetalLayer | CAMetalLayer |
| **Input** | FSlateApplication | Cocoa events, XInput2 | InputSystem (keyCode translation) |
| **File I/O** | POSIX open/read/write | FileAccess + threading | POSIX (open/read/write/fcntl) |
| **Directory Watch** | FSEvents wrapper | DirectoryWatcher | DirectoryWatcher (FSEvents) |
| **Memory-Mapped Files** | mmap PROT_READ | FileAccess::read | MemoryMappedFile (mmap) |
| **Process Creation** | fork/execv | OS::execute | ProcessCreate (fork/execvp/pipes) |
| **DLL Loading** | dlopen/dlsym | load_dynamic | LoadDll/GetDllSymbol (dlopen/dlsym) |
| **Crash Handler** | Signal handler + Mach | CrashHandlerException | Signal handler + backtrace |
| **Stack Walking** | backtrace_symbols | backtrace | backtrace + dladdr + __cxa_demangle |
| **System Paths** | FPaths | XDG/standard | macOS standard (~/Library/) |
| **CPU Info** | sysctl | sysctl + Metal | sysctl + __cpuid_count |
| **Memory Info** | Mach VM | sysctl | Mach VM API |
| **GPU Info** | Metal API | Metal API | Metal API + IOKit |
| **Locale** | NSLocale API | OS.get_locale | NSLocale + setlocale |

## Best Practices & Rules

### Cocoa Window Management

1. **ALWAYS use NSWindow + CAMetalLayer for Metal rendering** — enables efficient GPU rendering
   - NSWindow handles window manager integration (fullscreen, minimize, title)
   - CAMetalLayer is the standard Metal rendering surface
2. **NEVER call Cocoa APIs from background threads** — wrap in dispatch_main_async or similar
3. **ALWAYS implement NSWindowDelegate for resize/close callbacks** — enables responsive window handling
4. **NEVER assume window focus** — check m_focused flag before rendering expensive frames

### File System

5. **ALWAYS use `O_CLOEXEC` on open/pipe/socket** — prevents accidental FD leaks to child processes
6. **NEVER assume parent directories exist** — auto-create with `mkdir -p` equivalent before write
7. **ALWAYS respect macOS standard directories** — users expect ~/Library/Application Support/, not /opt/
8. **NEVER hard-code library names** — use `dlopen()` with .dylib extension for portability
9. **ALWAYS handle EINTR in read/write loops** — signals can interrupt system calls
10. **NEVER use `fnmatch()` on full paths** — match only filenames in directory enumeration

### Directory Watching

11. **ALWAYS use FSEvents with kFSEventStreamCreateFlagFileEvents** — file-level granularity for cache invalidation
12. **NEVER add duplicate stream references** — one stream per directory
13. **ALWAYS drain FSEvents in batches** — multiple events may arrive in single callback

### Crash Handling

14. **ALWAYS use `SA_RESETHAND`** — ensures second crash invokes default handler (core dump) not infinite loop
15. **NEVER call malloc/UZ_LOG in signal handler** — async-signal-safe functions only
16. **ALWAYS use _NSGetExecutablePath for crash reporting** — platform-specific alternative to /proc/self/exe
17. **NEVER use DbgHelp-style symbol resolution in signal context** — do it offline post-crash

### Process Management

18. **ALWAYS use `waitpid()` with WNOHANG** — check exit status without blocking main loop
19. **NEVER leak pipe FDs** — call ProcessClose() in all code paths (success, timeout, error)
20. **ALWAYS set stdin/stdout/stderr pipe in child** — dup2() after fork(), before execvp()
21. **NEVER rely on SIGCHLD** — some applications disable it; use explicit waitpid() polling

### Performance

22. **ALWAYS use mmap for large read-only files** — zero-copy access, faster than read()
23. **NEVER enumerate same directory repeatedly** — cache results or use FSEvents-based auto-reload
24. **ALWAYS use backtrace() not dladdr in signal handler** — dladdr is non-async-signal-safe

## Future Work

The following subsystems are planned but not yet implemented:

- **Clipboard**: Copy/paste support via NSPasteboard
- **Drag & Drop**: File/image drag-and-drop via NSDraggingDestination
- **Native Dialogs**: File open/save dialogs via NSSavePanel
- **Wayland Support** (Linux): Planned for Phase 4.10.1
- **Windows Support**: Planned for Phase 4.10.0 (simultaneous with macOS)
