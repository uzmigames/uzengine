# Frame Streaming — H.264 Media Foundation & WebCodecs

> **Status**: Planned (Phase 1.5–2)
> **Source**: `runtime/ipc/h264_encoder.h`, `extension/ipc/h264_decoder.ts`
> **References**: Media Foundation API, WebCodecs API, H.264 baseline profile, NAL units

## Purpose

The 3D viewport renders directly to a native HWND, not into the VSCode webview (which would require frame streaming). However, 2D editor panels (inspector, hierarchy, asset browser) need to display rendered content (gizmo overlays, node previews). Frame streaming encodes rendered frames as H.264 video and streams them to the webview at 120 FPS with < 5ms latency.

Evolution of approaches in POC 0.2:
- Raw RGBA copy: 2 FPS (bandwidth-limited)
- JPEG software encode (stb): 138 FPS (low quality)
- WebP software encode (libwebp): 24 FPS (codec slow)
- **H.264 hardware encode (Media Foundation): 120 FPS** (optimal)

## Architecture

```
┌────────────────────────────────┐
│  C++ Runtime (D3D12)           │
│  Render 3D frame to texture    │
│  (color RT @ internal resolution)
└────────┬───────────────────────┘
         │
         ▼
┌────────────────────────────────┐
│  H.264 Encoder Thread          │
│  ┌──────────────────────────────┤
│  │ 1. ReadbackBuffer GPU→CPU    │
│  │ 2. Media Foundation Encoder  │
│  │ 3. H.264 bitstream output    │
│  │ 4. Send frame via IPC        │
│  └──────────────────────────────┤
└────────┬───────────────────────┘
         │ H.264 elementary stream
         │ (NAL units, keyframes every 15 frames)
         │
         ▼
┌────────────────────────────────┐
│  N-API IPC Channel             │
│  Send Uint8Array frame buffer  │
│  (zero-copy via typed array)   │
└────────┬───────────────────────┘
         │
         ▼
┌────────────────────────────────┐
│  TypeScript Extension          │
│  Receive H.264 frame data      │
│  Queue for WebCodecs decoder   │
└────────┬───────────────────────┘
         │
         ▼
┌────────────────────────────────┐
│  WebCodecs VideoDecoder        │
│  ┌──────────────────────────────┤
│  │ 1. Create EncodedVideoChunk  │
│  │ 2. Decode to VideoFrame      │
│  │ 3. Draw to canvas            │
│  │ 4. Close frame               │
│  └──────────────────────────────┤
└────────┬───────────────────────┘
         │
         ▼
    Canvas Display (120 FPS)
```

## Media Foundation H.264 Encoder

### Initialization

```cpp
/// H.264 encoder using Media Foundation (hardware-accelerated on Windows).
class H264Encoder {
public:
    /// Initialize encoder.
    /// Requirements:
    /// - CoInitializeEx(COINIT_MULTITHREADED) must be called on this thread
    /// - Encoder will run on dedicated thread
    bool Initialize(
        ID3D12Device* device,
        uint32_t width,
        uint32_t height,
        uint32_t fps = 60,
        uint32_t bitrate_kbps = 20000  // 20 Mbps (adaptive to frame rate)
    );

    /// Encode one frame (async).
    /// GPU texture → H.264 NAL units → callback with encoded data.
    /// Returns immediately; callback fired on encoder thread when frame ready.
    bool EncodeFrame(
        ID3D12Resource* color_texture,
        std::function<void(const uint8_t* data, size_t size)> on_encoded
    );

    /// Shutdown encoder and flush pending frames.
    void Shutdown();

    /// Get recommended bitrate for target FPS (to control quality/latency tradeoff).
    static uint32_t GetRecommendedBitrate(uint32_t width, uint32_t height, uint32_t fps);

private:
    IMFTransform* encoder_ = nullptr;           // H.264 encoder transform
    IMFMediaType* input_type_ = nullptr;
    IMFMediaType* output_type_ = nullptr;

    // Thread pool for encoding (runs on worker thread)
    std::thread encoder_thread_;
    std::queue<std::pair<ID3D12Resource*, FrameCallback>> work_queue_;
    std::mutex work_queue_lock_;
    std::condition_variable work_available_;
    bool should_shutdown_ = false;

    using FrameCallback = std::function<void(const uint8_t* data, size_t size)>;

    void EncoderThreadMain();
};
```

