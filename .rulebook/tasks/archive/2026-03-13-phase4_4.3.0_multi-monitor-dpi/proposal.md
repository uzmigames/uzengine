# Multi-Monitor & DPI Awareness

## Why

Modern displays use varying DPI scales (100%-300%) and multi-monitor setups are common. Without per-monitor DPI awareness, the engine renders blurry or incorrectly sized on high-DPI displays, and cannot target specific monitors for fullscreen.

## Summary

Implement per-monitor DPI awareness (V2) and multi-monitor enumeration for UzEngine's platform layer. Handles WM_DPICHANGED, provides MonitorInfo struct, and supports fullscreen on specific monitors.

## Impact

- **Platform Layer**: DPI-aware window creation, monitor enumeration
- **Renderer**: Swapchain resize on DPI change
- **UI**: DPI scale factor for UI layout

## Acceptance Criteria

1. Window creates with DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
2. EnumerateMonitors() returns all connected monitors with DPI info
3. WM_DPICHANGED resizes window correctly
4. SetFullscreenOnMonitor(index) targets specific monitor
5. GetDpiScale() returns correct per-monitor scale factor
