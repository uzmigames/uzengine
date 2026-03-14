# UE5 Platform Abstraction Layer - Comprehensive Inventory

## Overview

UE5 organizes its platform abstraction into three distinct layers:

1. **ApplicationCore** — Window management, input, application lifecycle, display/monitor info
2. **Core/HAL** — Low-level Hardware Abstraction Layer: threading, memory, file I/O, processes, OS services
3. **RHI** — Render Hardware Interface: graphics API abstraction (DX12, Vulkan, Metal, etc.)

All platform abstractions follow a **two-tier pattern**:
- **Generic prefix** (e.g., `FGenericPlatformFile`) — Base interface/defaults
- **Platform-specific** (e.g., `FWindowsPlatformFile`, `FLinuxPlatformFile`) — Concrete implementations

File locations:
- **Generic definitions**: `Engine/Source/Runtime/Core/Public/GenericPlatform/*.h` and `Engine/Source/Runtime/ApplicationCore/Public/GenericPlatform/*.h`
- **Windows impl**: `Engine/Source/Runtime/Core/Private/Windows/*.cpp` and `Engine/Source/Runtime/ApplicationCore/Public/Windows/*.h`
- **Linux impl**: `Engine/Source/Runtime/Core/Private/Linux/*.cpp` and `Engine/Source/Runtime/ApplicationCore/Public/Linux/*.h`
- **Mac impl**: `Engine/Source/Runtime/ApplicationCore/Public/Mac/*.h` and `/Private/Mac/*.cpp`

---

## 1. WINDOW MANAGEMENT

### Interface: FGenericWindow (ApplicationCore)

**Location**: `ApplicationCore/Public/GenericPlatform/GenericWindow.h`

**Key abstraction**:
```cpp
class FGenericWindow
{
    // Lifecycle
    virtual void Initialize(...) = 0;
    virtual void Destroy() = 0;

    // Geometry
    virtual void ReshapeWindow(int32 X, int32 Y, int32 Width, int32 Height) = 0;
    virtual bool IsMaximized() const = 0;
    virtual bool IsMinimized() const = 0;
    virtual bool IsFullscreen() const = 0;
    virtual void SetWindowMode(EWindowMode Mode) = 0;

    // State
    virtual void Show() = 0;
    virtual void Hide() = 0;
    virtual void Enable() = 0;
    virtual void Disable() = 0;
    virtual bool IsVisible() const = 0;

    // Focus & Activation
    virtual void SetFocus() = 0;
    virtual void* GetOSWindowHandle() const = 0;

    // DPI
    virtual float GetDPIScaleFactor() const = 0;
    virtual void AdjustCachedSize(FVector2D& Size) const = 0;

    // Fullscreen/Monitor
    virtual bool GetRestoredDimensions(int32& X, int32& Y, int32& W, int32& H) const = 0;

    // Transparency
    virtual EWindowTransparency GetTransparencySupport() const = 0;
};
```

### Windows Implementation: FWindowsWindow

**Location**: `ApplicationCore/Public/Windows/WindowsApplication.h` (interface stub)
**Implementation**: `ApplicationCore/Private/Windows/WindowsWindow.cpp`

**What it implements**:
- **Native handle**: HWND storage and retrieval
- **Window creation**: Calls `CreateWindowEx()` with appropriate style flags
- **Sizing/positioning**: `SetWindowPos()`, `GetWindowRect()`, `GetClientRect()`
- **Maximized/Minimized/Fullscreen state**: Maintains state flags, responds to WM_SIZE messages
- **DPI awareness**: Caches DPI per-monitor, applies DPI scaling to client coords
- **Taskbar integration**: Uses `ITaskbarList3` for progress, overlay icons
- **Drag-drop**: Implements OLE drop target via `IDropTarget`

**Windows-specific data members**:
```cpp
HWND HWnd;
RECT WindowRect;  // Last known window rectangle
EWindowMode WindowMode;
bool bIsMaximized, bIsMinimized;
float DPIScaleFactor;
IDropTarget* DropTarget;  // OLE drag-drop
FTaskbarList TaskbarList;
```

### Linux Implementation: FLinuxWindow

**Location**: `ApplicationCore/Public/Linux/LinuxWindow.h`