### Implementation Details

```cpp
bool H264Encoder::Initialize(
    ID3D12Device* device,
    uint32_t width,
    uint32_t height,
    uint32_t fps,
    uint32_t bitrate_kbps
) {
    // Initialize COM (required for Media Foundation)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        UZ_LOG(IPC, Error, "CoInitializeEx failed: 0x{:X}", hr);
        return false;
    }

    // Initialize Media Foundation
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        UZ_LOG(IPC, Error, "MFStartup failed: 0x{:X}", hr);
        return false;
    }

    // Create H.264 encoder transform
    hr = CoCreateInstance(
        CLSID_CMSH264EncoderMFT,  // H.264 encoder (hardware if available)
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IMFTransform,
        (void**)&encoder_
    );
    if (FAILED(hr)) {
        UZ_LOG(IPC, Error, "Failed to create H.264 encoder: 0x{:X}", hr);
        return false;
    }

    // Configure input media type (NV12, our internal format)
    hr = MFCreateMediaType(&input_type_);
    input_type_->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    input_type_->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    input_type_->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    input_type_->SetUINT64(MF_MT_FRAME_SIZE, Pack2UINT32AsUINT64(width, height));
    input_type_->SetUINT64(MF_MT_FRAME_RATE, Pack2UINT32AsUINT64(fps, 1));
    input_type_->SetUINT32(MF_MT_PIXEL_ASPECT_RATIO, Pack2UINT32AsUINT32(1, 1));
    hr = encoder_->SetInputType(0, input_type_, 0);
    if (FAILED(hr)) {
        UZ_LOG(IPC, Error, "SetInputType failed: 0x{:X}", hr);
        return false;
    }

    // Configure output media type (H.264)
    hr = MFCreateMediaType(&output_type_);
    output_type_->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    output_type_->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    output_type_->SetUINT64(MF_MT_FRAME_SIZE, Pack2UINT32AsUINT64(width, height));
    output_type_->SetUINT64(MF_MT_FRAME_RATE, Pack2UINT32AsUINT64(fps, 1));

    // Encoder settings
    ICodecAPI* codec_api = nullptr;
    hr = encoder_->QueryInterface(IID_ICodecAPI, (void**)&codec_api);
    if (SUCCEEDED(hr)) {
        // Baseline profile (most compatible)
        VARIANT var;
        var.vt = VT_I4;
        var.intVal = eAVEncH264VProfile_Base;
        codec_api->SetValue(&CODECAPI_AVEncH264Profile, &var);

        // Low latency mode
        var.intVal = 1;
        codec_api->SetValue(&CODECAPI_AVEncCommonLowLatency, &var);

        // Quality/bitrate
        var.intVal = bitrate_kbps * 1000;  // Convert to bits/sec
        codec_api->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &var);

        // Force keyframe interval (every 15 frames ≈ 250ms at 60 fps)
        var.intVal = 15;
        codec_api->SetValue(&CODECAPI_AVEncCommonKeyFrameInterval, &var);

        codec_api->Release();
    }

    hr = encoder_->SetOutputType(0, output_type_, 0);
    if (FAILED(hr)) {
        UZ_LOG(IPC, Error, "SetOutputType failed: 0x{:X}", hr);
        return false;
    }

    // Start encoder
    hr = encoder_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    hr = encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    hr = encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    // Start encoder thread
    encoder_thread_ = std::thread(&H264Encoder::EncoderThreadMain, this);

    UZ_LOG(IPC, Info, "H.264 encoder initialized: {}x{} @ {} fps, {} kbps",
        width, height, fps, bitrate_kbps);
    return true;
}

void H264Encoder::EncoderThreadMain() {
    while (true) {
        std::unique_lock<std::mutex> lock(work_queue_lock_);
        work_available_.wait(lock, [this] { return !work_queue_.empty() || should_shutdown_; });

        if (should_shutdown_ && work_queue_.empty()) break;

        if (work_queue_.empty()) continue;

        auto [texture, callback] = work_queue_.front();
        work_queue_.pop();
        lock.unlock();

        // Convert texture to NV12 and encode
        // (Simplified; real implementation would use D3D12 copy + format conversion)

        IMFSample* input_sample = nullptr;
        // Create sample from NV12 data
        encoder_->ProcessInput(0, input_sample, 0);
        input_sample->Release();

        // Get encoded output
        while (true) {
            IMFSample* output_sample = nullptr;
            MFT_OUTPUT_DATA_BUFFER output_buffer;
            DWORD status = 0;

            HRESULT hr = encoder_->ProcessOutput(0, 1, &output_buffer, &status);
            if (FAILED(hr)) break;

            if (output_sample) {
                // Extract H.264 data
                IMFMediaBuffer* buffer = nullptr;
                output_sample->GetBufferByIndex(0, &buffer);
                uint8_t* data = nullptr;
                DWORD length = 0;
                buffer->Lock(&data, nullptr, &length);

                callback(data, length);

                buffer->Unlock();
                buffer->Release();
                output_sample->Release();
            }
        }
    }
}

bool H264Encoder::EncodeFrame(
    ID3D12Resource* color_texture,
    std::function<void(const uint8_t* data, size_t size)> on_encoded
) {
    {
        std::lock_guard<std::mutex> lock(work_queue_lock_);
        work_queue_.push({ color_texture, on_encoded });
    }
    work_available_.notify_one();
    return true;
}

void H264Encoder::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(work_queue_lock_);
        should_shutdown_ = true;
    }
    work_available_.notify_one();

    if (encoder_thread_.joinable()) {
        encoder_thread_.join();
    }

    if (encoder_) encoder_->Release();
    if (input_type_) input_type_->Release();
    if (output_type_) output_type_->Release();

    MFShutdown();
    CoUninitialize();
}
```

