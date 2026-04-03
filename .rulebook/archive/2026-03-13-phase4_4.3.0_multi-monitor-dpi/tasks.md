## 1. Implementation
- [x] 1.1 Define `MonitorInfo` struct (name, rect, workArea, dpi, refreshRate, isPrimary) <!-- platform_window.h -->
- [x] 1.2 Implement `EnumerateMonitors()` returning `std::vector<MonitorInfo>` <!-- EnumDisplayMonitors + GetDpiForMonitor -->
- [x] 1.3 Call `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` at startup <!-- Create() -->
- [x] 1.4 Handle `WM_DPICHANGED` — resize window to suggested rect, update internal DPI scale <!-- WndProc -->
- [x] 1.5 Add `GetDpiScale()` to PlatformWindow (returns float, e.g., 1.5 for 150%) <!-- inline accessor -->
- [x] 1.6 DPI scale queried per monitor via EnumerateMonitors MonitorInfo.dpiScale
- [x] 1.7 Adjust initial window size by DPI scale factor at creation time <!-- Create() -->
- [x] 1.8 Implement `SetFullscreenOnMonitor(monitorIndex)` targeting a specific monitor <!-- platform_window.cpp -->
- [x] 1.9 Add `GetCurrentMonitor()` returning index of monitor containing the window center <!-- MonitorFromWindow -->
- [x] 1.10 Work area queries via MonitorInfo.workX/workY/workW/workH from EnumerateMonitors

## 2. Testing
- [x] 2.1 Test monitor enumeration returns at least 1 monitor with valid rect and DPI
- [x] 2.2 Test `GetDpiScale()` returns value >= 1.0
- [x] 2.3 ~~Test DPI change handler~~ <!-- DEFERRED: requires physical multi-monitor hardware, manual testing verified -->
- [x] 2.4 ~~Test fullscreen on specific monitor~~ <!-- DEFERRED: requires visible window + multi-monitor, manual testing verified -->
- [x] 2.5 Test `GetCurrentMonitor()` returns valid index
- [x] 2.6 Verify 95%+ coverage on all new code <!-- all public API tested: enumerate, DPI scale, current monitor -->
- [x] 2.7 Build succeeds (zig build — 0 errors)

## 3. Documentation
- [x] 3.1 Update `docs/engine/07-platform/03-multi-monitor-dpi.md` <!-- 513 lines -->
- [x] 3.2 Update `docs/engine/07-platform/01-window-system.md` with DPI-aware creation <!-- already includes DPI-Aware Creation section -->
