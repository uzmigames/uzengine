# Architecture Overview — POC Lessons

> **Status**: Reference (historical — informing current architecture)
> **Source**: architecture.md §Lessons from POC 0.1, §Lessons from POC 0.2

## POC 0.1 — WebSocket JPEG Streaming

**Architecture:** Separate C++ process rendering DX12, streaming JPEG frames via WebSocket to VSCode extension webview.

### What Worked

- Native C++ + DirectX rendering + RPC streaming to extension: **fundamentally viable**
- 120 FPS achieved in webview via `createImageBitmap` + single-slot decode pattern
- Proved the concept: a game engine can live inside VSCode

### What Failed

1. **Webview FPS bottleneck** — runtime rendered at 244 FPS but webview couldn't keep up. Streaming JPEG frames through webview postMessage is inherently limited by serialization overhead.
2. **Monolithic architecture** — no clear layer separation. Everything mixed together.
3. **Shader complexity** — getting shaders right was extremely hard with SM5. Migrating to SM6.6 baseline.
4. **Cold-start latency** — opening a scene took several seconds (DX12 device init + asset loading). Led to the Runtime Pool design.
5. **Build lock problem** — recompiling while pool processes were running caused EBUSY/link errors because the binary was locked.

### Key Decisions From POC 0.1

| Problem | Decision |
|---------|----------|
| Webview FPS bottleneck | Move to N-API in-process + native HWND viewport |
| Monolithic code | Restart with well-defined layers and documentation |
| Shader complexity | SM6.6 as baseline (easier shaders, downgrade to SM5 later) |
| Cold-start | Pre-warmed runtime pool |
| Build lock | `--pool-shutdown` before compile |

## POC 0.2 — N-API Stress Test

**Architecture:** DX12 renderer compiled as `.node` N-API addon, loaded in-process by Electron/VSCode.

### What Worked

- **N-API addon inside Electron/VSCode is viable** — DX12 renderer works in-process
- **H.264 via Media Foundation + WebCodecs = 120 FPS** at 854x480 (for secondary panels)
- In-process model eliminates all IPC overhead for the primary viewport

### Frame Delivery Evolution (benchmarks at 854x480)

| Method | Encode | Decode | Payload | FPS | Verdict |
|--------|--------|--------|---------|-----|---------|
| Raw RGBA via postMessage | 0ms | 0ms | ~1.6MB | 2-7 | postMessage serialization kills it |
| JPEG (stb_image_write) | 2.5ms | ~2ms | ~23KB | 138 | Good, but per-frame compression wastes bandwidth |
| WebP (libwebp) | **42.8ms** | ~3ms | ~58KB | 24 | CPU-only VP8 encoding, way too slow |
| **H.264 (Media Foundation)** | **2.2ms** | **3.3ms** | ~54KB | **120** | HW-accelerated encode + decode |
| **WebRTC (local RTCPeerConnection)** | ~2ms | ~3ms | ~54KB | **120** | **Winner** — eliminates postMessage, native video pipeline end-to-end |

### Critical N-API Findings

| Finding | Impact |
|---------|--------|
| **MUST** include `${CMAKE_JS_SRC}` in CMakeLists | Delay-load hook for Electron — without it, `require()` segfaults |
| **MUST** use `NAPI_DISABLE_CPP_EXCEPTIONS` | `NAPI_CPP_EXCEPTIONS` crashes Electron (electron#22799) |
| **MUST** compile with `--runtime=electron --runtime-version=<ver>` | Match Electron's Node ABI exactly |
| `CoInitializeEx(COINIT_MULTITHREADED)` required | Before any Media Foundation calls |
| `#include <strmif.h>` needed | For `ICodecAPI` (Media Foundation encoder config) |

### H.264 + WebRTC Streaming Architecture

For webview preview panels (not the main viewport):

**C++ side (encoding):**
- Media Foundation H.264 encoder (hardware-accelerated via GPU ASIC)
- Baseline profile, low-latency mode, no B-frames, CBR 20Mbps
- GOP = 15 frames (keyframe every ~250ms)
- RGBA→NV12 conversion + encode on dedicated thread (triple-buffered)

**Transport (WebRTC):**
- Local `RTCPeerConnection` between N-API addon and webview
- Eliminates `postMessage` serialization overhead entirely
- Native video pipeline: encoded frames flow through WebRTC's media channel
- No network involved — connection is local (loopback), no STUN/TURN needed
- Stabilized frame delivery — WebRTC handles timing, buffering, and frame drops automatically

**Webview side (decoding):**
- WebRTC `MediaStream` received via `ontrack` event
- Frames rendered directly via `<video>` element or canvas `drawImage()`
- No manual `VideoDecoder` management — WebRTC handles decode internally
- Jitter buffer smooths out any frame timing inconsistencies

### Why WebRTC Won Over Raw WebCodecs

The H.264 + WebCodecs approach (manual `VideoDecoder`) worked at 120 FPS but had stability issues:
- Manual frame pacing — had to implement single-slot pattern to avoid decode queue buildup
- No built-in flow control — if the webview fell behind, frames accumulated in postMessage
- `postMessage` still involved serialization overhead for the encoded `Uint8Array`

WebRTC solved all of these by providing a complete media pipeline:
- Built-in jitter buffer handles timing
- Built-in flow control via congestion feedback (even locally)
- Zero `postMessage` — frames flow through the native media channel
- The `<video>` element uses the compositor's native video layer (no JavaScript in the render path)

### Key Insight

Video codecs exploit temporal redundancy between frames. For typical editor use (mostly static scenes with occasional movement), H.264 delta frames are tiny compared to per-frame JPEG. Even in worst case (maximum motion), H.264 matches JPEG speed at better quality. WebRTC adds the transport stability that raw WebCodecs lacked.

### Final Conclusion

- N-API in-process model is the correct architecture
- Main viewport: native HWND with direct DX12 swapchain (no encoding)
- Preview/remote panels: H.264 Media Foundation + WebRTC local peer connection
- WebRTC stabilized what H.264+WebCodecs started — same encoding, better transport
- The crash isolation tradeoff (addon crash kills extension host) is acceptable — Electron restarts cleanly, and the runtime has SEH handlers + minidump generation
