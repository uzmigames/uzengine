# UzEngine — Streaming Pipeline Guide

## Overview

This document details how a frame travels from the DX12 GPU, through encoding, and into the VSCode webview display. Each frame is self-contained; there are no init segments or container overhead.

---

## Frame Lifecycle

```
┌─────────────────────────────────────────────────────────────────────┐
│  RENDER THREAD (main)                                               │
├─────────────────────────────────────────────────────────────────────┤
│  1. DX12Renderer::render()                                          │
│     ├─ Render pyramid to RT[frameIndex_]                           │
│     ├─ performReadback() → copy RT to readback buffer              │
│     └─ Extract RGBA pixels into FrameData struct                   │
│                                                                     │
│  2. Frame-rate limiter (target 60 fps)                             │
│     └─ sleep + spin-wait for 16.67 ms per frame                   │
│                                                                     │
│  3. Push frame to g_frameRing (8-slot ring buffer)                 │
│     └─ Drop oldest if full (ring never blocks)                     │
└──────────────────┬──────────────────────────────────────────────────┘
                   │
                   │ lock-free SPSC ring buffer
                   ↓
┌─────────────────────────────────────────────────────────────────────┐
│  ENCODE THREAD                                                      │
├─────────────────────────────────────────────────────────────────────┤
│  1. Pop frame from g_frameRing                                      │
│     └─ sleep 1ms if empty (no busy-wait)                           │
│                                                                     │
│  2. JpegEncoder::encode()                                           │
│     ├─ libjpeg-turbo tjCompress2()                                 │
│     ├─ Input:  rgba pixels, 1280×720, TJPF_RGBA, TJSAMP_420       │
│     ├─ Quality: 85                                                 │
│     └─ Output: JPEG blob (~40–80 KB)                               │
│                                                                     │
│  3. WsServer::broadcast()                                           │
│     └─ Send JPEG blob to all connected WebSocket clients           │
└──────────────────┬──────────────────────────────────────────────────┘
                   │
                   │ binary WebSocket frames
                   ↓
┌─────────────────────────────────────────────────────────────────────┐
│  IXWEBSOCKET SERVER THREADS                                         │
├─────────────────────────────────────────────────────────────────────┤
│  1. Per-connection message dispatch                                 │
│  2. Send binary frame to each client via WebSocket.sendBinary()    │
└──────────────────┬──────────────────────────────────────────────────┘
                   │
                   │ network (TCP over WS)
                   ↓
┌─────────────────────────────────────────────────────────────────────┐
│  VSCODE WEBVIEW CLIENT                                              │
├─────────────────────────────────────────────────────────────────────┤
│  1. WebSocket message event (ArrayBuffer)                           │
│  2. Convert to base64: btoa(String.fromCharCode(...bytes))         │
│  3. Create data URL: "data:image/jpeg;base64," + base64            │
│  4. Set <img src="data:image/jpeg;base64,...">                     │
│  5. Browser decodes JPEG, renders to canvas                        │
│  6. Update FPS counter (frames received per second)                │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Component Details

### 1. DX12 Renderer (src/renderer/)

**Render target creation:**
```c++
// Double-buffered render targets, R8G8B8A8_UNORM
// One is actively rendered, the other is being read back by CPU
ID3D12Resource* renderTargets_[2];
```

**Per-frame render and readback:**
```c++
void DX12Renderer::render() {
    // Render to swapchain RT and readback buffer
    recordRenderCommands();
    commandQueue_->ExecuteCommandLists(...);
    waitForGpu();

    // Copy RT to readback buffer
    performReadback();

    // Extract RGBA pixels
    const FrameData frame = captureFrameFromReadback();
}
```

**Pixel format:** `DXGI_FORMAT_R8G8B8A8_UNORM` (4 bytes per pixel)
- Direct mapping to `TJPF_RGBA` for libjpeg-turbo
- Row-major (standard layout)
- Readback buffer uses 256-byte aligned row pitch for GPU efficiency

### 2. Ring Buffer (src/common/ring_buffer.h)

**Lock-free SPSC (Single Producer, Single Consumer):**
```c++
template<typename T, size_t N>  // N must be power-of-2
class RingBuffer {
    std::array<T, N> slots_;
    alignas(64) std::atomic<size_t> head_;  // write by producer
    alignas(64) std::atomic<size_t> tail_;  // write by consumer
};
```

**Push policy (render thread):**
```c++
void push(T value) {
    size_t head = head_.load(relaxed);
    size_t tail = tail_.load(acquire);
    size_t next = (head + 1) & kMask;

    if (next == tail) {
        // Full: advance tail to drop oldest unread entry
        tail_.store((tail + 1) & kMask, release);
    }

    slots_[head] = std::move(value);
    head_.store(next, release);
}
```

**Pop (encode thread):**
```c++
std::optional<T> pop() {
    size_t tail = tail_.load(relaxed);
    size_t head = head_.load(acquire);

    if (tail == head) return std::nullopt;  // empty

    T value = std::move(slots_[tail]);
    tail_.store((tail + 1) & kMask, release);
    return value;
}
```

**Cache-line alignment (`alignas(64)`)**: Prevents false sharing between head_ and tail_ (on separate CPU cache lines).

**Capacity:** Default 8 slots (2^3). Absorbs render/encode jitter without blocking.

### 3. JPEG Encoder (src/encoder/jpeg_encoder.cpp)

**libjpeg-turbo TurboJPEG API:**
```c++
bool JpegEncoder::encode(const uint8_t* rgba, int width, int height,
                        int quality, std::vector<uint8_t>& out) {
    tjhandle handle = tjInitCompress();

    unsigned char* jpegBuf = nullptr;
    unsigned long jpegSize = 0;

    int ret = tjCompress2(
        handle,
        rgba,                    // RGBA input
        width,
        width * 4,               // pitch (bytes per row)
        height,
        TJPF_RGBA,              // pixel format
        &jpegBuf,
        &jpegSize,
        TJSAMP_420,             // chroma subsampling
        quality,                 // typically 85
        TJFLAG_FASTDCT          // fast DCT
    );

    out.assign(jpegBuf, jpegBuf + jpegSize);
    tjFree(jpegBuf);
    tjDestroy(handle);
    return (ret == 0);
}
```

**Encoding parameters:**
- **Input format**: `TJPF_RGBA` — 4 bytes per pixel (R, G, B, A in that order)
- **Chroma subsampling**: `TJSAMP_420` — 4:2:0 (Y full, Cb/Cr quarter)
  - Good quality-to-size balance
  - Larger filesizes use `TJSAMP_444` (no subsampling) or `TJSAMP_422`
- **Quality**: 85 (1–100 scale)
  - 85 gives ~40–80 KB per frame at 1280×720
  - Lower quality → smaller files, more artifacts
  - Higher quality → larger files, more bandwidth
- **Fast DCT**: `TJFLAG_FASTDCT` for speed over precision

**Output:**
- Raw JPEG binary (no headers, no wrapper)
- Approximately 3–6 KB per megapixel at quality 85, 4:2:0

**Not thread-safe:** Create one `JpegEncoder` per encoding thread.

### 4. WebSocket Server (src/network/ws_server.cpp)

**IXWebSocket library setup:**
```c++
impl_->server = std::make_unique<ix::WebSocketServer>(9002, "0.0.0.0");

