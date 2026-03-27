## Phase 1: Async Render Thread

- [ ] 1.1 Create `RenderThread` class in `napi/src/render_thread.h/.cpp` — dedicated `std::thread` that owns the render loop. Exposes `Start(fps)`, `Stop()`, `RequestFrame()`. Uses `napi_threadsafe_function` to signal frame completion back to Node.js.
- [ ] 1.2 Move `uz_render_frame()` call from `setInterval` callback into `RenderThread::ThreadMain()`. The thread calls `uz_render_frame()` in a loop with frame pacing (`std::this_thread::sleep_until`).
- [ ] 1.3 Update `napi_viewport.cpp` — replace synchronous `RenderFrame` N-API function with `StartRenderLoop(targetFps)` and `StopRenderLoop()`. Frame stats delivered via `ThreadSafeFunction` callback.
- [ ] 1.4 Update `viewport-panel.ts` — remove `setInterval(16ms)` render loop. Call `addon.startRenderLoop(60)` on viewport create. Handle stats via async callback.
- [ ] 1.5 Add mutex protection for shared state between render thread and N-API thread (input queue, resize requests, shutdown flag).
- [ ] 1.6 Build + verify: Node.js event loop stays responsive while rendering (measure with `process.hrtime` between ticks).

## Phase 2: H.264 → WebCodecs Streaming

- [ ] 2.1 Call `uz_encode_frame()` from `RenderThread` after each `uz_render_frame()` completes. Store encoded NAL in a lock-free queue.
- [ ] 2.2 Create `FrameDelivery` N-API function — Node.js polls encoded frames from the queue, returns `Buffer<uint8_t>` or null. Called from `ThreadSafeFunction` on-frame-complete callback.
- [ ] 2.3 Send H.264 NAL units from Extension Host to webview via `panel.webview.postMessage({ type: 'frame', data: buffer }, [buffer.buffer])` (Transferable, zero-copy).
- [ ] 2.4 Implement WebCodecs `VideoDecoder` in webview HTML: init with SPS/PPS extradata, decode each NAL chunk, draw `VideoFrame` to `<canvas>` via `drawImage()`.
- [ ] 2.5 Add `getCodecExtradata()` N-API call to retrieve SPS/PPS from encoder. Send to webview before first frame for decoder init.
- [ ] 2.6 Add JPEG fallback for environments without WebCodecs: `uz_capture_screenshot()` → base64 PNG → `<img>` in webview. Lower FPS (10-15) but universal.
- [ ] 2.7 Measure end-to-end latency: GPU render complete → frame visible on canvas. Target < 30ms at 1080p.

## Phase 3: Input Forwarding Fix

- [ ] 3.1 Create `keymap.ts` — DOM `event.code` → Win32 `VK_*` translation table. Cover: A-Z, 0-9, F1-F12, arrows, numpad, Home/End/Insert/Delete, modifiers (Shift/Ctrl/Alt/Meta), punctuation.
- [ ] 3.2 Update webview HTML input handlers: use `event.code` (physical key) instead of deprecated `event.keyCode`. Include `{ shift, ctrl, alt, meta }` modifier flags per event.
- [ ] 3.3 Update `napi_input.cpp` — extend `uz_on_key()` to accept modifier flags. Update `runtime.cpp` to forward modifiers to engine input system.
- [ ] 3.4 Add viewport focus tracking: only forward keyboard events when the viewport webview panel is focused (`document.hasFocus()` + `blur`/`focus` events).
- [ ] 3.5 Add mouse pointer lock for camera orbit: `canvas.requestPointerLock()` on right-click, release on right-click up. Forward `movementX/Y` as relative mouse delta.
- [ ] 3.6 Test: WASD camera movement, arrow keys, F-key shortcuts, gizmo hotkeys (T/R/S), mouse orbit, scroll zoom.

## Phase 4: Frame Pacing and Backpressure

- [ ] 4.1 Render thread uses high-resolution timer (`QueryPerformanceCounter`) for frame pacing instead of `sleep_for`.
- [ ] 4.2 Add backpressure: render thread checks if encoder accepted previous frame before starting GPU readback for next.
- [ ] 4.3 Expose frame stats from render thread: renderMs, encodeMs, readbackMs, droppedFrames, encoderQueueDepth. Delivered via ThreadSafeFunction.
- [ ] 4.4 Viewport panel shows streaming stats overlay: FPS, latency, encoder type (NVENC/AMF/SW), resolution.
- [ ] 4.5 Handle resize: render thread pauses, Extension Host sends new dimensions, render thread recreates swapchain + readback buffers, resumes.

## Phase 5: Testing + Validation

- [ ] 5.1 Verify Extension Host event loop responsiveness: `setImmediate` callback latency < 2ms while rendering at 60fps.
- [ ] 5.2 Verify H.264 streaming at 1080p 60fps with NVENC — no dropped frames, < 30ms end-to-end latency.
- [ ] 5.3 Verify all keyboard keys work: full keymap round-trip test (send DOM event → verify engine input state).
- [ ] 5.4 Verify mouse input: camera orbit, gizmo drag, scroll zoom all work in VSCode viewport.
- [ ] 5.5 Verify graceful degradation: JPEG fallback when WebCodecs unavailable, software encoder when no GPU encoder.
- [ ] 5.6 Memory leak test: run viewport for 10 minutes, check for growing Buffer/VideoFrame allocations.
