# Platform Abstraction Interface — Spec Delta

## ADDED Requirements

### Requirement: IPlatformWindow Interface
The engine MUST define an `IPlatformWindow` pure virtual interface that abstracts all window operations. No OS-specific types (HWND, Display*, NSWindow*) SHALL appear in the interface signature.

#### Scenario: Window Creation Through Interface
Given a platform factory is available
When `CreatePlatformWindow(desc)` is called
Then a platform-specific `IPlatformWindow` implementation is returned
And the window is created with the specified dimensions and title

#### Scenario: Native Handle Access
Given a window was created via `IPlatformWindow`
When `GetNativeHandle()` is called
Then an opaque `NativeWindowHandle` is returned
And the handle can be passed to RHI swapchain creation without casting to OS types

### Requirement: IPlatformInput Interface
The engine MUST define an `IPlatformInput` pure virtual interface that abstracts keyboard, mouse, and gamepad queries.

#### Scenario: Input State Through Interface
Given an `IPlatformInput` instance exists
When `IsKeyDown(KeyCode::W)` is called
Then the current key state is returned
And the key code enum is platform-agnostic (not VK_* or X11 keysyms)

#### Scenario: Action Mapping Through Interface
Given an `IPlatformInput` with registered actions
When `IsActionJustPressed("jump")` is called
Then the action state is resolved through the platform-agnostic action map

### Requirement: Platform Factory Dispatch
The engine MUST provide factory functions that return the correct platform implementation based on compile-time defines.

#### Scenario: Windows Platform Selection
Given `UZ_PLATFORM_WINDOWS` is defined at compile time
When `CreatePlatformWindow()` is called
Then a `WindowsWindow` instance is returned as `IPlatformWindow*`

#### Scenario: Linux Platform Selection
Given `UZ_PLATFORM_LINUX` is defined at compile time
When `CreatePlatformWindow()` is called
Then a `LinuxWindow` instance is returned as `IPlatformWindow*`

### Requirement: No OS Type Leakage
Engine code above the platform layer MUST NEVER include OS headers (`<windows.h>`, `<X11/Xlib.h>`) or use OS-specific types. The `NativeWindowHandle` opaque type SHALL be the only way to pass native handles across the boundary.

#### Scenario: RHI Receives Opaque Handle
Given a renderer needs a window surface
When it calls `window->GetNativeHandle()`
Then the returned `NativeWindowHandle` is passed to `rhi->CreateSwapChain(handle)`
And the RHI backend internally casts to the OS type within its own translation unit

### Requirement: IPlatformFile Interface
The engine MUST define an `IPlatformFile` pure virtual interface for file operations including read, write, exists, enumerate, memory-map, and file change notification.

#### Scenario: File Read Through Interface
Given an `IPlatformFile` instance
When `Read("shaders/pbr.hlsl")` is called
Then the file contents are returned as a byte buffer
And the path resolution is platform-specific (forward slashes normalized)

### Requirement: IPlatformProcess Interface
The engine MUST define an `IPlatformProcess` pure virtual interface for DLL loading, process creation, and environment variable access.

#### Scenario: DLL Loading Through Interface
Given an `IPlatformProcess` instance
When `LoadDll("plugin.dll")` is called on Windows
Then a `NativeDllHandle` is returned
And symbols can be resolved via `GetSymbol(handle, "init")`

### Requirement: IPlatformMisc Interface
The engine MUST define an `IPlatformMisc` pure virtual interface for system queries (CPU info, OS version, locale, GUID generation, high-resolution timestamps).

#### Scenario: CPU Info Query
Given an `IPlatformMisc` instance
When `GetCpuInfo()` is called
Then a struct with core count, feature flags (SSE4.2, AVX2), and cache sizes is returned