### Bitrate Selection

Intelligent bitrate to balance quality, latency, and bandwidth:

```cpp
uint32_t H264Encoder::GetRecommendedBitrate(uint32_t width, uint32_t height, uint32_t fps) {
    uint32_t pixels = width * height;
    uint32_t base_bitrate = (pixels * fps) / 2000;  // Heuristic

    // Adjust for typical resolutions
    if (pixels <= 480 * 360) {
        return std::max(500u, base_bitrate);          // Small: 500 kbps–2 Mbps
    } else if (pixels <= 854 * 480) {
        return std::max(2000u, base_bitrate);         // HD: 2–5 Mbps
    } else if (pixels <= 1920 * 1080) {
        return std::max(5000u, base_bitrate);         // FHD: 5–15 Mbps
    } else {
        return std::max(15000u, base_bitrate);        // 4K: 15–50 Mbps
    }
}
```

## WebCodecs H.264 Decoder

### TypeScript Implementation

```typescript
/// Decode H.264 frames on web side using WebCodecs API.
export class H264StreamDecoder {
    private decoder?: VideoDecoder;
    private canvas: HTMLCanvasElement;
    private canvasCtx: CanvasRenderingContext2D;

    constructor(canvas: HTMLCanvasElement) {
        this.canvas = canvas;
        this.canvasCtx = canvas.getContext("2d")!;
    }

    /// Initialize decoder for given resolution.
    async initialize(width: number, height: number): Promise<void> {
        this.canvas.width = width;
        this.canvas.height = height;

        // Check VideoDecoder support
        const support = await VideoDecoder.isConfigSupported({
            codec: "avc1.42001e",        // Baseline profile
            codedWidth: width,
            codedHeight: height,
            hardwareAcceleration: "prefer-hardware"
        });

        if (!support.supported) {
            throw new Error("H.264 decoding not supported on this browser");
        }

        this.decoder = new VideoDecoder({
            output: (frame) => this.renderFrame(frame),
            error: (err) => console.error("VideoDecoder error:", err)
        });

        const config: VideoDecoderConfig = {
            codec: "avc1.42001e",  // Baseline profile, level 3.0
            codedWidth: width,
            codedHeight: height,
            hardwareAcceleration: "prefer-hardware",
            optimizeForLatency: true
        };

        await this.decoder.configure(config);
    }

    /// Feed H.264 frame data from runtime.
    async receiveFrame(data: Uint8Array): Promise<void> {
        if (!this.decoder) return;

        try {
            // Detect keyframe vs delta frame
            const isKeyframe = this.isH264Keyframe(data);

            const chunk = new EncodedVideoChunk({
                type: isKeyframe ? "key" : "delta",
                timestamp: performance.now() * 1000,  // Microseconds
                data: data
            });

            // Queue frame for decoding (async)
            this.decoder.decode(chunk);
        } catch (e) {
            console.error("Failed to decode frame:", e);
        }
    }

    /// Render decoded frame to canvas.
    private renderFrame(frame: VideoFrame): void {
        try {
            // Draw frame to canvas
            this.canvasCtx.drawImage(frame, 0, 0);

            // Measure decode latency
            const latency = performance.now() - (frame.timestamp / 1000);
            if (latency > 5) {
                console.warn(`High decode latency: ${latency.toFixed(2)}ms`);
            }
        } finally {
            // Important: close frame to free GPU resources
            frame.close();
        }
    }

    /// Detect H.264 keyframe (IDR frame).
    /// H.264 NAL unit header:
    ///   Byte 0: [forbidden_bit(1) | nal_ref_idc(2) | nal_unit_type(5)]
    ///   NAL type 5 = IDR (keyframe)
    ///   NAL type 1 = non-IDR (delta frame)
    private isH264Keyframe(data: Uint8Array): boolean {
        if (data.length < 1) return false;
        const nal_unit_type = data[0] & 0x1F;
        return nal_unit_type === 5;  // IDR frame
    }

    /// Shutdown decoder.
    async shutdown(): Promise<void> {
        if (this.decoder) {
            await this.decoder.flush();
            this.decoder.close();
        }
    }
}
```