impl_->server->setOnClientMessageCallback([this](...) {
    if (msg->type == ix::WebSocketMessageType::Open) {
        // Register client
        {
            std::lock_guard<std::mutex> lock(impl_->clientsMutex);
            impl_->clients.insert(socket_ptr);
        }
        // Note: no init segment sent (not needed for JPEG)
    }
});

impl_->server->start();
```

**Broadcasting (called from encode thread):**
```c++
void WsServer::broadcast(const std::vector<uint8_t>& data) {
    ix::IXWebSocketSendData sendData(
        reinterpret_cast<const char*>(data.data()),
        data.size());

    std::lock_guard<std::mutex> lock(impl_->clientsMutex);
    for (auto& client : impl_->clients) {
        client->sendBinary(sendData);
    }
}
```

**Client tracking:**
- Connected clients stored in `std::set<std::shared_ptr<ix::WebSocket>>`
- Mutex-protected for thread-safe broadcast
- IXWebSocket handles accept loop and message framing internally

### 5. VSCode Webview (vscode-uzengine/src/stream-panel.ts)

**WebSocket connection and JPEG decode loop:**
```javascript
var ws = new WebSocket(SERVER_URL);
ws.binaryType = 'arraybuffer';

ws.addEventListener('message', function(e) {
    if (!(e.data instanceof ArrayBuffer)) return;

    // Convert ArrayBuffer to base64
    var bytes = new Uint8Array(e.data);
    var binary = '';
    for (var i = 0; i < bytes.byteLength; i++) {
        binary += String.fromCharCode(bytes[i]);
    }
    var dataUrl = 'data:image/jpeg;base64,' + btoa(binary);

    // Display
    document.getElementById('stream').src = dataUrl;

    // Update FPS counter
    frameCount++;
    if (now - lastFpsTick >= 1000) {
        fpsDisplay.textContent = frameCount + ' fps';
        frameCount = 0;
    }
});
```

**Why data URLs?**
- VSCode webviews have CSP `sandbox` restrictions
- MSE (Media Source Extensions) requires unrestricted blob: URLs
- Data URLs bypass these restrictions
- Simple: set `<img src="...">` and browser handles decode

**Content Security Policy (CSP):**
```
default-src 'none'
connect-src ws://localhost:*
img-src blob: data: vscode-webview:
script-src 'nonce-...'
```

**Reconnection with backoff:**
```javascript
var reconnectDelay = 1000;
const MAX_RECONNECT = 16000;

