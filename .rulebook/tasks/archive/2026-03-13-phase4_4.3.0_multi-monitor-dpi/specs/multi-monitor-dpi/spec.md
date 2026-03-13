# Multi-Monitor & DPI — Spec Delta

## ADDED Requirements

### Requirement: Per-Monitor DPI Awareness
The engine MUST declare `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` at process startup so Windows delivers correct DPI values per monitor.

#### Scenario: High-DPI Window Creation
Given a 4K monitor at 150% scaling
When a window is created with logical size 1280x720
Then the physical window size is 1920x1080 (1280*1.5, 720*1.5)
And `GetDpiScale()` returns 1.5

### Requirement: DPI Change Handling
The window MUST handle `WM_DPICHANGED` when dragged between monitors with different DPI scales.

#### Scenario: Window Moved to Different DPI Monitor
Given a window on a 100% DPI monitor
When the user drags it to a 150% DPI monitor
Then `WM_DPICHANGED` fires
And the window resizes to maintain logical size
And `GetDpiScale()` returns the new scale factor

### Requirement: Monitor Enumeration
The platform layer MUST enumerate all connected monitors with complete info (rect, DPI, refresh rate).

#### Scenario: Multi-Monitor Query
Given a system with 2 monitors
When `EnumerateMonitors()` is called
Then a vector of 2 `MonitorInfo` structs is returned
And exactly one has `isPrimary = true`
And each has valid `rect`, `dpi > 0`, and `refreshRate > 0`
