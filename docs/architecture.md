# UzEngine PoC - Architecture

## Overview

UzEngine is a proof-of-concept for a 3D engine coupled to VSCode via H.264 streaming. A C++ application renders 3D content using DirectX 12 on the GPU, encodes the frames to H.264, and streams them via WebSocket as fragmented MP4. A VSCode extension receives the stream and displays it in a webview panel using Media Source Extensions (MSE).

## Pipeline

```
[DX12 Render] --> [Readback GPU->CPU] --> [NVENC/MF Encode] --> [fMP4 Mux] --> [WebSocket Server]
     |                    |                                                              |
  Triangle           FPS Overlay                                               [VSCode Extension]
  1280x720           (bitmap font,                                             [Webview + MSE Player]
  30 fps             CPU blit)
```

## Threading Model

```
Main Thread (Render)          Encode Thread                  IXWebSocket Threads
─────────────────────         ─────────────────────          ─────────────────────
Win32 message loop            pop() from ring buffer         accept connections
DX12 render + readback   →    H.264 encode (MF/NVENC)   →   send init segment
push() to RingBuffer<8>       fMP4 mux (moof+mdat)           broadcast segments
                              WsServer::broadcast()
```

- **RingBuffer\<FrameData, 8\>**: lock-free SPSC, drop-oldest policy (never blocks render thread)
- **Encode thread** sleeps 1 ms when ring is empty; no busy-wait
- **IXWebSocket** manages its own I/O threads; `broadcast()` is mutex-protected

## Component Details

### Renderer (`src/renderer/`)

| Item | Detail |
|------|--------|
| API | DirectX 12, feature level 12_0 |
| Swap chain | Double-buffered, FLIP_DISCARD, R8G8B8A8_UNORM |
| Shaders | HLSL embedded as `constexpr` strings, compiled at runtime via `D3DCompile` |
| MVP | Root 32-bit constants (16 floats at b0) — no constant buffer allocation needed |
| Geometry | 3-vertex triangle, per-vertex RGB color, uploaded via upload heap |
| Readback | `D3D12_HEAP_TYPE_READBACK` buffer (256-byte aligned row pitch), double-buffered, fence-synced |
| FPS Overlay | 5×7 bitmap font, 2× scaled, green on dark background, CPU-composited into readback pixels |

### Encoder (`src/encoder/`)

**Interface:** `IFrameEncoder` (strategy pattern)

```
initialize(EncoderConfig) → bool
encode(rgba, w, h, outNALUs) → bool
getCodecConfig() → CodecConfig { profileIdc, levelIdc, sps, pps }
shutdown()
```

**Factory (`createEncoder`):** tries NVENC first, falls back to Windows MF.

| Backend | Class | Status |
|---------|-------|--------|
| NVENC | `NvEncEncoder` | Stub — detects `nvEncodeAPI64.dll`, returns `false` from `initialize()` pending SDK headers |
| Windows MF | `FFmpegEncoder` | Full — `CLSID_CMSH264EncoderMFT`, ARGB32→H.264 Baseline CBR, `ICodecAPI` low-latency mode |

> **Note:** The `FFmpegEncoder` name is historical — the actual backend is Windows Media Foundation's built-in H.264 MFT, which requires no external dependencies on Windows 10+.

**CodecConfig** carries raw SPS/PPS bytes (without start codes) extracted from the first IDR frame. These are consumed by the muxer to build the `avcC` box.

### Muxer (`src/muxer/`)

Hand-written ISO 14496-12 Fragmented MP4 muxer. Zero external dependencies.

**Init segment** (sent once per client on WebSocket connect):
```
ftyp (32 bytes)  — brand: isom, compatible: isom/iso2/avc1/mp41
moov
  mvhd           — timescale 90000
  trak
    tkhd
    mdia
      mdhd       — timescale 90000
      hdlr       — vide / VideoHandler
      minf
        vmhd
        dinf/dref (self-contained)
        stbl
          stsd → avc1 → avcC (SPS+PPS)
          stts/stsc/stsz/stco (empty — fragments handle timing)
  mvex → trex    — default_sample_duration = 3000 ticks (90000/30fps)
```