**What it implements**:
- **Native handle**: SDL_Window* storage
- **Window creation**: Calls `SDL_CreateWindow()` with SDL_WINDOW_VULKAN or similar
- **Sizing/positioning**: `SDL_SetWindowPosition()`, `SDL_GetWindowSize()`, etc.
- **Maximized/Minimized state**: Responds to SDL_WINDOWEVENT_MAXIMIZED, SDL_WINDOWEVENT_MINIMIZED
- **Fullscreen**: `SDL_SetWindowFullscreen()` with SDL_WINDOW_FULLSCREEN or SDL_WINDOW_FULLSCREEN_DESKTOP
- **DPI awareness**: Queries `SDL_GetWindowDisplayScale()`, caches DPI
- **No taskbar integration** (Linux doesn't have an equivalent)
- **Drag-drop**: Implements via SDL drag-drop events

**Linux-specific data members**:
```cpp
SDL_Window* HWindow;  // SDL_Window pointer
FVector2D SizeInScreen;
FVector2D PositionInScreen;
bool bIsMaximized, bIsMinimized;
float DPI;
```

### Mac Implementation: FCocoaWindow

**Location**: `ApplicationCore/Public/Mac/CocoaWindow.h`

**What it implements**:
- **Native handle**: NSWindow* storage
- **Window creation**: Allocates NSWindow with NSRect
- **Sizing/positioning**: `[window setFrame:display:animate:]`
- **Fullscreen**: Toggles NSFullScreenWindowMask
- **Transparency**: Uses `[window setAlphaValue:]` and `[window setOpaque:]`

---

## 2. APPLICATION LIFECYCLE & MESSAGE PUMP

### Interface: GenericApplication (ApplicationCore)

**Location**: `ApplicationCore/Public/GenericPlatform/GenericApplication.h`

**Key abstraction**:
```cpp
class GenericApplication
{
    // Message handling
    virtual void PumpMessages(const float TimeDelta) = 0;
    virtual void ProcessDeferredEvents(const float TimeDelta) = 0;
    virtual void Tick(const float TimeDelta) = 0;

    // Input devices
    virtual void PollGameDeviceState(const float TimeDelta) = 0;
    virtual IInputInterface* GetInputInterface() = 0;

    // Window management
    virtual TSharedRef<FGenericWindow> MakeWindow() = 0;
    virtual void InitializeWindow(TSharedRef<FGenericWindow>&, ...) = 0;

    // Display metrics
    virtual void GetInitialDisplayMetrics(FDisplayMetrics& OutMetrics) const = 0;
    // Broadcasts FOnDisplayMetricsChanged event when resolution/monitors change

    // Capture & Focus
    virtual void SetCapture(const TSharedPtr<FGenericWindow>& InWindow) = 0;
    virtual void* GetCapture() const = 0;
    virtual TSharedPtr<FGenericWindow> GetWindowUnderCursor() = 0;

    // Minimized state
    virtual bool IsMinimized() const = 0;

    // Input state
    virtual FModifierKeysState GetModifierKeys() const = 0;
    virtual bool IsMouseAttached() const = 0;
    virtual bool IsGamepadAttached() const = 0;

    // Text input system
    virtual ITextInputMethodSystem* GetTextInputMethodSystem() = 0;

    // Accessibility
    virtual void SetAccessibleMessageHandler(...) = 0;
};
```

### Windows Implementation: FWindowsApplication

**Location**: `ApplicationCore/Public/Windows/WindowsApplication.h`

**Key behaviors**:
- **Message pump**: Uses Win32 message loop via `PeekMessage()` / `DispatchMessage()`
- **Raw input**: Registers for WM_INPUT to capture raw mouse/keyboard at high precision
- **Message routing**: Deferred message queue to handle async processing
- **Input devices**: DirectInput for gamepad, XInput as fallback
- **Virtual keyboard**: Responds to Windows soft keyboard show/hide
- **Cursor**: Wraps Win32 cursor API (SetCursor, GetCursorPos, ClipCursor)
- **Text input**: IME via Windows IME API

**Key data**:
```cpp
HINSTANCE InstanceHandle;
TArray<TSharedPtr<FWindowsWindow>> Windows;
TSharedPtr<FWindowsWindow> CurrentFocusWindow;
TArray<FDeferredWindowsMessage> DeferredMessages;  // Batched messages
FTaskbarList TaskbarList;
bool bAllowHighResolutionMouse;
FCriticalSection CursorCriticalSection;
```

### Linux Implementation: FLinuxApplication

**Location**: `ApplicationCore/Public/Linux/LinuxApplication.h`

**Key behaviors**:
- **Event loop**: Uses SDL event queue via `SDL_PollEvent()` / `SDL_WaitEventTimeout()`
- **Event dispatching**: Finds window from SDL event, routes to appropriate handler
- **Gamepad support**: Uses SDL joystick API
- **Cursor**: SDL cursor API (SDL_SetCursor, SDL_GetMouseState, SDL_CaptureMouse)
- **Virtual keyboard**: Responds to SDL_TEXTINPUT and SDL_TEXTEDITING
- **Raw input**: Not directly available; SDL provides normalized events
- **Modifier keys**: Queried from SDL_GetModState()

**Key data**:
```cpp
TArray<TSharedPtr<FLinuxWindow>> Windows;
TSharedPtr<FLinuxWindow> CurrentlyActiveWindow;
TSharedPtr<FLinuxWindow> CurrentFocusWindow;
TArray<SDL_Event> PendingEvents;
bool bActivateApp;
TMap<SDL_HWindow, FWindowProperties> WindowProperties;  // For event loop
```

---

## 3. INPUT HANDLING

### Interface: IInputInterface (ApplicationCore)

**Location**: `ApplicationCore/Public/GenericPlatform/IInputInterface.h`

**Key abstraction**:
```cpp
class IInputInterface
{
    virtual void SetCapture(const TSharedPtr<FGenericWindow>& InWindow) = 0;
    virtual void* GetCapture() const = 0;

    // Device enumeration
    virtual int32 GetNumGamepads() const = 0;
    virtual bool IsGamepadAttached(int32 ControllerId) const = 0;

    // High precision input
    virtual void SetHighPrecisionMouseMode(bool Enable, const TSharedPtr<FGenericWindow>& InWindow) = 0;
    virtual bool IsUsingHighPrecisionMouseMode() const = 0;

    // Haptic/Force feedback
    virtual void SetForceFeedbackChannelValue(...) = 0;
    virtual void SetHapticFeedbackValues(...) = 0;
};
```

### Windows Input Implementation

**Location**: `ApplicationCore/Private/Windows/WindowsInput.cpp` + Windows-specific input files

**Keyboard handling**:
- `WM_KEYDOWN`, `WM_KEYUP` — Raw key input
- `WM_CHAR` — Character input (after IME processing)
- Modifier keys tracked via `GetAsyncKeyState()` for independent left/right detection
- Caps Lock state via `GetKeyState(VK_CAPITAL)`

**Mouse handling**:
- `WM_MOUSEMOVE`, `WM_LBUTTONDOWN/UP`, `WM_MBUTTONDOWN/UP`, `WM_RBUTTONDOWN/UP`
- `WM_MOUSEWHEEL`, `WM_MOUSEHWHEEL`
- `WM_INPUT` (raw input) at higher precision (when `bAllowHighResolutionMouse=true`)
- Mouse capture via `SetCapture()` / `ReleaseCapture()`

**Gamepad (XInput/DirectInput)**:
- **XInput**: Primary on Win10+, up to 4 controllers, via `XInputGetState()`
- **DirectInput**: Fallback, supports arbitrary controllers via `IDirectInputDevice8`
- Force feedback via `XINPUT_VIBRATION`, `IDirectInputDevice8::SetParameters()`

**Touch/Pen**:
- `WM_TOUCH` events (tablet mode)
- `WM_POINTER*` events (Windows 8+)

### Linux Input Implementation

**Location**: `ApplicationCore/Private/Linux/LinuxInput.cpp`

**Keyboard handling**:
- `SDL_KEYDOWN`, `SDL_KEYUP` — Raw key events
- `SDL_TEXTINPUT` — Character input (after IME)
- `SDL_TEXTEDITING` — IME composition
- Modifier state from `SDL_GetModState()`

**Mouse handling**:
- `SDL_MOUSEMOTION`, `SDL_MOUSEBUTTONDOWN/UP`
- `SDL_MOUSEWHEEL`
- Mouse capture via `SDL_CaptureMouse()` (relative mode via `SDL_SetRelativeMouseMode()`)

**Gamepad**:
- SDL joystick API via `SDL_GameControllerOpen()` / `SDL_GameControllerGetState()`
- Rumble via `SDL_GameControllerRumble()` (if supported)
- No direct haptics API (driver-dependent)

**Touch**:
- `SDL_FINGERDOWN`, `SDL_FINGERUP`, `SDL_FINGERMOTION`
- Multi-touch via touch device ID

---

## 4. GRAPHICS/DISPLAY INFORMATION

### Interface: FDisplayMetrics (ApplicationCore)

**Location**: `ApplicationCore/Public/GenericPlatform/GenericApplication.h`

**Defines**:
```cpp
struct FMonitorInfo
{
    FString Name;           // "NVIDIA RTX 4090" or "Display 1"
    FString ID;             // Platform-specific unique ID
    int32 NativeWidth, NativeHeight;
    FIntPoint MaxResolution;
    FPlatformRect DisplayRect;  // Screen bounds in virtual coords
    FPlatformRect WorkArea;     // Excludable taskbar/dock area
    bool bIsPrimary;
    int32 DPI;              // Dots per inch
};

struct FDisplayMetrics
{
    int32 PrimaryDisplayWidth, PrimaryDisplayHeight;
    TArray<FMonitorInfo> MonitorInfo;
    FPlatformRect PrimaryDisplayWorkAreaRect;
    FPlatformRect VirtualDisplayRect;  // Union of all displays
    FVector4 TitleSafePaddingSize;     // TV safe zone
    FVector4 ActionSafePaddingSize;

    static void RebuildDisplayMetrics(FDisplayMetrics& OutMetrics);
    FPlatformRect GetMonitorWorkAreaFromPoint(const FVector2D& Point) const;
};
```

**Static rebuilding**:
- Called by `GenericApplication::GetInitialDisplayMetrics()`
- Platform implementation queries monitor enumeration, resolves primary display, computes safe zones

### Windows Implementation

**Location**: `ApplicationCore/Private/Windows/WindowsApplication.cpp`

**Monitor enumeration**:
- Calls `EnumDisplayMonitors()` callback for each connected monitor
- For each: `GetMonitorInfo()` → `MONITORINFO` with rcWork (task bar excluded)
- DPI via `GetDpiForMonitor()` (Windows 8.1+) or `GetDeviceCaps(hdc, LOGPIXELSY)`
- Primary monitor: marked via `MONITORINFO.dwFlags & MONITORINFOF_PRIMARY`

**Safe zones**:
- TV-safe areas set to 0 by default (PC has no analog signal limits)
- Can be overridden via CVar `r.TitleSafeZone.X` etc.

### Linux Implementation

**Location**: `ApplicationCore/Private/Linux/LinuxApplication.cpp`

**Monitor enumeration**:
- Uses SDL: `SDL_GetNumVideoDisplays()`, `SDL_GetDisplayBounds(i, &rect)`
- For each: `SDL_GetDisplayDPI()` for DPI
- Primary: first in the list (index 0)
- Work area: SDL doesn't provide taskbar info, defaults to full display rect

**Safe zones**:
- Defaults to 0 (no safe zone on Linux desktops)

---

## 5. FILE SYSTEM ABSTRACTION

### Interface: IPlatformFile (Core/HAL)

**Location**: `Core/Public/GenericPlatform/GenericPlatformFile.h`

**Key abstraction**:
```cpp
class IPlatformFile
{
    // File operations
    virtual IFileHandle* OpenRead(const TCHAR* Filename, bool bAllowWrite=false) = 0;
    virtual IFileHandle* OpenWrite(const TCHAR* Filename, bool bAppend=false, bool bAllowRead=false) = 0;
    virtual bool Delete(const TCHAR* Filename) = 0;
    virtual bool Copy(const TCHAR* Dst, const TCHAR* Src, bool bReplace=false) = 0;
    virtual bool Move(const TCHAR* Dst, const TCHAR* Src) = 0;
    virtual bool FileExists(const TCHAR* Filename) = 0;
    virtual bool IsReadOnly(const TCHAR* Filename) = 0;
    virtual bool SetReadOnly(const TCHAR* Filename, bool bNewReadOnlyState) = 0;
    virtual int64 FileSize(const TCHAR* Filename) = 0;

    // Directory operations
    virtual bool DirectoryExists(const TCHAR* Directory) = 0;
    virtual bool CreateDirectory(const TCHAR* Directory) = 0;
    virtual bool DeleteDirectory(const TCHAR* Directory) = 0;
    virtual FFileStatData GetStatData(const TCHAR* FilenameOrDirectory) = 0;

    // Enumeration
    virtual bool IterateDirectory(const TCHAR* Directory,
                                  FDirectoryVisitor& Visitor) = 0;
    virtual bool IterateDirectoryRecursively(const TCHAR* Directory,
                                             FDirectoryVisitor& Visitor) = 0;

    // Timestamps
    virtual FDateTime GetTimeStamp(const TCHAR* Filename) = 0;
    virtual void SetTimeStamp(const TCHAR* Filename, FDateTime DateTime) = 0;

    // Memory-mapped files
    virtual IMappedFileHandle* OpenMapped(const TCHAR* Filename) = 0;

    // Async I/O
    virtual IAsyncReadFileHandle* OpenAsyncRead(const TCHAR* Filename) = 0;

    // Wrapping/middleware
    virtual IPlatformFile* FindPlatformFile(const TCHAR* Name) = 0;
    virtual void SetLowerLevel(IPlatformFile* NewLowerLevel) = 0;
};

// Platform paths
class FPlatformFileManager
{
    static void Initialize();
    static IPlatformFile& Get();  // Singleton
};

// File handle abstraction
class IFileHandle
{
    virtual bool Read(uint8* Destination, int64 BytesToRead) = 0;
    virtual bool Write(const uint8* Source, int64 BytesToWrite) = 0;
    virtual int64 Tell() = 0;
    virtual bool Seek(int64 NewPosition) = 0;
    virtual bool SeekFromEnd(int64 NewPositionRelativeToEnd=0) = 0;
    virtual bool Flush(const bool bFullFlush=false) = 0;
    virtual int64 Size() = 0;
};
```

### Windows Implementation: FWindowsPlatformFile

**Location**: `Core/Private/Windows/WindowsPlatformFile.cpp`

**Behaviors**:
- Opens files via `CreateFileW()` with appropriate sharing/access flags
- Uses `GetFileAttributesW()` to check file/dir existence and readonly state
- Directory ops: `CreateDirectoryW()`, `RemoveDirectoryW()`, `FindFirstFileW()`
- Timestamps: `GetFileTime()`, `SetFileTime()`
- File locking: Opens with `FILE_SHARE_READ | FILE_SHARE_WRITE` if `bAllowWrite/bAllowRead` set
- Async I/O: Uses overlapped I/O via `OVERLAPPED`, `ReadFileEx()`, `WriteFileEx()`
- Memory mapping: `CreateFileMappingW()`, `MapViewOfFile()`
- Case-insensitive paths (Windows semantics)

**File handle impl**:
```cpp
class FWindowsFileHandle : public IFileHandle
{
    HANDLE FileHandle;
    bool bReadOnly;
};
```

### Linux Implementation: FLinuxPlatformFile

**Location**: `Core/Private/Linux/LinuxPlatformFile.cpp`

**Behaviors**:
- Opens files via `open()` syscall with O_RDONLY, O_WRONLY, O_APPEND flags
- Uses `stat()`, `access()` to check existence/permissions
- Directory ops: `mkdir()`, `rmdir()`, `opendir()`, `readdir()`
- Timestamps: `stat()` for mtime, `utimes()` / `utimensat()` to set
- File locking: Optional `flock()` or `fcntl()` for advisory locks
- Async I/O: `libaio` or thread pool (depends on config)
- Memory mapping: `mmap()` with PROT_READ/WRITE, MAP_SHARED
- Case-sensitive paths (Linux semantics)

**File handle impl**:
```cpp
class FLinuxFileHandle : public IFileHandle
{
    int FileHandle;  // fd from open()
    bool bWritable;
};
```

### Platform Paths (Core/HAL)

**Location**: `Core/Public/HAL/PlatformProcess.h`, `Core/Public/GenericPlatform/GenericPlatformFile.h`

**Abstract interface** (FPaths, FPlatformProcess):
```cpp
namespace FPaths
{
    // Core directories
    FString GetEngineDir();      // Engine root
    FString GetProjectDir();     // Project root
    FString GetGameSourceDir();  // Source/
    FString GetBinariesDir();    // Binaries/
    FString GetIntermediateDir();// Intermediate/
    FString GetSavedDir();       // Saved/

    // User directories
    FString GetUserSettingsDir();
    FString GetUserLogsDir();
    FString GetApplicationSettingsDir();

    // Temp
    FString GetTempRootPath();
};

namespace FPlatformProcess
{
    FString GetApplicationName();  // argv[0] basename
    FString GetModuleDirectory();  // Directory of exe/dll
};
```

**Windows implementation**:
- Engine dir: relative to executable or set at compile-time
- User settings: `%APPDATA%\Unreal Engine\Saved\`
- Temp: `GetTempPath()` → typically `%USERPROFILE%\AppData\Local\Temp\`

**Linux implementation**:
- Engine dir: relative to executable or set at compile-time
- User settings: `~/.local/share/UnrealEngine/` or `~/.config/UnrealEngine/`
- Temp: `TMPDIR` env var or `/tmp/`

---

## 6. THREADING

### Interface: FRunnableThread (Core/HAL)

**Location**: `Core/Public/HAL/RunnableThread.h`

**Key abstraction**:
```cpp
class FRunnableThread
{
    // Lifecycle
    static FRunnableThread* Create(
        class FRunnable* InRunnable,
        const TCHAR* ThreadName = nullptr,
        uint32 InStackSize = 0,
        EThreadPriority InThreadPriority = TPri_Normal,
        uint64 InThreadAffinityMask = 0);

    virtual void Kill(bool bShouldWait = true) = 0;
    virtual void WaitForCompletion() = 0;

    // State
    virtual uint32 GetThreadID() const = 0;
    virtual bool IsThreadRunning() const = 0;
};

class FRunnable
{
    virtual bool Init() = 0;
    virtual uint32 Run() = 0;
    virtual void Stop() = 0;
    virtual void Exit() = 0;
};

enum EThreadPriority
{
    TPri_Normal,
    TPri_AboveNormal,
    TPri_BelowNormal,
    TPri_Highest,
    TPri_Lowest,
    TPri_SlightlyBelowNormal,
};
```

### Windows Implementation

**Location**: `Core/Private/Windows/WindowsRunnableThread.cpp`

**Thread creation**:
- Uses `_beginthreadex()` (or `CreateThread()`)
- Stack size: `InStackSize` or platform default
- Returns `HANDLE` wrapped in `FWindowsRunnableThread`

**Priority mapping**:
```cpp
TPri_Normal          → THREAD_PRIORITY_NORMAL
TPri_AboveNormal     → THREAD_PRIORITY_ABOVE_NORMAL
TPri_BelowNormal     → THREAD_PRIORITY_BELOW_NORMAL
TPri_Highest         → THREAD_PRIORITY_HIGHEST
TPri_Lowest          → THREAD_PRIORITY_LOWEST
```

**Affinity**:
- `SetThreadAffinityMask()` to pin thread to cores
- `InThreadAffinityMask` is a bitmask of CPU cores

**Synchronization**:
- `WaitForSingleObject(hThread, INFINITE)` in `WaitForCompletion()`
- Termination: `TerminateThread()` (last resort)

### Linux Implementation

**Location**: `Core/Private/Linux/LinuxRunnableThread.cpp`

**Thread creation**:
- Uses `pthread_create()` with thread attributes
- Stack size: `pthread_attr_setstacksize()`

**Priority mapping**:
```cpp
TPri_Normal          → SCHED_OTHER (nice 0)
TPri_AboveNormal     → SCHED_OTHER (nice -5)
TPri_BelowNormal     → SCHED_OTHER (nice +5)
TPri_Highest         → SCHED_FIFO (real-time, requires CAP_SYS_NICE)
TPri_Lowest          → SCHED_OTHER (nice +19)
```

**Affinity**:
- `pthread_setaffinity_np()` with CPU set
- Requires CAP_SYS_NICE capability for real-time

**Synchronization**:
- `pthread_join()` in `WaitForCompletion()`
- Cancellation: `pthread_cancel()` (with cleanup handlers)

### Synchronization Primitives

#### Interface: FEvent (Core/HAL)

**Location**: `Core/Public/HAL/Event.h`

```cpp
class FEvent
{
    virtual bool Create(bool bIsManualReset=false) = 0;
    virtual void Trigger() = 0;
    virtual void Reset() = 0;
    virtual bool Wait(uint32 WaitTimeMs, bool bIgnoreThreadIdleStats=false) = 0;
    virtual bool IsManualReset() = 0;
};
```

**Windows impl** (`WindowsEvent.h`):
- Manual reset: `CreateEventW(NULL, TRUE, FALSE, NULL)` → auto-reset: `CreateEventW(NULL, FALSE, FALSE, NULL)`
- `Trigger()` → `SetEvent()`
- `Wait()` → `WaitForSingleObject(hEvent, WaitTime)`
- Timeout: converts `WaitTime` ms to Windows timeout (clamped to DWORD)

**Linux impl** (`PThreadEvent.h`):
- Manual reset: `pthread_cond_t` + `bool bIsManualReset` flag
- `Trigger()` → `pthread_cond_broadcast()` if manual, else `pthread_cond_signal()`
- `Wait()` → `pthread_cond_timedwait()` with timeout

#### Interface: FCriticalSection (Core/HAL)

**Location**: `Core/Public/HAL/CriticalSection.h`

```cpp
class FCriticalSection
{
    void Lock() = 0;
    void Unlock() = 0;
    bool TryLock() = 0;
};
```

**Windows impl**:
- Wraps `CRITICAL_SECTION` from Win32
- `Lock()` → `EnterCriticalSection()`
- `Unlock()` → `LeaveCriticalSection()`

**Linux impl**:
- Wraps `pthread_mutex_t`
- `Lock()` → `pthread_mutex_lock()`
- `Unlock()` → `pthread_mutex_unlock()`

---

## 7. MEMORY MANAGEMENT

### Interface: FGenericPlatformMemory (Core/HAL)

**Location**: `Core/Public/GenericPlatform/GenericPlatformMemory.h`

```cpp
struct FGenericPlatformMemoryConstants
{
    uint64 TotalPhysical;        // Total RAM
    uint64 TotalVirtual;         // Total VM limit
    SIZE_T PageSize;             // OS page size (4KB typical)
    SIZE_T OsAllocationGranularity;  // 64KB on Windows, 4KB on Linux
    SIZE_T BinnedPageSize;       // 64KB typical (for allocator)
    uint64 AddressStart, AddressLimit;  // Expected VA range
};

struct FGenericPlatformMemoryStats : FGenericPlatformMemoryConstants
{
    uint64 AvailablePhysical;
    uint64 AvailableVirtual;
    uint64 UsedPhysical;
    uint64 PeakUsedPhysical;
    uint64 UsedVirtual;
    uint64 PeakUsedVirtual;

    enum class EMemoryPressureStatus { Unknown, Nominal, Warning, Critical };
};

class FPlatformMemory
{
    static void Init();
    static FGenericPlatformMemoryStats GetStats();
    static void GetStatsForMallocProfiler(FGenericMemoryStats& OutStats);

    // Virtual memory
    static void* BinnedAllocFromOS(SIZE_T Size);  // For binned malloc
    static void BinnedFreeToOS(void* Ptr, SIZE_T Size);

    // Large pages
    static void EnableLargePages();
    static uint64 GetLargePageSize();

    // Pressure callback
    static void OnOutOfMemory(uint64 AllocationSize, uint32 AllocationAlignment, const ANSICHAR* BinnedType);
};
```

### Windows Implementation

**Location**: `Core/Private/Windows/WindowsPlatformMemory.cpp`

**Memory constants**:
- `TotalPhysical`: `GlobalMemoryStatusEx()` → `MEMORYSTATUSEX.ullTotalPhys`
- `PageSize`: 4096 bytes (x86/x64)
- `OsAllocationGranularity`: 65536 bytes (64KB)
- `BinnedPageSize`: 65536 bytes

**Memory stats**:
- Calls `GlobalMemoryStatusEx()` periodically to refresh available/used
- `GetProcessMemoryInfo()` (via PSAPI) for process-specific stats
- Pressure: computed from available/total ratio

**Virtual allocation**:
- `BinnedAllocFromOS()` → `VirtualAlloc(NULL, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)`
- Large page support: `VirtualAlloc(..., MEM_LARGE_PAGES)` requires `SeLockMemoryPrivilege`
- Windows 10 can request large pages (2MB) via `GetLargePageMinimum()`

**Crash on OOM**:
- `OnOutOfMemory()` logs error, triggers crash dump, terminates

### Linux Implementation

**Location**: `Core/Private/Linux/LinuxPlatformMemory.cpp`

**Memory constants**:
- `TotalPhysical`: Read from `/proc/meminfo` (MemTotal)
- `PageSize`: `sysconf(_SC_PAGESIZE)` → 4096 bytes
- `OsAllocationGranularity`: 4096 bytes
- `BinnedPageSize`: 65536 bytes (aligns with Windows for consistency)

**Memory stats**:
- Reads `/proc/meminfo` (MemAvailable, etc.)
- Process stats: `/proc/self/status` (VmRSS, VmPeak)
- Pressure: critical if available < 5% of total

**Virtual allocation**:
- `BinnedAllocFromOS()` → `mmap(NULL, Size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON)`
- Large pages: `mmap(..., MAP_HUGETLB)` (requires hugepages configured in kernel)
- Huge page size: typically 2MB (query via `/sys/kernel/mm/transparent_hugepage/hpage_pmd_size`)

**Allocators**:
- **MallocBinned3**: Primary, uses virtual memory pools, O(1) lookup
- **MallocStandard**: ANSI malloc wrapper (fallback)

---

## 8. PROCESS MANAGEMENT

### Interface: FGenericPlatformProcess (Core/HAL)

**Location**: `Core/Public/GenericPlatform/GenericPlatformProcess.h`

```cpp
struct FGenericPlatformProcess
{
    // Process creation
    static FProcHandle CreateProc(
        const TCHAR* URL,
        const TCHAR* Parms,
        bool bLaunchDetached=false,
        bool bLaunchHidden=false,
        bool bLaunchReallyHidden=false,
        uint32* OutProcessID=nullptr,
        int32 PriorityModifier=0,
        const TCHAR* OptionalWorkingDirectory=nullptr,
        void* PipeWrite_SendHandle=nullptr,
        void* PipeRead_ReceiveHandle=nullptr);

    // DLL management
    static void* GetDllHandle(const TCHAR* Filename);
    static void FreeDllHandle(void* DllHandle);
    static void* GetDllExport(void* DllHandle, const TCHAR* ProcName);
    static void AddDllDirectory(const TCHAR* Directory);
    static void PushDllDirectory(const TCHAR* Directory);
    static void PopDllDirectory();

    // Process inspection
    static bool IsApplicationRunning(uint32 ProcessID);
    static bool GetProcReturnCode(FProcHandle& ProcHandle, int32* OutReturnCode);
    static bool Terminate(FProcHandle& ProcHandle, bool bForce=false);
    static bool WaitForProc(FProcHandle& ProcHandle);
    static uint32 GetCurrentProcessId();
    static FString GetComputerName();
    static FString GetUserName();

    // Pipes & IPC
    static bool CreatePipe(void*& ReadPipe, void*& WritePipe);
    static bool ReadPipe(void* ReadPipe, FString& Result);
    static bool WritePipe(void* WritePipe, const FString& Message, FString* OutWritten=nullptr);
    static void ClosePipe(void*& Pipe);

    // Stack walk & crash
    static void StackWalk(...);
    static void GenerateCrashInfo(...);
    static void WriteCrashMinidump(...);
};
```

### Windows Implementation

**Location**: `Core/Private/Windows/WindowsPlatformProcess.cpp`

**Process creation**:
- `CreateProcessW()` with `PROCESS_INFORMATION`
- Detached: `CREATE_NEW_CONSOLE` or `DETACHED_PROCESS`
- Hidden: `CREATE_NO_WINDOW`, `SW_HIDE`
- Priority: `SetProcessPriorityBoost()`, `SetPriorityClass()` (ABOVE_NORMAL, BELOW_NORMAL, etc.)
- Pipes: inheritable handles passed as `hStdInput`, `hStdOutput`, `hStdError`

**DLL management**:
- `LoadLibraryW()` / `GetProcAddress()` / `FreeLibrary()`
- `LoadLibrarySearchUserDirs` (Windows 8.1+) for custom search paths

**Process inspection**:
- `OpenProcess()`, `GetExitCodeProcess()`, `TerminateProcess()`, `WaitForSingleObject()`

**Pipes**:
- `CreatePipe()` creates anonymous pipes, inheritable
- `WriteFile()`, `ReadFile()` with `OVERLAPPED` for async

**Stack walk**:
- Uses StackWalk64() with loaded PE/PDB data
- Generates human-readable call stack with function names, offsets, source lines (if PDB loaded)

### Linux Implementation

**Location**: `Core/Private/Linux/LinuxPlatformProcess.cpp`

**Process creation**:
- `fork()` + `execve()` pattern
- Detached: `fork()` + grandchild pattern (parent dies) or `setsid()`
- Hidden: redirect stdin/stdout/stderr to `/dev/null`
- Priority: `setpriority(PRIO_PROCESS)` (nice level)
- Pipes: anonymous pipes via `pipe()`, inherited in child

**DLL management**:
- `dlopen()` / `dlsym()` / `dlclose()` (POSIX dynamic linking)
- Search paths: `LD_LIBRARY_PATH` or `-rpath` at link time

**Process inspection**:
- `waitpid()`, `/proc/{pid}/stat` for status
- `SIGTERM` then `SIGKILL` to terminate

**Pipes**:
- `pipe()` creates fd pair
- `read()` / `write()` syscalls (inherently async-ready with O_NONBLOCK)

**Stack walk**:
- Uses `backtrace()` + `backtrace_symbols()` (glibc)
- Or `dladdr()` for symbol lookup
- Requires `-ldl -rdynamic` at link time

---

## 9. CRASH REPORTING & DEBUGGING

### Interface: FGenericCrashContext (Core/HAL)

**Location**: `Core/Public/HAL/PlatformCrashContext.h`

```cpp
struct FGenericCrashContext
{
    // Exception info
    virtual void UpdateCrashDescription(const TCHAR* NewDescription) = 0;

    // Report generation
    virtual void GenerateReport() = 0;
    virtual bool GetFullMinidumpFilename(FString& OutFilename) = 0;

    // System info
    FString ProcessPath;
    FString ExecutableName;
    FString GameName;
    uint32 ProcessID;
    uint64 ProcessStartTime;

    // Crash details
    FString ErrorDescription;
    FString SourceContext;
    FString CallStack;
    bool bIsEnsure;  // Warning vs crash
};
```

### Windows Implementation

**Location**: `Core/Private/Windows/WindowsPlatformCrashContext.cpp`

**Crash capture**:
- **SEH**: Wraps main via `__try/__except` or `SetUnhandledExceptionFilter()`
- **Structured Exception Handler**: Catches access violations, divide-by-zero, etc.
- Records: CPU state, memory dump, stack trace, loaded modules

**Minidump generation**:
- Uses MiniDumpWriteDump() from DbgHelp.dll
- Writes to `Saved/Crashes/` directory
- Includes thread info, loaded modules, memory (if `MINIDUMP_TYPE` includes heap)

**Stack walk**:
- StackWalk64() with symbol loading from PDB
- Outputs function name + offset + source file + line number

**Upload**:
- Can send to Epic's crash report server if enabled
- Includes hardware info, OS version, GPU info

### Linux Implementation

**Location**: `Core/Private/Linux/LinuxPlatformCrashContext.cpp`

**Crash capture**:
- **Signal handlers**: `sigaction(SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL)` etc.
- Captures current thread state, signal context
- Walks stack via backtrace()

**Minidump generation**:
- Writes to `Saved/Crashes/`
- Minimal format (no kernel MiniDump equivalent)
- Includes backtrace, loaded libraries (from /proc/self/maps), register state

**Stack walk**:
- Uses `backtrace()` (glibc) + `dladdr()` (libc)
- Requires `-rdynamic -ldl` at link time

**No direct upload** (unlike Windows) — logs locally, user must upload manually

---

## 10. OS-SPECIFIC SERVICES

### Interface: FGenericPlatformMisc (Core/HAL)

**Location**: `Core/Public/GenericPlatform/GenericPlatformMisc.h`

```cpp
struct FGenericPlatformMisc
{
    // CPU info
    static uint32 NumberOfCores();
    static uint32 NumberOfCoresIncludingHyperthreads();
    static uint64 GetPhysicalGBRam();
    static FString GetCpuBrand();
    static FString GetCpuModel();

    // GPU info
    static FString GetPrimaryGpuBrand();
    static uint32 GetVideoAdapterMemory();

    // Time
    static uint64 GetCycles();  // CPU cycles
    static double GetSecondsPerCycle();
    static uint64 Cycles64();   // 64-bit cycles

    // GUID
    static void CreateGuid(FGuid& OutGuid);

    // Debugging
    static bool IsDebuggerPresent();
    static void DebugBreak();
    static void OutputDebugString(const TCHAR* Text);

    // System paths
    static const TCHAR* GetSystemRoot();
    static const TCHAR* GetEnvironmentVariable(const TCHAR* VariableName);

    // Platform name
    static const TCHAR* GetPlatformName();
    static const TCHAR* GetOSVersion();

    // Processor control
    static void PrefetchMemory(void* Ptr, SIZE_T Bytes);
    static void Prefetch(void* Ptr);

    // CPU features
    static bool SupportsSSE4_1();
    static bool SupportsSSE4_2();
    static bool SupportsAVX();
    static bool SupportsAVX2();

    // Named events (profiling)
    static void BeginNamedEvent(const FColor& Color, const TCHAR* Text);
    static void EndNamedEvent();

    // Hardware quirks
    static EConvertibleLaptopMode GetConvertibleLaptopMode();
    static EDeviceScreenOrientation GetDeviceScreenOrientation();
};
```

### Windows Implementation

**Location**: `Core/Private/Windows/WindowsPlatformMisc.cpp`

**CPU info**:
- `GetSystemInfo()` → `SYSTEM_INFO.dwNumberOfProcessors`
- CPUID instruction for brand/model (via inline asm)
- Hyperthreading: CPUID leaf 0x1 EBX[23:16]

**GPU info**:
- Enumerates DXGI adapters (DirectX) to find primary GPU
- `IDXGIAdapter::GetDesc()` for brand name and VRAM
- Fallback: WMI query to Win32_VideoController

**Time**:
- `__rdtsc()` for cycle counter (x86-specific)
- `QueryPerformanceCounter()` for high-res timer

**GUID**:
- `CoCreateGuid()` from COM

**Debugging**:
- `IsDebuggerPresent()` via kernel function
- `DebugBreak()` triggers INT 3
- `OutputDebugString()` → kernel debug output

**Environment**:
- `GetEnvironmentVariableW()` for env vars
- `GetSystemDirectoryW()` for system root

**CPU features**:
- CPUID intrinsics (leaf 0x1, 0x7) to check SSE4.1, SSE4.2, AVX, AVX2

**Named events**:
- Integrates with ETW (Event Tracing for Windows) for profiler capture

### Linux Implementation

**Location**: `Core/Private/Linux/LinuxPlatformMisc.cpp`

**CPU info**:
- `sysconf(_SC_NPROCESSORS_CONF)` / `_SC_NPROCESSORS_ONLN` for core count
- `/proc/cpuinfo` for brand, model
- Hyperthreads: count "siblings" vs "cpu cores" in cpuinfo

**GPU info**:
- Queries PCI bus via `/sys/class/pci_bus/` (rarely used; requires GPU-specific drivers)
- Fallback: environment variables (`DISPLAY` for X11 GPU, `WAYLAND_DISPLAY`)
- VRAM: queried via Vulkan SDK if available

**Time**:
- `clock_gettime(CLOCK_MONOTONIC)` for high-res (nanosecond precision)
- `CLOCK_MONOTONIC_RAW` to bypass NTP adjustments

**GUID**:
- Reads from `/proc/sys/kernel/random/uuid` or generates via random bytes

**Debugging**:
- `gdb` detection: check `/proc/self/status` for TracerPid
- `raise(SIGTRAP)` for breakpoint
- `write(STDERR_FILENO, ...)` for output (no kernel debug output equivalent)

**Environment**:
- `getenv()` for env vars
- Root: hardcoded as `/`

**CPU features**:
- CPUID (if x86) via inline asm, or read from `/proc/cpuinfo` flags

---

## 11. ATOMICS & INTERLOCKED OPERATIONS

### Interface: FGenericPlatformAtomics (Core/HAL)

**Location**: `Core/Public/HAL/PlatformAtomics.h`

```cpp
struct FGenericPlatformAtomics
{
    static int32 InterlockedIncrement(int32 volatile* Value);
    static int32 InterlockedDecrement(int32 volatile* Value);
    static int32 InterlockedAdd(int32 volatile* Value, int32 Amount);
    static int32 InterlockedExchange(int32 volatile* Value, int32 Exchange);
    static int32 InterlockedCompareExchange(int32 volatile* Dest, int32 Exchange, int32 Comparand);

    static int64 InterlockedIncrement64(int64 volatile* Value);
    static int64 InterlockedDecrement64(int64 volatile* Value);
    static int64 InterlockedExchange64(int64 volatile* Value, int64 Exchange);
    static int64 InterlockedCompareExchange64(int64 volatile* Dest, int64 Exchange, int64 Comparand);

    static void* InterlockedExchangePtr(void* volatile* Ptr, void* NewPtr);
    static void* InterlockedCompareExchangePtr(void* volatile* Dest, void* Exchange, void* Comparand);
};
```

### Windows Implementation

**Location**: `Core/Private/Windows/WindowsPlatformAtomics.cpp`

**Operations** (all via intrinsics):
- `_InterlockedIncrement()` / `_InterlockedDecrement()` / `_InterlockedAdd()`
- `_InterlockedExchange()` / `_InterlockedCompareExchange()`
- 64-bit: `_InterlockedIncrement64()` etc. (on x64, uses native 64-bit ops; on x86 uses CMPXCHG8B)

**Memory barriers**:
- Acquire/Release semantics enforced by processor (x86 has strong memory model)
- `_InterlockedCompareExchange()` acts as full barrier

### Linux Implementation

**Location**: `Core/Private/Linux/LinuxPlatformAtomics.cpp`

**Operations** (GCC builtin atomics):
- `__sync_fetch_and_add()` / `__sync_sub_and_fetch()` / `__sync_fetch_and_or()`
- `__sync_val_compare_and_swap()` / `__sync_bool_compare_and_swap()`
- ARM: Uses `ldrex`/`strex` or `LL/SC` pair; x86 uses CMPXCHG / LOCK prefix

**Memory barriers**:
- `__sync_synchronize()` for full barrier
- Acquire/Release: via `__atomic_*()` C11 builtins with `__ATOMIC_ACQUIRE` / `__ATOMIC_RELEASE`

---

## 12. MATH & SIMD

### Interface: FGenericPlatformMath (Core/HAL)

**Location**: `Core/Public/HAL/PlatformMath.h`

```cpp
struct FGenericPlatformMath
{
    // Basic
    static constexpr float KINDA_SMALL_NUMBER;
    static constexpr float SMALL_NUMBER;
    static constexpr float BIG_NUMBER;

    // Float ops
    static FORCEINLINE float Frac(float Value);
    static FORCEINLINE float Fmod(float X, float Y);
    static FORCEINLINE float Sin(float Value);
    static FORCEINLINE float Cos(float Value);
    static FORCEINLINE float Sqrt(float Value);
    static FORCEINLINE float InvSqrt(float Value);
    static FORCEINLINE float Pow(float Base, float Exp);
    static FORCEINLINE int32 Trunc(float Value);

    // SIMD intrinsics (SSE/AVX)
    #if PLATFORM_CPU_X86_FAMILY
        // SSE: _mm_* intrinsics
        // AVX: _mm256_* intrinsics
    #endif
};
```

### Windows Implementation

**Location**: `Core/Private/Windows/WindowsPlatformMath.cpp`

**CPU features**:
- Uses CPUID to detect SSE4.1, SSE4.2, AVX, AVX2, AVX-512
- Dispatches to optimized code paths accordingly
- Fallback: scalar C++ implementation

**SIMD**:
- Includes `<immintrin.h>` for intrinsics
- `_mm_set_ps()`, `_mm_add_ps()`, `_mm_mul_ps()`, etc. for SSE
- `_mm256_*` for AVX
- Conditional: `#if WITH_AVX2` to guard advanced ops

**Float math**:
- `sinf()`, `cosf()`, `sqrtf()` from CRT (may auto-vectorize)
- Custom `InvSqrt()` using Newton-Raphson (rsqrt14 SSE intrinsic as fallback)

### Linux Implementation

**Location**: `Core/Private/Linux/LinuxPlatformMath.cpp`

**CPU features**:
- CPUID (x86) or `/proc/cpuinfo` (ARM) to detect SIMD support
- Runtime dispatch via function pointers or `__attribute__((target(...)))`

**SIMD**:
- GCC/Clang intrinsics (same as Windows: `immintrin.h`)
- `__m128`, `__m256` types
- Compile with `-msse4.2 -mavx2` if targeting specific extensions

**Float math**:
- `sinf()`, `cosf()`, `sqrtf()` from libm
- Custom `InvSqrt()` may use `rsqrt14` or Newton-Raphson

---

## ARCHITECTURAL PATTERNS FOR UZENGINE

### Pattern 1: Two-Tier Abstraction

**What UE5 does**:
```
IPlatformFile (interface)
    ↓
FGenericPlatformFile (base stubs)
    ↓
FWindowsPlatformFile, FLinuxPlatformFile (per-platform)
```

**How to adapt for UzEngine**:
```cpp
// rhi/rhi_backend.h
struct rhi_backend_t {
    rhi_device_t (*create_device)(...);
    void (*destroy_device)(rhi_device_t);
    // ... vtable
};

// rhi/dx12/dx12_backend.cpp
rhi_backend_t dx12_backend = {
    .create_device = dx12_create_device,
    .destroy_device = dx12_destroy_device,
    // ...
};

// rhi/vulkan/vulkan_backend.cpp
rhi_backend_t vulkan_backend = {
    .create_device = vk_create_device,
    // ...
};
```

### Pattern 2: Platform Detection via Compile-Time Macros

**What UE5 does**:
```cpp
#include COMPILED_PLATFORM_HEADER(PlatformFile.h)  // Resolves to Windows/PlatformFile.h or Linux/PlatformFile.h

// Or explicit:
#if PLATFORM_WINDOWS
    #include "Windows/WindowsPlatformFile.h"
#elif PLATFORM_LINUX
    #include "Linux/LinuxPlatformFile.h"
#endif
```

**How to adapt for UzEngine**:
```cpp
// platform/platform.h
#if defined(_WIN32)
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_LINUX 0
#elif defined(__linux__)
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_LINUX 1
#endif

// Use COMPILED_PLATFORM_HEADER macro, or simple #if checks
```

### Pattern 3: Resource Lifetime via Ref-Counting

**What UE5 does (RHI)**:
```cpp
class FRHIResource {
    FORCEINLINE uint32 AddRef() const {
        return AtomicFlags.AddRef(std::memory_order_acquire);
    }
    FORCEINLINE uint32 Release() const {
        int32 NewValue = AtomicFlags.Release(std::memory_order_release);
        if (NewValue == 0) MarkForDelete();
        return uint32(NewValue);
    }
};

// Usage
TRefCountPtr<FRHITexture> Texture = CreateTexture(...);  // AddRef on ctor
// ... use ...
Texture = nullptr;  // Release on dtor
```

**How to adapt for UzEngine**:
```cpp
// core/ref_count.h
template <typename T>
struct ref_ptr {
    T* ptr;

    ref_ptr(T* p) : ptr(p) { if (ptr) ptr->add_ref(); }
    ~ref_ptr() { if (ptr) ptr->release(); }
    ref_ptr(const ref_ptr& o) : ptr(o.ptr) { if (ptr) ptr->add_ref(); }
    ref_ptr& operator=(const ref_ptr& o) {
        if (ptr) ptr->release();
        ptr = o.ptr;
        if (ptr) ptr->add_ref();
        return *this;
    }
};

// rhi_device_t would have add_ref()/release()
```

### Pattern 4: Deferred Deletion Queue

**What UE5 does (RHI)**:
- Resources are ref-counted, but actual deletion is deferred to a safe point (end of frame)
- Prevents use-after-free when CPU/GPU access is concurrent
- Flushes automatically or on explicit `RHIFlushResources()`

**How to adapt for UzEngine**:
```cpp
// rhi/deletion_queue.h
struct deletion_queue_t {
    TArray<rhi_resource_t> pending_deletes;

    void add(rhi_resource_t res) {
        pending_deletes.push_back(res);
    }

    void flush() {
        for (auto res : pending_deletes) {
            res->backend->destroy(res);
        }
        pending_deletes.clear();
    }
};

// In frame loop:
// ... render ...
g_deletion_queue.flush();  // Safe to delete
```

### Pattern 5: Virtual Dispatch via Vtable

**What UE5 does**:
```cpp
class FDynamicRHI {
    virtual FRHIVertexShaderRef CreateVertexShader(const TArray<uint8>& Code) = 0;
    virtual FRHIPixelShaderRef CreatePixelShader(const TArray<uint8>& Code) = 0;
    // ... 100+ virtual methods
};

// Platform provides concrete impl
class FD3D12DynamicRHI : public FDynamicRHI { ... };
class FVulkanDynamicRHI : public FVulkanDynamicRHI { ... };
```

**How to adapt for UzEngine**:
```cpp
// rhi/rhi_backend.h
typedef struct {
    rhi_device_t device;
    const rhi_backend_t* backend;
} rhi_context_t;

typedef struct {
    rhi_shader_t (*create_shader)(rhi_context_t* ctx, const void* code, size_t code_len);
    void (*destroy_shader)(rhi_context_t* ctx, rhi_shader_t shader);
    // ...
} rhi_backend_t;

// Usage:
rhi_shader_t shader = ctx->backend->create_shader(ctx, code, len);
```

### Pattern 6: Display Metrics as Queried Data

**What UE5 does**:
```cpp
struct FDisplayMetrics {
    static void RebuildDisplayMetrics(FDisplayMetrics& OutMetrics);
};

// Called at startup, on resolution change, or explicitly
// Platform override handles enumeration
```

**How to adapt for UzEngine**:
```cpp
// platform/display.h
typedef struct {
    int x, y, w, h;  // Display bounds
    int dpi;
    bool is_primary;
} display_info_t;

void get_displays(display_info_t** out_displays, int* out_count);
void free_displays(display_info_t* displays);
```

### Pattern 7: Per-Platform Path Resolution

**What UE5 does**:
```cpp
// GenericPlatformFile.h
struct FPaths {
    static FString GetEngineDir();
    static FString GetGameSourceDir();
    static FString GetSavedDir();
    static FString GetUserSettingsDir();
};

// Windows impl: APPDATA\Unreal Engine\...
// Linux impl: ~/.local/share/UnrealEngine/...
```

**How to adapt for UzEngine**:
```cpp
// platform/paths.h (generic)
const char* uz_get_engine_dir();
const char* uz_get_saved_dir();
const char* uz_get_user_settings_dir();

// platform/windows/paths_win32.cpp
const char* uz_get_user_settings_dir() {
    static char path[MAX_PATH];
    char appdata[MAX_PATH];
    GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    snprintf(path, MAX_PATH, "%s\\UzEngine\\", appdata);
    return path;
}

// platform/linux/paths_linux.cpp
const char* uz_get_user_settings_dir() {
    static char path[256];
    const char* home = getenv("HOME");
    snprintf(path, 256, "%s/.local/share/uzengine/", home);
    return path;
}
```

---

## CRITICAL DESIGN DECISIONS IN UE5

### 1. **Virtual Interface First**
- Every subsystem starts with a generic/virtual base class
- Concrete implementations override specific virtual methods
- Allows runtime switching (rarely used but architecturally clean)

### 2. **Compilation Macro Dispatching**
- `COMPILED_PLATFORM_HEADER()` macro includes the right file at compile time
- No runtime overhead, completely static dispatch
- Makes it easy to reason about which code will be used

### 3. **Resource Pooling & Deferred Cleanup**
- Small objects (events, threads) are pooled to avoid allocation churn
- Large objects (GPU resources) use deferred deletion queues
- Avoids synchronization issues when CPU/GPU operate asynchronously

### 4. **No Dynamic Type Casting**
- Platform code never checks "which backend is this?" with RTTI
- Each backend is a completely separate compilation unit
- Avoids type confusion, allows compile-time elimination of unused code

### 5. **Extensive Platform Wrapping**
- Even OS-native types (HWND, HANDLE, etc.) are wrapped in platform-specific classes
- Prevents leakage of OS-specific types into generic code
- Makes it easy to add logging, debugging, lifetime tracking

### 6. **Callback/Delegate-Based Events**
- Not direct method calls on listeners
- Allows decoupled event processing (e.g., display metrics changes broadcast to all subscribers)
- Enables event deferral and batch processing

---

## FILES TO READ FOR UzEngine IMPLEMENTATION

1. **Platform abstraction structure**:
   - `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\HAL\Platform.h` (header include logic)
   - `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\GenericPlatform\GenericPlatformFile.h`
   - `W:\UE_5.6_Source\Engine\Source\Runtime\ApplicationCore\Public\GenericPlatform\GenericApplication.h`

2. **Windows implementations** (reference):
   - `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Private\Windows\WindowsPlatformFile.cpp`
   - `W:\UE_5.6_Source\Engine\Source\Runtime\ApplicationCore\Private\Windows\WindowsApplication.cpp`
   - `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Private\Windows\WindowsPlatformMemory.cpp`

3. **Linux implementations** (reference):
   - `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Private\Linux\LinuxPlatformFile.cpp`
   - `W:\UE_5.6_Source\Engine\Source\Runtime\ApplicationCore\Private\Linux\LinuxApplication.cpp`

4. **RHI abstraction** (graphics):
   - `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\DynamicRHI.h`
   - `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\RHIResources.h`
   - `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\RHICommandList.h`

---

## SUMMARY: KEY ABSTRACTION BOUNDARIES

| Layer | Generic Interface | Windows Impl | Linux Impl | Purpose |
|-------|-------------------|--------------|-----------|---------|
| **Window** | FGenericWindow | FWindowsWindow (HWND) | FLinuxWindow (SDL_Window*) | Create/manage OS windows, fullscreen, DPI |
| **Application** | GenericApplication | FWindowsApplication (Win32 loop) | FLinuxApplication (SDL loop) | Message pump, event dispatch, input devices |
| **Input** | IInputInterface | DirectInput/XInput + raw input | SDL joystick/keyboard/mouse | Keyboard, mouse, gamepad, haptics |
| **Display** | FDisplayMetrics (rebuild) | EnumDisplayMonitors() | SDL_GetNumVideoDisplays() | Monitor enumeration, DPI, safe zones |
| **File I/O** | IPlatformFile | CreateFileW() + APIs | open()/read()/write() + APIs | File open, directory ops, async I/O |
| **Paths** | FPaths (statics) | CVar lookup + API calls | Env vars + hardcoded | User dirs, temp, engine dirs |
| **Threading** | FRunnableThread + FEvent | Win32 HANDLE APIs | pthread + condition vars | Thread creation, events, mutexes |
| **Memory** | FGenericPlatformMemory | VirtualAlloc + GlobalMemoryStatusEx() | mmap() + /proc/meminfo | Allocation, stats, pressure |
| **Process** | FGenericPlatformProcess | CreateProcessW() + DLLs | fork()/execve() + dlopen() | Child processes, DLL loading, pipes |
| **Crash** | FGenericCrashContext | SEH + MiniDumpWriteDump() | Signal handlers + backtrace() | Exception capture, crash reports |
| **Misc** | FGenericPlatformMisc | GetSystemInfo() + CPUID | sysconf() + cpuinfo | CPU/GPU info, time, GUIDs, debug |
| **Atomics** | FGenericPlatformAtomics | Win32 _Interlocked* | GCC __sync_*/C11 __atomic_* | Lock-free operations |
| **Math** | FGenericPlatformMath | SSE/AVX intrinsics | GCC inline asm or intrinsics | SIMD, trigonometry |
| **RHI** | FDynamicRHI | FD3D12DynamicRHI | FVulkanDynamicRHI | GPU resource creation/rendering |

---

## ANTI-PATTERNS TO AVOID

1. **Switch on platform string**: Use compile-time `#if PLATFORM_WINDOWS` instead
2. **OS-specific types in generic code**: Wrap them (HWND → FWindowsWindow)
3. **Direct DLL/module loading in hot paths**: Use module manager with caching
4. **No error handling on OS calls**: Always check return codes, log context
5. **Polling loops instead of events**: Use event-driven architecture (message pump)
6. **Unbounded virtual method tables**: Keep vtables focused (RHI has 100+ methods, which is borderline too many)
7. **Resource deletion in destructors**: Defer to safe queue (GPU may still be using it)
8. **No DPI awareness**: Query per-monitor DPI and scale UI accordingly
9. **Hardcoded paths**: Always use path resolution API (uz_get_saved_dir())
10. **Ignoring memory pressure**: Monitor available RAM, reduce quality/free caches when low

