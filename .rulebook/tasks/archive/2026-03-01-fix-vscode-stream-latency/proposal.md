# Proposal: fix-vscode-stream-latency

## Why

The H.264 stream displayed in the VSCode panel accumulates latency progressively compared to the native engine window. Confirmed via direct side-by-side comparison: the engine runs smoothly at 60fps in the Win32 window, but the VSCode webview shows growing drift. Root causes: no live-edge sync in the MSE player, a 1-second IDR interval in the encoder, and an unbounded pending segment queue on the JS side.

## What Changes

### 1. VSCode Webview — `stream-panel.ts`
- **Live-edge seek**: 500ms interval checks if `video.buffered.end(last) - video.currentTime > 200ms`. If so, seeks to `buffered.end(last) - 50ms`. Eliminates accumulated drift.
- **Pending segment queue cap**: keep at most 3 segments. When full, drop oldest and retain the newest (highest chance of being an IDR). Prevents progressive backlog.
- **Real FPS via `requestVideoFrameCallback`**: measure frames actually decoded by the video element instead of WebSocket packets received. Reflects true playback smoothness.

### 2. Engine C++ — `main.cpp`
- **IDR interval**: reduce from 60 to 30 frames (0.5s instead of 1s). Cuts maximum recovery time after any MSE stall or reconnect.

## Impact
- Affected specs: `stream`, `encoder`
- Affected code: `vscode-uzengine/src/stream-panel.ts`, `src/main.cpp`
- Breaking change: NO
- User benefit: VSCode stream with minimal and stable latency, no progressive drift. Fast recovery after reconnect.