**Media segment** (one per encoded frame):
```
moof
  mfhd           — sequence_number (incrementing)
  traf
    tfhd         — flags: default-base-is-moof
    tfdt         — version=1, 64-bit base_media_decode_time
    trun         — flags=0x305: data-offset + first-sample-flags + duration + size
mdat             — AVCC-format NAL units (4-byte big-endian length prefix)
```

**Annex B → AVCC conversion:** start codes (`00 00 01` / `00 00 00 01`) stripped, replaced with 4-byte big-endian length prefix. SPS/PPS NALs filtered out (they live in `avcC`).

### Network (`src/network/`)

- **Library:** IXWebSocket (vcpkg)
- **Port:** 9002 (default, configurable)
- **Protocol:** binary WebSocket frames
  - First message to each client: fMP4 init segment
  - Subsequent messages: fMP4 media segments (moof+mdat)
- **Thread safety:** `std::mutex` on client set; `broadcast()` safe to call from encode thread

### Ring Buffer (`src/common/ring_buffer.h`)

Lock-free SPSC ring buffer, capacity `N` (power-of-2, default 8 slots).
- **Producer** (render thread): `push()` — overwrites oldest entry when full
- **Consumer** (encode thread): `pop()` — returns `std::optional<T>`, empty when no data
- Cache-line aligned atomics (`alignas(64)`) to prevent false sharing

### VSCode Extension (`vscode-uzengine/`)

| Item | Detail |
|------|--------|
| Language | TypeScript, compiled with `tsc` |
| Activation | On command `uzengine.openStreamViewer` |
| Panel | `WebviewPanel`, singleton, `retainContextWhenHidden: true` |
| CSP | `connect-src ws://localhost:* wss://localhost:*; media-src blob:; script-src 'nonce-...'` |
| MSE codec | `video/mp4; codecs="avc1.42E01F"` (H.264 Baseline 3.1) |
| SourceBuffer mode | `sequence` — ignores timestamps, appends in arrival order |
| Buffer trim | Keeps last 10 s behind playhead; older data removed on `updateend` |
| Reconnection | Exponential backoff: 1 s → 2 s → 4 s → ... → 16 s cap |
| Configuration | `uzengine.serverUrl` (VSCode setting, default `ws://localhost:9002`) |
| Lint | ESLint 9 + `typescript-eslint` v8, flat config (`eslint.config.js`) |

## Build System

- CMake 3.20+ with Visual Studio 2022 generator
- vcpkg for dependency management (`ixwebsocket`)
- C++20 standard
- Windows SDK for DirectX 12, DXGI, D3DCompiler, Windows Media Foundation

### Build Commands (C++ App)

```bash
# Configure (first time — vcpkg installs ixwebsocket automatically)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build Debug
cmake --build build --config Debug

# Build Release
cmake --build build --config Release

# Run
./build/Debug/UzEngine.exe
```

### Build Commands (VSCode Extension)

```bash
cd vscode-uzengine
npm install
npm run compile   # tsc

# To debug: open vscode-uzengine/ in VSCode, press F5
# To package: npm run package  (produces .vsix)
```

## Key Design Decisions

1. **Root 32-bit constants** over constant buffers for MVP matrix — simpler, no per-frame upload heap management
2. **Upload heap** for vertex buffer — 72 bytes of static geometry doesn't need a default heap + copy queue
3. **Runtime shader compilation** via `D3DCompile` — no build-time `fxc` step, shaders embedded as strings
4. **Drop-oldest ring buffer** — render thread never blocks; encoder silently skips frames when behind
5. **Windows MF over FFmpeg** — zero external build dependencies; `CLSID_CMSH264EncoderMFT` ships with Windows 10+
6. **Inlined webview script** — avoids webpack/bundler, keeps CSP tight, simpler build
7. **`SourceBuffer.mode = 'sequence'`** — correct for live fMP4; browser manages timestamps automatically
8. **NVENC stub** — project compiles and runs without NVIDIA SDK; factory falls through to MF automatically
