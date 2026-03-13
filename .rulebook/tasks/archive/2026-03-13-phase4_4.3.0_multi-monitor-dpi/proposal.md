# Proposal: Multi-Monitor & DPI (Phase 4.3.0)

## Why
Modern setups have multiple monitors at different DPI scales (e.g., 4K primary at 150%, 1080p secondary at 100%). Without per-monitor DPI awareness, the engine renders blurry on high-DPI or incorrectly sized on mixed-DPI setups. UE5 uses `GetDPIScaleFactorAtPoint()` + per-monitor awareness; Godot uses `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`. The editor (VSCode-embedded) will frequently span monitors — this is critical for correct UI/viewport sizing.

## What Changes

### New Systems
1. **MonitorInfo struct** — name, rect, work area, DPI, refresh rate, primary flag
2. **Monitor enumeration** — `EnumDisplayMonitors` + `GetMonitorInfoW` + `GetDpiForMonitor`
3. **Per-monitor DPI awareness V2** — `SetProcessDpiAwarenessContext` at startup
4. **DPI change handling** — `WM_DPICHANGED` handler with automatic window resize
5. **DPI scale queries** — `GetDpiScale()` on window, `GetMonitorDpiScale(index)`
6. **Fullscreen on specific monitor** — `SetFullscreen(monitorIndex)` targets any monitor

### Refactoring
- Window creation accounts for DPI when calculating initial size
- UI/font systems (future) receive DPI scale factor for correct rendering

## Impact
- Affected specs: `docs/engine/07-platform/01-window-system.md`
- Affected code: `runtime/platform/windows/platform_window.h/.cpp`
- Breaking change: NO — additive
- User benefit: Crisp rendering on 4K, correct sizing on mixed-DPI multi-monitor
- Enables: Editor multi-monitor support, correct UI scaling