## Triple-Buffer Architecture

Encode and display in parallel to maintain 120 FPS:

```cpp
/// Triple-buffer for frame streaming (encode while GPU renders next frame).
class FrameStreamingPipeline {
public:
    void Initialize(ID3D12Device* device, uint32_t width, uint32_t height) {
        encoder_.Initialize(device, width, height);

        // Create 3 readback buffers (triple-buffer)
        for (int i = 0; i < 3; ++i) {
            auto buffer = device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Tex2D(
                    DXGI_FORMAT_NV12,  // H.264 expects NV12
                    width, height
                ),
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr
            );
            readback_buffers_[i] = buffer;
        }
    }

    void RenderFrame(ID3D12GraphicsCommandList* cmd_list, ID3D12Resource* color_rt) {
        // Rotate through buffers (0 → 1 → 2 → 0)
        int buffer_index = frame_count_ % 3;
        frame_count_++;

        // Copy render target to readback buffer
        cmd_list->CopyResource(readback_buffers_[buffer_index], color_rt);

        // Queue encode (on separate thread)
        encoder_.EncodeFrame(
            readback_buffers_[buffer_index],
            [this](const uint8_t* data, size_t size) {
                SendFrameToExtension(data, size);
            }
        );
    }

    void SendFrameToExtension(const uint8_t* data, size_t size) {
        // Create Uint8Array and send via IPC
        // (napi_create_arraybuffer, then send)
    }

private:
    H264Encoder encoder_;
    ID3D12Resource* readback_buffers_[3] = { nullptr };
    uint64_t frame_count_ = 0;
};
```

## Latency Optimization

Key techniques to maintain < 5ms latency:

