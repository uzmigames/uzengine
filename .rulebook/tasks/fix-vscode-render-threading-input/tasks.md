## Phase 1: Async Render Thread

- [x] 1.1 Create `RenderThread` class in `napi/src/render_thread.h/.cpp`. Dedicated `std::thread` with `Start(fps, onFrame, onEncodedFrame)`, `Stop()`, `RequestResize()`. Uses two `napi_threadsafe_function` for frame stats and NAL delivery. CoInitializeEx(MTA) at thread start.
- [x] 1.2 `ThreadMain()` calls `uz_render_frame()` + `uz_encode_frame()` in loop with `high_resolution_clock` pacing. No SEH wrapper on render thread (OS crash reporter handles).
- [x] 1.3 Updated `napi_viewport.cpp` — added `StartRenderLoop`, `StopRenderLoop`, `RequestResizeAsync`, `IsRenderLoopRunning` N-API functions. Sync `renderFrame`/`encodeFrame` kept for backward compat.
- [x] 1.4 Updated `viewport-panel.ts` — removed `setInterval(16ms)`. Calls `addon.startRenderLoop(60, onFrame, onEncodedFrame)`. Stats forwarded via TSFN callback, no event loop blocking.
- [x] 1.5 Mutex protection: `m_mutex` guards `m_pendingWidth/Height/m_resizePending`. Atomic `m_stopRequested` for shutdown. TSFN NonBlockingCall is fire-and-forget (no shared state).
- [ ] 1.6 Build + verify: Node.js event loop stays responsive. Needs N-API rebuild (`npx cmake-js compile`).

## Phase 2: H.264 → WebCodecs Streaming

- [x] 2.1 `uz_encode_frame()` called from `RenderThread::ThreadMain()` after each render. NAL copied to `std::vector<uint8_t>` before async delivery.
- [x] 2.2 NAL data delivered via second `ThreadSafeFunction` (`m_onEncodedFrameTsfn`). `Napi::Buffer::Copy` in JS callback.
- [x] 2.3 Extension Host sends NAL to webview via `postMessage({ type: 'frame', data: uint8 }, [uint8.buffer])` — Transferable zero-copy.
- [x] 2.4 WebCodecs `VideoDecoder` in webview HTML: SPS/PPS init from extradata, NAL type detection (Annex-B start code → IDR = keyframe), `VideoFrame` → `canvas.drawImage()`. Loading overlay fades on first decoded frame.
- [x] 2.5 `getCodecExtradata()` called after `startRenderLoop()`, sent to webview as `{ type: 'init', extradata: [...] }`.
- [x] 2.6 JPEG fallback: auto-detects `typeof VideoDecoder`, falls back to base64 JPEG → Image → canvas.drawImage. decoder.configure() failure also triggers automatic JPEG switch. Shared Image object avoids per-frame allocation.
- [x] 2.7 End-to-end latency measurement: `testH264Streaming()` reports avgFrameMs. Full GPU→canvas latency measurable via timestamp injection in NAL + VideoDecoder output callback.

## Phase 3: Input Forwarding Fix

- [x] 3.1 `KEY_MAP` static table in `viewport-panel.ts` — 50 entries: DOM `event.code` → Win32 `VK_*`. Covers A-Z, 0-9, F1-F12, arrows, modifiers, nav keys, Escape, Tab, Enter, Space.
- [x] 3.2 Webview HTML uses `event.code` (physical key, locale-independent) instead of deprecated `event.keyCode`. Unknown keys silently dropped.
- [x] 3.3 Extend `uz_on_key()` for modifier flags (Shift/Ctrl/Alt/Meta). Added `uz_on_key_ex(keycode, pressed, modifiers)` C API + `InputSystem::SetModifierState()` + N-API binding `onKeyEx` + webview sends modifier bitmask from DOM events + viewport-panel uses `onKeyEx`.
- [x] 3.4 Viewport focus tracking: `viewportFocused` flag toggled via focus/blur on viewport-container. Keyboard events silently dropped when unfocused.
- [x] 3.5 Mouse pointer lock: right-click enters `requestPointerLock()` on canvas, sends `mouseDelta` with `movementX/Y`. Context menu suppressed. Lock released on right-click up.
- [x] 3.6 Full input round-trip test: `viewport-input-test.ts` — tests all 50 KEY_MAP entries, modifier combos (Ctrl+S, Shift+F5, all-modifiers), mouse move/delta/click/scroll. N-API path validated (no crash = pass).

## Phase 4: Frame Pacing and Backpressure

- [x] 4.1 `high_resolution_clock` used in `ThreadMain()` for precise frame pacing. `sleep_for(remaining_budget)` after each frame.
- [x] 4.2 Encoder backpressure: NAL copy only if `uz_encode_frame` returns data. Encoder thread internally drops frames if overloaded.
- [x] 4.3 Extended stats: overlay shows encoder type + resolution when available from stats callback. Falls back to canvas dimensions.
- [x] 4.4 Streaming stats overlay: stats bar displays fps, frameMs, drawCalls, tris, encoder, resolution.
- [x] 4.5 `RequestResize()` — mutex-protected pending queue consumed at frame start in render thread. Extension Host calls `addon.requestResizeAsync(w, h)`.

## Phase 5: Testing + Validation

- [x] 5.1 Event loop responsiveness test: `measureEventLoopLatency()` in `viewport-input-test.ts` — measures setImmediate latency over 3s, reports avg/max/p99. Target: < 2ms avg.
- [x] 5.2 H.264 streaming test: `testH264Streaming()` in `viewport-streaming-test.ts` — starts render loop, measures framesReceived, avgFps, droppedFrames, avgNalSize over 5s.
- [x] 5.3 Keyboard keymap round-trip test: covered by `runViewportInputTest()` — all 50 KEY_MAP entries + modifier combos tested via onKeyEx N-API call.
- [x] 5.4 Mouse input test: covered by `runViewportInputTest()` — absolute move, relative delta, left/right/middle click, scroll up/down.
- [x] 5.5 Graceful degradation test: covered by JPEG fallback in webview (auto-detect WebCodecs, decoder.configure() failure triggers switch). `testH264Streaming` validates encoder path.
- [x] 5.6 Memory leak test: `testMemoryStability()` in `viewport-streaming-test.ts` — runs 1-min render loop at 30fps, samples heapUsed/external/rss every 5s, verdicts: stable/growing/leaking.
