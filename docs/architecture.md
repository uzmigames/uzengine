# UzEngine PoC - Architecture

## Overview

UzEngine is a proof-of-concept for a 3D engine coupled to VSCode via H.264 streaming. A C++ application renders 3D content using DirectX 12 on the GPU, encodes the frames to H.264, and streams them via WebSocket. A VSCode extension receives the stream and displays it in a webview panel using Media Source Extensions (MSE).

## Pipeline

```
[DX12 Render] --> [Readback GPU->CPU] --> [NVENC/x264 Encode] --> [fMP4 Mux] --> [WebSocket Server]
                                                                                        |
                                                                              [VSCode Extension]
                                                                              [Webview + MSE Player]
```

## Component Architecture

### Renderer (src/renderer/)
- **DirectX 12** with feature level 12_0
- Double-buffered swap chain (FLIP_DISCARD, R8G8B8A8_UNORM)
- HLSL shaders compiled at runtime via D3DCompile (embedded strings)
- Root 32-bit constants for MVP matrix (no constant buffer overhead)
- Spinning triangle with per-vertex RGB color interpolation
- Resolution: 1280x720

### Encoder (src/encoder/) - Future
- IFrameEncoder strategy pattern interface
- NVENC hardware encoder (primary, dynamic loading)
- FFmpeg/libx264 software encoder (fallback)
- H.264 Baseline profile, CBR 4 Mbps, no B-frames

### Muxer (src/muxer/) - Future
- Hand-written fragmented MP4 muxer
- Init segment: ftyp + moov (with avcC)
- Media segments: moof + mdat per frame
- Annex B to AVCC NAL unit conversion

### Network (src/network/) - Future
- IXWebSocket server on port 9002
- Init segment on connect, media segments broadcast

### VSCode Extension (vscode-uzengine/) - Future
- WebviewPanel with MSE video player
- WebSocket client with reconnection

## Threading Model

```
[Render Thread] --RingBuffer--> [Encode Thread] --Queue--> [Network Thread]
     main()                      consumer                  IXWebSocket
```

## Build System

- CMake 3.20+ with Visual Studio 2022 generator
- vcpkg for dependency management
- C++20 standard

### Build Commands

```bash
# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build (Debug)
cmake --build build --config Debug

# Build (Release)
cmake --build build --config Release

# Run
./build/Debug/UzEngine.exe
```

## Key Design Decisions

1. **Root 32-bit constants** over constant buffers for the MVP matrix - simpler, no per-frame upload buffer management
2. **Upload heap** for vertex buffer - 72 bytes of static data doesn't warrant a default heap + copy
3. **Runtime shader compilation** via D3DCompile - eliminates build-time shader compilation step
4. **PeekMessage loop** - non-blocking message pump allows continuous rendering
5. **Fence-based synchronization** - proper CPU/GPU sync with double buffering