```cpp
// 1. Use lower bitrate during high motion (faster encoding)
uint32_t adaptive_bitrate = motion_variance > 0.5f ? 10000 : 20000;

// 2. Reduce resolution for lower-powered GPUs
uint32_t scale_factor = gpu_power >= 300 ? 1 : 2;  // Scale 2x if weak GPU
uint32_t encode_width = display_width / scale_factor;

// 3. Force keyframes every 15 frames (for quick recovery from packet loss)
// Already set in SetOutputType() via CODECAPI_AVEncCommonKeyFrameInterval

// 4. Measure and log frame times
auto start = std::chrono::high_resolution_clock::now();
encoder_.EncodeFrame(...);
auto elapsed = std::chrono::high_resolution_clock::now() - start;
if (elapsed > 5ms) {
    UZ_LOG(IPC, Warning, "Slow encode: {:.2f}ms", elapsed.count());
}
```

## Error Recovery

Detect and recover from streaming failures:

```typescript
/// Auto-restart decoder if stream corrupted.
class RobustH264Decoder {
    async receiveFrame(data: Uint8Array): Promise<void> {
        try {
            await this.decoder.receiveFrame(data);
        } catch (e) {
            console.error("Decode error, resetting decoder:", e);

            // Reset decoder and request keyframe from encoder
            await this.decoder.shutdown();
            await this.decoder.initialize(this.width, this.height);

            // Signal to C++ to send keyframe
            ipc.call("ipc.request_keyframe", []);
        }
    }
}

// C++ side: on request_keyframe, force encoder to emit IDR frame next
bool H264Encoder::RequestKeyframe() {
    IMFSample* keyframe_cmd = nullptr;
    MFCreateSample(&keyframe_cmd);
    encoder_->ProcessInput(0, keyframe_cmd, MFT_INPUT_DATA_BUFFER_FLAG_SYNCPOINT);
    keyframe_cmd->Release();
    return true;
}
```

## Comparison

| Approach | Speed | Quality | Latency | Notes |
|---|---|---|---|---|
| Raw RGBA | 2 FPS | Perfect | <1ms | Bandwidth-limited (~8 Gbps for 1920x1080) |
| JPEG (stb) | 138 FPS | Visible artifacts | ~7ms | Loss compression, decoding overhead |
| WebP (libwebp) | 24 FPS | Good | ~10ms | Slower codec, still software |
| **H.264 (FFmpeg)** | **120 FPS** | **Good** | **<5ms** | **Hardware accelerated (NVENC/AMF/QSV)** |

## Implementation Status

| Phase | Status | What Ships |
|-------|--------|-----------|
| **Phase 1** | **DONE** (v0.2.52) | FFmpeg libavcodec H.264 encoder, HW probe (NVENC/AMF/QSV/MF/x264), WebCodecs VideoDecoder, synchronous frame streaming, PNG fallback, crash protection (SEH + VEH), resize handling |
| **Phase 1.5** | Pending | Triple-buffer readback, async encoder thread, motion-adaptive bitrate, latency benchmarks |
| **Phase 2** | Pending | Selective region encoding, frame pacing, VP9 fallback |

### Phase 1 Implementation Notes (v0.2.52)

- **Encoder**: FFmpeg libavcodec (not Media Foundation directly). Probes HW encoders: `h264_nvenc` > `h264_amf` > `h264_qsv` > `h264_mf` > `libx264`.
- **Color conversion**: libswscale RGBA-to-NV12 (SWS_BILINEAR).
- **Readback**: Single `ScreenshotCapture::MapReadback` + `WaitIdle` (synchronous). Sufficient for 720p@120FPS, needs triple-buffer for 1080p.
- **Transport**: N-API `encodeFrame()` returns Buffer of H.264 NAL units. Extension posts to webview via `postMessage`.
- **Decoder**: WebCodecs `VideoDecoder` with `avc1.42001e` baseline, HW-accelerated, `optimizeForLatency: true`.
- **Fallback**: PNG via `captureScreenshot()` when encoder unavailable or WebCodecs unsupported.
- **Crash protection**: VEH crash reports + SEH wrappers on all N-API calls.
- **C API**: `uz_encode_frame`, `uz_request_keyframe`, `uz_get_codec_extradata`, `uz_set_encode_bitrate`.
- **Task tracking**: `.rulebook/tasks/phase2-2.1.0-viewport-hwnd-embedding/tasks.md`
