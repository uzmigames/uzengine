# Platform — Abstraction Interface

> **Status**: Planned (Phase 4.11.0)
> **Source**: `platform/windows/`, `platform/linux/`, `platform/common/platform_types.h`
> **Depends on**: Platform-specific APIs (Win32, X11, Wayland)

## Purpose

UzEngine separates platform-specific code from engine logic using vtable interfaces. Instead of engine code depending on Win32 (HWND, HANDLE) or Linux (Display*, Window) types, everything depends on platform-agnostic abstractions: `IPlatformWindow`, `IPlatformInput`, `IPlatformFile`, `IPlatformProcess`, `IPlatformMisc`.

This design enables multi-platform support: new platforms (Linux, macOS) plug in as new implementations without changing game code. Each platform provides a factory that creates the correct implementation at runtime or compile-time.

## Architecture

```
┌─────────────────────────────────────────────────┐
│ Engine Code (InputSystem, Renderer, AssetMgr)  │
│ Depends only on IPlatform* interfaces           │
└─────────────────────────────────────────────────┘
                       ↑
        ┌──────────────┼──────────────┐
        │              │              │
    IPlatformWindow  IPlatformFile  IPlatformMisc
    IPlatformInput   IPlatformProcess
        │              │              │
        ↓              ↓              ↓
    ┌─────────────────────────────────┐
    │ Platform-Specific Implementations│
    ├─────────────────────────────────┤
    │ WindowsWindow, WindowsInput,     │
    │ WindowsFile, WindowsProcess, etc │
    │ (Win32, DXC, COM APIs)          │
    └─────────────────────────────────┘
        AND
    ┌─────────────────────────────────┐
    │ LinuxWindow, LinuxInput,        │
    │ LinuxFile, LinuxProcess, etc    │
    │ (X11, Wayland, POSIX APIs)      │
    └─────────────────────────────────┘
```

## Interfaces

### IPlatformWindow

Manages OS window creation, state, and message pumping.

```cpp
/// Platform-agnostic window interface.
struct IPlatformWindow {
    virtual ~IPlatformWindow() = default;

    /// Create a new window with given description.
    virtual bool Create(const WindowDesc& desc) = 0;

    /// Process pending OS messages (non-blocking).
    /// Returns false if window closed.
    virtual bool PumpMessages() = 0;

    /// Destroy the window and release resources.
    virtual void Destroy() = 0;

    /// Query if window is still open.
    virtual bool IsOpen() const = 0;

    /// Check if window was resized since last PumpMessages().
    virtual bool WasResized() const = 0;

    /// Get opaque native window handle (safe cross-platform).
    virtual NativeWindowHandle GetNativeHandle() const = 0;
};
```

### IPlatformInput

Queries keyboard, mouse, and gamepad state.

```cpp
/// Platform-agnostic input interface.
struct IPlatformInput {
    virtual ~IPlatformInput() = default;

    /// Query key state (pressed, released, repeated).
    virtual KeyState GetKeyState(Key key) const = 0;

    /// Get current mouse position in screen coordinates.
    virtual Vec2 GetMousePosition() const = 0;

    /// Get mouse wheel delta this frame.
    virtual float GetMouseWheel() const = 0;

    /// Query mouse button state.
    virtual bool IsMouseButtonDown(MouseButton btn) const = 0;
};
```

### IPlatformFile

Abstracts file I/O, directory enumeration, and file watching.

```cpp
/// Platform-agnostic file I/O interface.
struct IPlatformFile {
    virtual ~IPlatformFile() = default;

    /// Read entire file into memory. Returns empty on failure.
    virtual std::vector<uint8_t> Read(const char* path) = 0;

    /// Write bytes to file. Returns bytes written.
    virtual size_t Write(const char* path, const uint8_t* data, size_t size) = 0;

    /// Check if file exists.
    virtual bool Exists(const char* path) const = 0;

    /// Get file size in bytes.
    virtual uint64_t GetSize(const char* path) const = 0;

    /// Watch directory for changes. Callback invoked on file events.
    virtual bool Watch(const char* dir, FileWatchCallback cb) = 0;
};
```

