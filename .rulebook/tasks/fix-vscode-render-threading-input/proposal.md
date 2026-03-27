# Proposal: fix-vscode-render-threading-input

## Why

The VSCode extension viewport has three critical issues that make it unusable:

1. **Render blocks Node.js main thread**: `uz_render_frame()` is called synchronously from `setInterval(16ms)`, blocking the Extension Host event loop for 8-16ms per frame. VSCode UI freezes during rendering — tabs don't respond, language servers stall.

2. **H.264 streaming not connected**: The C++ pipeline is complete (triple-buffered GPU readback → NVENC async encoder → NAL units), but the TypeScript layer never calls `encodeFrame()` and the webview has no `VideoDecoder` or `<canvas>`. Frames render into a hidden swapchain but are never displayed in VSCode.

3. **Input forwarding broken for non-alphanumeric keys**: DOM `keyCode` values are sent as Win32 `VK_*` codes without translation. Works for A-Z/0-9 by coincidence (same numeric values) but fails for F-keys, arrow keys, numpad, and modifier combos. Camera WASD works but gizmo shortcuts, view switching, and editor hotkeys don't.

## What Changes

### Phase 1: Async render thread (decouple from Node.js event loop)
- Move `uz_render_frame()` off the Node.js main thread using a dedicated `std::thread` or `napi_create_async_work`
- Render loop runs independently at target FPS, signals frame completion via `napi_threadsafe_function`
- Node.js main thread stays responsive for VSCode UI, input, and IPC

### Phase 2: Wire H.264 → WebCodecs end-to-end
- TypeScript: poll `encodeFrame()` after render completion callback
- Send H.264 NAL units to webview via `postMessage` with `Transferable` (zero-copy)
- Webview: `VideoDecoder` (WebCodecs API) → `VideoFrame` → `<canvas>` blit
- SPS/PPS extradata sent once at stream init via `getCodecExtradata()`
- Fallback: JPEG readback + `createImageBitmap` for Electron < 100

### Phase 3: Fix input forwarding
- Add keyCode translation table: DOM `event.code` → Win32 `VK_*`
- Use `event.code` (physical key, locale-independent) instead of deprecated `event.keyCode`
- Forward modifier state (Shift, Ctrl, Alt, Meta) per-event
- Add viewport focus tracking — only forward input when viewport panel is focused
- Add mouse pointer lock for camera orbit/fly mode

### Phase 4: Frame pacing and backpressure
- Replace `setInterval(16)` with render-completion-driven scheduling
- Account for encode time: don't start new frame until encoder can accept
- Drop frames gracefully under load (encoder overload → skip readback)
- Expose FPS/frame-time stats from worker thread to Extension Host

## Impact
- Affected specs: viewport-manager, ipc-protocol
- Affected code: napi/src/napi_viewport.cpp, extension/src/panels/viewport-panel.ts, runtime/engine/app/runtime.cpp, extension webview HTML
- Breaking change: NO (same N-API surface, internal threading change)
- User benefit: Smooth 60fps viewport in VSCode without UI freezing, working keyboard shortcuts, visible rendered frames
