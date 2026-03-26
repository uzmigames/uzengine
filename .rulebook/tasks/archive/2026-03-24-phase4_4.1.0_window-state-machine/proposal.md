# Proposal: Window State Machine (Phase 4.1.0)

## Why
The current `PlatformWindow` only tracks open/closed and resize. Real applications need minimize, maximize, restore, borderless fullscreen, focus tracking, and state persistence. Without these, the engine cannot handle Alt+Tab, taskbar clicks, or save/restore window layout between sessions. Both UE5 (`FGenericWindow::Minimize/Maximize/Restore`) and Godot (`DisplayServer` WindowData with 30+ state fields) implement full window state machines. This is P1 — every demo and the future editor needs it.

## What Changes

### New Systems
1. **WindowState enum** — `Normal`, `Minimized`, `Maximized`, `FullscreenBorderless`, `FullscreenExclusive`
2. **Window state machine** — WM_SYSCOMMAND/WM_SIZE dispatch to state transitions with callbacks
3. **Borderless fullscreen toggle** — `SetFullscreen(monitor)` / `SetWindowed()` with style swap + position restore
4. **Focus tracking** — `WM_ACTIVATEAPP` / `WM_SETFOCUS` / `WM_KILLFOCUS` with `IsFocused()` query
5. **Minimum size enforcement** — `WM_GETMINMAXINFO` handler with configurable min width/height
6. **Timer-throttled resize** — rate-limit `WM_SIZE` events during live drag (like Godot)
7. **State persistence** — save/load window rect + state to config on close/open

### Refactoring
- `PlatformWindow::WndProc` extended with new message handlers
- `WindowDesc` extended with `minWidth`, `minHeight`, `startState`, `rememberPosition`

## Impact
- Affected specs: `docs/engine/07-platform/01-window-system.md`
- Affected code: `runtime/platform/windows/platform_window.h/.cpp`, all demos
- Breaking change: NO — additive API, existing code continues to work
- User benefit: Proper Alt+Tab, minimize/maximize, fullscreen toggle, window position remembered
- Enables: Multi-monitor support (4.3.0), editor window management