### IPlatformProcess

Handles DLL loading, process creation, and environment.

```cpp
/// Platform-agnostic process/DLL management.
struct IPlatformProcess {
    virtual ~IPlatformProcess() = default;

    /// Load DLL/shared object.
    virtual DllHandle LoadDll(const char* path) = 0;

    /// Get symbol address from loaded DLL.
    virtual void* GetDllSymbol(DllHandle dll, const char* symbol) = 0;

    /// Unload DLL.
    virtual void FreeDll(DllHandle dll) = 0;

    /// Create new process with optional pipe capture.
    virtual ProcessHandle CreateProcess(const ProcessDesc& desc) = 0;

    /// Get environment variable.
    virtual std::string GetEnv(const char* name) const = 0;
};
```

### IPlatformMisc

System information, timestamps, UUIDs, locale.

```cpp
/// Platform-agnostic miscellaneous utilities.
struct IPlatformMisc {
    virtual ~IPlatformMisc() = default;

    /// Get CPU information (cores, brand, features).
    virtual CpuInfo GetCpuInfo() const = 0;

    /// Get memory information (total, available).
    virtual MemoryInfo GetMemoryInfo() const = 0;

    /// Get OS information (name, version, architecture).
    virtual OsInfo GetOsInfo() const = 0;

    /// Get current time in milliseconds since epoch.
    virtual uint64_t GetTimestampMs() const = 0;

    /// Generate UUID v4.
    virtual std::string GenerateUuid() const = 0;
};
```

## Platform Factory

Create platform implementations without exposing platform-specific types:

```cpp
/// Factory functions (platform-specific implementation).
namespace Platform {
    /// Create window for current platform.
    IPlatformWindow* CreateWindow();

    /// Create input system for current platform.
    IPlatformInput* CreateInput();

    /// Create file I/O for current platform.
    IPlatformFile* CreateFile();

    /// Create process manager for current platform.
    IPlatformProcess* CreateProcess();

    /// Create miscellaneous utilities for current platform.
    IPlatformMisc* CreateMisc();
}
```

## Linux Backend Implementation

The Linux platform backend provides X11 window management, POSIX file I/O, and system integration:

```
IPlatformWindow & IPlatformInput
    ├── PlatformWindow (X11 native)
    │   ├── XCreateSimpleWindow, XMapWindow, XDestroyWindow
    │   ├── XNextEvent loop for key/mouse/configure events
    │   └── EWMH support (_NET_WM_STATE_FULLSCREEN)
    │
    └── InputSystem
        ├── X11 keysym → Windows VK_* translation
        ├── Per-frame state tracking (current vs previous)
        └── Mouse position and scroll accumulation

IPlatformFile
    ├── FileSystem operations (POSIX open/read/write)
    │   ├── UTF-8 path handling (native on Linux)
    │   ├── Parent directory creation (mkdir -p equivalent)
    │   └── FileResult enum for error codes
    │
    ├── DirectoryWatcher (inotify-based)
    │   ├── IN_CLOSE_WRITE, IN_CREATE, IN_DELETE, IN_MOVED_TO
    │   ├── Recursive directory watching
    │   └── Background thread with poll() + self-pipe
    │
    └── MemoryMappedFile (mmap PROT_READ)
        └── Zero-copy read access for large assets

IPlatformProcess
    ├── DLL Loader (dlopen/dlsym/dlclose)
    │   ├── RTLD_NOW for immediate symbol resolution
    │   └── dlerror() reporting for missing symbols
    │
    └── Process Management (fork/execvp)
        ├── Piped stdout/stderr capture
        ├── Non-blocking reads via fcntl O_NONBLOCK
        └── waitpid for exit code retrieval

IPlatformMisc
    ├── System Information
    │   ├── CPU: brand, core counts, cache, feature flags via CPUID + /proc/cpuinfo
    │   ├── Memory: /proc/meminfo parsing for total/available RAM
    │   ├── OS: uname() for kernel version and architecture
    │   ├── GPU: /sys/class/drm/ for PCI vendor/device IDs
    │   ├── Battery: /sys/class/power_supply/ for charge status
    │   └── Locale: setlocale + LANG env var for language/region
    │
    └── Environment & Paths
        ├── getenv/setenv for environment variables
        └── XDG standard paths ($XDG_DATA_HOME, $XDG_CONFIG_HOME, etc.)

Crash Handler
    ├── Signal handlers for SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL
    ├── Async-signal-safe backtrace() capture
    ├── Output to {exe_dir}/crashes/{timestamp}/
    └── crash.txt (human-readable) + crash.json (structured)

Stack Walker
    ├── backtrace() for raw frame capture
    ├── dladdr() + __cxa_demangle() for symbol resolution
    └── FormatStackTrace() for human-readable output
```

