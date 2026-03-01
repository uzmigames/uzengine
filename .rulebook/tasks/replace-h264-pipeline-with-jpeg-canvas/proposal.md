# Proposal: replace-h264-pipeline-with-jpeg-canvas

## Why

The current H.264/fMP4/MSE streaming pipeline has critical reliability problems in the VSCode
webview environment. MSE autoplay is blocked in hidden or backgrounded webviews, causing a black
screen every time the panel is opened or revealed. SourceBuffer drift accumulates over time
requiring periodic resets. Codec negotiation (NVENC vs. Media Foundation fallback) adds startup
complexity and failure modes. The fMP4 muxer requires a correctly formed init segment before any
frames can be decoded, adding a fragile two-phase startup. These problems collectively make the
stream unreliable as a developer tool.

JPEG-over-WebSocket replaces this fragile pipeline with the simplest possible approach: every
WebSocket binary message is one complete JPEG frame. The client calls `createImageBitmap()` on the
raw bytes and paints it to a `<canvas>` element. There is no codec state, no buffer management, no
init segment, no autoplay policy, and no MSE Surface — just images drawn as fast as they arrive.

## What Changes

### REMOVED
- `src/encoder/encoder.h` — abstract encoder interface
- `src/encoder/encoder_factory.h/.cpp` — factory that selected NVENC vs. MF
- `src/encoder/nvenc_encoder.h/.cpp` — NVENC H.264 hardware encoder
- `src/encoder/ffmpeg_encoder.h/.cpp` — FFmpeg/x264 software fallback encoder
- `src/muxer/fmp4_muxer.h/.cpp` — hand-written fragmented MP4 muxer
- All MSE / SourceBuffer / `<video>` code in `vscode-uzengine/src/stream-panel.ts`
- `ws_server.setInitSegment()` call and associated init-segment broadcast logic

### ADDED
- `src/encoder/jpeg_encoder.h/.cpp` — libjpeg-turbo turbojpeg API wrapper; takes RGBA pixels,
  produces a JPEG blob at configurable quality (default 85)
- libjpeg-turbo vcpkg dependency in `vcpkg.json`
- `<canvas>` rendering path in `stream-panel.ts` using `createImageBitmap()` and `drawImage()`
- Client-side FPS counter derived from frame-arrival timestamps

### MODIFIED
- `vcpkg.json` — add `libjpeg-turbo` dependency
- `CMakeLists.txt` — `find_package(libjpeg-turbo)`, link `libjpeg-turbo::turbojpeg-static`,
  remove H.264 encoder and muxer source files from compile list
- `src/main.cpp` — encode thread simplified: readback RGBA → `JpegEncoder::encode()` →
  `WsServer::broadcast(blob)`, remove encoder factory / muxer construction and lifecycle
- `vscode-uzengine/src/stream-panel.ts` — full replacement of `<video>`+MSE with `<canvas>`+
  `createImageBitmap()`; remove all SourceBuffer, MediaSource, live-edge sync, and stall detection

### UNCHANGED
- `src/network/ws_server.h/.cpp`
- `src/renderer/` (all renderer files)
- `src/common/ring_buffer.h`
- `src/common/ipc_*.h/.cpp`

## Impact

- Affected specs: `specs/streaming/spec.md`
- Affected code: `src/encoder/`, `src/muxer/`, `src/main.cpp`, `vcpkg.json`, `CMakeLists.txt`,
  `vscode-uzengine/src/stream-panel.ts`
- Breaking change: YES — existing `<video>`-based stream viewer will stop working; canvas viewer
  replaces it entirely
- User benefit: Stream opens instantly with no black screen, no autoplay policy interference, no
  buffer drift, and dramatically simpler error surface; JPEG quality 85 delivers acceptable visual
  fidelity at ~50-80 KB per 1280x720 frame
