# Linux Platform — Spec Delta

## ADDED Requirements

### Requirement: X11 Window Backend
The engine MUST implement `IPlatformWindow` for X11, supporting window creation, resize, fullscreen, and close.

#### Scenario: X11 Window Creation
Given a Linux system with X11 display server
When `CreatePlatformWindow(desc)` is called
Then an X11 window is created with the specified size and title
And a Vulkan surface can be created via `VK_KHR_xlib_surface`

### Requirement: Wayland Window Backend
The engine MUST implement `IPlatformWindow` for Wayland, supporting window creation via `xdg_toplevel` with client-side decorations via libdecor.

#### Scenario: Wayland Window Creation
Given a Linux system with Wayland compositor and `WAYLAND_DISPLAY` set
When `CreatePlatformWindow(desc)` is called
Then the Wayland backend is selected automatically
And a `wl_surface` + `xdg_toplevel` is created with libdecor decorations

### Requirement: Display Server Auto-Detection
The platform factory MUST detect whether Wayland or X11 is available and select the appropriate backend automatically.

#### Scenario: Wayland Preferred Over X11
Given both Wayland and X11 are available
When the platform factory initializes
Then Wayland is selected (preferred)
And X11 is used as fallback if Wayland initialization fails

### Requirement: Linux File Watcher
The Linux file system MUST support file change notification via `inotify`.

#### Scenario: File Modified Notification
Given a watcher on directory `shaders/`
When `shaders/pbr.glsl` is modified
Then `OnFileChanged("shaders/pbr.glsl", Modified)` fires within 100ms

### Requirement: Linux Crash Handler
The engine MUST install signal handlers for SIGSEGV/SIGABRT/SIGFPE and produce a backtrace + crash log.

#### Scenario: Segmentation Fault
Given the crash handler is installed
When a null pointer dereference occurs
Then a crash log with backtrace is written to the crash directory
And the process terminates with a core dump (if ulimit allows)

### Requirement: Dynamic Library Loading
The Linux platform MUST support `dlopen`/`dlsym`/`dlclose` for runtime library loading, including graceful fallback when optional libraries (Wayland, Fontconfig) are unavailable.

#### Scenario: Optional Wayland Library
Given Wayland libraries are not installed
When the platform tries to `dlopen("libwayland-client.so")`
Then a null handle is returned
And the engine falls back to X11 without crashing