### Key Differences from Windows

| Aspect | Windows | Linux |
|--------|---------|-------|
| Window system | Win32 HWND | X11 Window |
| File paths | UTF-16 + POSIX conversion | UTF-8 native |
| File I/O | CreateFileA, ReadFile, WriteFile | open, read, write |
| File watcher | ReadDirectoryChangesW | inotify |
| Memory-mapped files | CreateFileMapping, MapViewOfFile | mmap(PROT_READ, MAP_PRIVATE) |
| Process creation | CreateProcessW with STARTUPINFO | fork/execvp with pipes |
| DLL loading | LoadLibraryA, GetProcAddress | dlopen/dlsym |
| Crash handler | SEH, MiniDumpWriteDump | sigaction, backtrace() |
| Symbol resolution | DbgHelp (PDB files) | dladdr + __cxa_demangle |
| System info | WMI, registry, GetSystemInfo | /proc, /sys, uname(), CPUID |

## Opaque Handles

Instead of exposing HWND or X11 Display*, platform types wrap OS handles:

```cpp
/// Opaque native window handle (platform-specific implementation).
struct NativeWindowHandle {
    void* handle;
    void* display;  // X11 Display*, nullptr on Windows
};

using DllHandle = void*;
using ProcessHandle = void*;
using FileWatchHandle = void*;
```

## Usage

```cpp
// Platform factory creates correct implementation
auto window = Platform::CreateWindow();
auto input = Platform::CreateInput();
auto file = Platform::CreateFile();

WindowDesc desc{};
desc.title = "UzEngine";
desc.width = 1280;
desc.height = 720;
window->Create(desc);

// Use platform-agnostic APIs only
while (window->IsOpen()) {
    window->PumpMessages();

    auto key_state = input->GetKeyState(Key::Escape);
    if (key_state == KeyState::Pressed) {
        break;
    }

    std::vector<uint8_t> data = file->Read("assets/config.json");
}

window->Destroy();
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Abstraction | `FGenericPlatform*` vtable | `OS` singleton + `DisplayServer` | `IPlatform*` interfaces + factory |
| Window abstraction | `FGenericWindow` | `DisplayServer::window_set_mode()` | `IPlatformWindow` |
| Input abstraction | `IInputInterface` | `Input` singleton | `IPlatformInput` |
| File abstraction | `IPlatformFile` | `FileAccess` hierarchy | `IPlatformFile` |
| Process abstraction | `FPlatformProcess` | `OS::execute()` | `IPlatformProcess` |
| Multi-platform | Yes (Win, Mac, Linux) | Yes (Win, Mac, Linux) | Windows done, Linux planned, macOS TBD |
| Factory pattern | Compile-time selects via `#if PLATFORM_*` | Runtime checks | Compile-time via `#if PLATFORM_*` |

## Rules

1. **NEVER use platform-specific types in engine code** — HWND, Display*, X11 Window must not appear outside `platform/*/` directories
2. **ALL platform operations go through interfaces** — no raw Win32/X11 calls from game code
3. **IPlatform* interfaces are pure virtual** — no default implementations (allows clean per-platform overrides)
4. **Factory functions encapsulate creation** — game code calls `Platform::CreateWindow()`, not `new WindowsWindow()`
5. **NativeWindowHandle wraps OS handles** — swap chain creation uses `GetNativeHandle()`, not `GetHWND()`
