# UzEngine PoC

Proof-of-concept of a 3D engine coupled to VSCode via H.264 streaming.

A C++ app renders a spinning triangle with DirectX 12, encodes each frame to H.264, and streams it as fragmented MP4 over WebSocket. A VSCode extension receives the stream and plays it in a webview using Media Source Extensions.

```
[DX12 Render] → [GPU Readback] → [H.264 Encode] → [fMP4 Mux] → [WebSocket :9002]
                                                                          ↓
                                                               [VSCode Webview MSE]
```

## Requirements

**C++ App**
- Windows 10/11 x64
- Visual Studio 2022 (MSVC v143)
- CMake 3.20+
- vcpkg (for `ixwebsocket`)
- DirectX 12 (included in Windows SDK)

**VSCode Extension**
- Node.js 20+
- VSCode 1.85+

**Optional**
- NVIDIA GPU + driver for NVENC hardware encoding (falls back to Windows MF software encoder automatically)

## Build

### C++ App

```bash
# Clone and configure (vcpkg installs ixwebsocket automatically)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release

# Run
build\Release\UzEngine.exe
```

### VSCode Extension

```bash
cd vscode-uzengine
npm install
npm run compile
```

Press **F5** inside `vscode-uzengine/` in VSCode to launch the Extension Development Host.

## Usage

1. Run `UzEngine.exe` — a window opens showing the spinning triangle with FPS counter
2. Open VSCode
3. Press `Ctrl+Shift+P` → **UzEngine: Open Stream Viewer**
4. The webview connects to `ws://localhost:9002` and starts playing the stream

To change the server URL: VSCode Settings → search `uzengine.serverUrl`.

## Project Structure

```
UzEngine/
├── CMakeLists.txt              # Build: C++20, DX12, IXWebSocket
├── vcpkg.json                  # Dependencies: ixwebsocket
├── CMakePresets.json
├── src/
│   ├── main.cpp                # Win32 entry, render loop, encode thread
│   ├── common/
│   │   ├── types.h             # FrameData, EncoderConfig, constants
│   │   ├── timer.h             # High-res timer (QueryPerformanceCounter)
│   │   └── ring_buffer.h       # Lock-free SPSC ring buffer
│   ├── renderer/
│   │   ├── renderer.h/cpp      # DX12: device, PSO, swap chain, readback
│   │   └── fps_overlay.h/cpp   # Bitmap font FPS counter (CPU, no GPU)
│   ├── encoder/
│   │   ├── encoder.h           # IFrameEncoder interface + CodecConfig
│   │   ├── nvenc_encoder.h/cpp # NVENC stub (requires NVIDIA SDK headers)
│   │   ├── ffmpeg_encoder.h/cpp# Windows MF H.264 encoder (zero deps)
│   │   └── encoder_factory.h/cpp# createEncoder(): NVENC > MF fallback
│   ├── muxer/
│   │   └── fmp4_muxer.h/cpp    # Hand-written ISO BMFF fMP4 (no deps)
│   └── network/
│       └── ws_server.h/cpp     # IXWebSocket server, port 9002
├── vscode-uzengine/            # VSCode extension (TypeScript)
│   ├── src/
│   │   ├── extension.ts        # activate/deactivate, command registration
│   │   └── stream-panel.ts     # WebviewPanel + inlined MSE player
│   ├── package.json
│   └── tsconfig.json
└── docs/
    └── architecture.md         # Detailed architecture and design decisions
```

## How It Works

### Render → Encode → Stream

1. **DX12 renders** the triangle to a swap chain back buffer
2. **GPU readback**: `CopyTextureRegion` copies pixels to a CPU-accessible buffer; fence ensures the copy is complete
3. **FPS overlay**: the counter is rasterized directly into the RGBA pixel buffer (no GPU texture)
4. **Ring buffer**: the render thread `push()`es the `FrameData`; the encode thread `pop()`s it
5. **H.264 encode**: Windows MF `CLSID_CMSH264EncoderMFT` (or NVENC if available) encodes RGBA → H.264 Annex B
6. **fMP4 mux**: Annex B NALs are converted to AVCC (length-prefixed), wrapped in `moof`+`mdat` boxes
7. **WebSocket broadcast**: each media segment is sent as a binary frame to all connected clients
8. **MSE playback**: the webview appends segments to a `SourceBuffer` (`sequence` mode); the `<video>` plays them

### Encoder Strategy

```
createEncoder()
  └── NvEncEncoder::isAvailable()?  → tries LoadLibrary("nvEncodeAPI64.dll")
        yes → NvEncEncoder::initialize() → false (SDK headers not present)
      FFmpegEncoder::isAvailable()?  → CoCreateInstance(CLSID_CMSH264EncoderMFT)
        yes → FFmpegEncoder::initialize() → true  ✓
```

### NVENC Support

To enable full NVENC hardware encoding:
1. Download [NVIDIA Video Codec SDK](https://developer.nvidia.com/video-codec-sdk)
2. Place headers in `external/nvidia-video-codec-sdk/Interface/`
3. Implement the stubbed bodies in `src/encoder/nvenc_encoder.cpp`

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| `uzengine.serverUrl` | `ws://localhost:9002` | WebSocket server URL |

## Architecture

See [docs/architecture.md](docs/architecture.md) for detailed component descriptions, threading model, fMP4 box layout, and design decisions.
