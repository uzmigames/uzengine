# Proposal: Platform Abstraction Interface (Phase 4.11.0)

## Why
UzEngine currently has Windows-only platform code (`PlatformWindow`, `InputSystem`) with Win32 types (HWND, VK codes) leaking into engine-level code. This blocks Linux/macOS support and violates Dependency Inversion — high-level systems depend on concrete Win32 implementations instead of abstractions. Every AAA engine (UE5's `FGenericPlatform*`, Godot's `DisplayServer`) solves this with a platform abstraction layer. This task establishes the vtable interfaces that ALL other platform tasks (4.1–4.10) implement against.

## What Changes

### New Systems
1. **IPlatformWindow** — vtable interface for window creation, state queries, message pumping. Platform-agnostic; no HWND/Display* above this layer.
2. **IPlatformInput** — vtable interface for keyboard, mouse, gamepad state queries and action mapping.
3. **IPlatformFile** — vtable interface for file I/O, directory enumeration, file watching.
4. **IPlatformProcess** — vtable interface for DLL loading, process creation, environment variables.
5. **IPlatformMisc** — vtable interface for CPU/GPU info, GUIDs, timestamps, OS version, locale.
6. **PlatformFactory** — `CreatePlatform*()` free functions returning correct implementation per compile-time platform.
7. **PlatformTypes.h** — forward declarations and opaque handle types (`NativeWindowHandle`) wrapping OS types.

### Refactoring
- Existing `PlatformWindow` becomes `WindowsWindow : IPlatformWindow`
- Existing `InputSystem` becomes `WindowsInput : IPlatformInput`
- All demo and engine code updated to use interfaces
- `platform_window.h` HWND accessor replaced with `GetNativeHandle() -> NativeWindowHandle`

## Impact
- Affected specs: `docs/engine/07-platform/01-window-system.md`, `docs/engine/07-platform/02-input-system.md`
- Affected code: `runtime/platform/windows/`, all demos, `runtime/tools/uzengine.cpp`
- Breaking change: YES — code using `PlatformWindow` directly must switch to `IPlatformWindow*`
- User benefit: Engine code becomes platform-agnostic; Linux/macOS backends plug in without changing game code
- Enables: All platform tasks 4.1–4.10, Linux support, macOS support
