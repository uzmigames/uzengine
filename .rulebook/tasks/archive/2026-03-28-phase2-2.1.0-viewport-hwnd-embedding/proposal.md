# Viewport Frame Streaming — H.264 DX12→WebView Canvas Inside VSCode

## Purpose

The 3D viewport must render inside the VSCode editor tab as a native webview canvas. The runtime renders DX12 frames, encodes them as H.264 via Media Foundation hardware encoder, streams the bitstream to the webview via N-API typed arrays, and the webview decodes with WebCodecs VideoDecoder and draws to a canvas at 120 FPS. This is the architecture validated by POC 0.2 (see `docs/engine/29-design-decisions/02-poc-lessons.md`) with <5ms total latency and 120 FPS sustained at 1280x720.

Currently the SceneEditorProvider creates a standalone floating DX12 window outside VSCode. The viewport content must appear INSIDE the VSCode tab where the `.uzscene` was opened.

## POC 0.2 Validated Architecture

```
Runtime (.node addon, in-process)
  ├─ DX12 render to offscreen RT (hidden HWND)
  ├─ GPU readback to CPU (triple-buffered)
  ├─ H.264 encode via Media Foundation (HW accelerated, 2-4ms)
  └─ N-API typed array (zero-copy Uint8Array)
       │
       ▼
VSCode Webview (Canvas)
  ├─ WebCodecs VideoDecoder (HW accelerated, 1-2ms)
  ├─ VideoFrame → Canvas drawImage
  └─ 120 FPS @ 854x480, 80-100 FPS @ 1080p
```

### POC 0.2 Benchmarks (from docs/engine/29-design-decisions/02-poc-lessons.md)

| Operation | Latency |
|-----------|---------|
| N-API function call | <1 µs |
| H.264 frame encode (GPU) | 2-4 ms |
| WebCodecs decode | 1-2 ms |
| Canvas render | <1 ms |
| **Total latency** | **3-7 ms** |

| Resolution | Bitrate | FPS |
|------------|---------|-----|
| 854x480 | 5 Mbps | 120 FPS |
| 1280x720 | 8 Mbps | 120 FPS |
| 1920x1080 | 15 Mbps | 80-100 FPS |

## What Changes

### Phase A: C++ H.264 Encoder (`runtime/engine/ipc/h264_encoder.h/.cpp`)
- `H264Encoder` class using Media Foundation `IMFTransform` (CMSH264EncoderMFT)
- Baseline profile, low-latency mode, CBR 20 Mbps, keyframe every 15 frames
- Input: DX12 render target (RGBA) → NV12 conversion → H.264 bitstream
- Triple-buffered readback (encode while GPU renders next frame)
- Dedicated encoder thread with work queue
- JPEG fallback for GPUs without H.264 HW encoder

### Phase B: N-API Frame Streaming (`napi/src/napi_viewport.cpp`)
- Add `uz_encode_frame(uint8_t** out_data, uint32_t* out_size) -> int` to C API
  - Reads current back buffer, encodes H.264, returns NAL units
- Add N-API `encodeFrame(): Buffer | null` — returns H.264 frame data as Node Buffer
- Render loop changes: `renderFrame()` renders + encodes, returns stats + frame buffer

### Phase C: WebView Canvas Decoder (`extension/src/panels/scene-editor-provider.ts`)
- WebView HTML contains a `<canvas>` element (fills the editor tab)
- WebCodecs `VideoDecoder` configured: `avc1.42001e` (Baseline), HW-accelerated, `optimizeForLatency: true`
- Extension posts H.264 `Uint8Array` frames to webview via `postMessage`
- WebView creates `EncodedVideoChunk` (key/delta), decodes to `VideoFrame`, draws to canvas
- Toolbar overlay (gizmo buttons, stats) rendered as HTML on top of canvas

### Phase D: Input Forwarding
- Canvas captures mouse/keyboard/scroll events
- Posts to extension via `postMessage`
- Extension forwards to runtime via `addon.onMouseMove/onMouseButton/onKey/onScroll`
- Already implemented in current SceneEditorProvider

### Phase E: Lifecycle & Adaptive Quality
- Panel hidden → stop render+encode loop
- Panel revealed → resume, request keyframe
- Panel resized → `addon.resizeViewport(w, h)`, reinit encoder at new resolution
- Adaptive bitrate: reduce during high motion, increase on static scenes
- Error recovery: if decoder errors, reset and request keyframe

## Impact

- **Replaces**: Standalone floating window approach, "Rendering in native viewport window" placeholder
- **Dependencies**: Media Foundation (Windows), WebCodecs API (Chromium/Electron)
- **User benefit**: 3D scene renders inside VSCode tab at 120 FPS with <5ms latency
- **Platform**: Windows (MF encoder). macOS fallback: VideoToolbox. Linux: software JPEG.