function scheduleReconnect() {
    reconnectDelay = Math.min(reconnectDelay * 2, MAX_RECONNECT);
    setTimeout(connect, reconnectDelay);
}
```

---

## Performance Characteristics

### Bandwidth

At 1280×720, quality 85, 4:2:0:
- Per frame: ~40–80 KB (JPEG-dependent on content)
- At 30 fps: ~1.2–2.4 Mbps
- Easily handled over local 1 Gbps Ethernet

### Latency

- **GPU render + readback**: ~3–5 ms
- **JPEG encode**: ~5–15 ms (depends on image complexity, CPU)
- **Network (local)**: <1 ms
- **WebSocket + browser decode**: ~2–5 ms
- **Total**: ~15–30 ms end-to-end (typical 60–66 fps display)

### Frame rate targets

- **Render thread**: 60 fps target (16.67 ms per frame, frame-rate limited)
- **Encode thread**: whatever frames are available (~30–40 fps typical)
- **Display**: ~30 fps observed in VSCode webview (JPEG decoding + DOM update)

The ring buffer absorbs render/encode timing differences. When encoding falls behind, older frames are dropped (drop-oldest policy).

---

## Data Flow Summary

| Stage | Input | Process | Output |
|-------|-------|---------|--------|
| Render | Scene state | DX12 render + GPU readback | FrameData (RGBA) |
| Ring buffer | FrameData | SPSC queue | FrameData (dequeued) |
| Encode | FrameData | libjpeg-turbo tjCompress2() | JPEG binary blob |
| Network | JPEG blob | WebSocket.sendBinary() | TCP/IP frames |
| Client decode | TCP frames | reassemble + ArrayBuffer + base64 | data:image/jpeg;base64,... |
| Display | data URL | browser img element | rendered frame on screen |

---

## IPC and Control

The pipeline is controlled via file-based IPC (see `docs/ipc-mcp.md`). The render loop reads scene parameters (rotation speed, background color, pause state) every frame from `g_scene`:

```c++
renderer.setSceneParams(&g_scene);  // pointer to shared struct

// In render loop:
if (!g_scene.paused.load()) {
    rotAngleY_ += g_scene.rotY.load() * deltaTime;
    rotAngleX_ += g_scene.rotX.load() * deltaTime;
    // ... update MVP matrix
}

// Background color
{
    std::lock_guard<std::mutex> lk(g_scene.bgMutex);
    clearColor = { g_scene.bgR, g_scene.bgG, g_scene.bgB, 1.0f };
}
```

These values are updated by the IPC poller when external commands are received.
