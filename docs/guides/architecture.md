# UzEngine PoC - Architecture

## Overview

UzEngine is a proof-of-concept for a 3D engine coupled to VSCode via JPEG streaming. A C++ application renders 3D content using DirectX 12 on the GPU, encodes each frame to JPEG using libjpeg-turbo, and streams individual JPEG images via WebSocket. A VSCode extension receives the stream and displays it in a webview panel using data URL image tags.

## Pipeline

```
[DX12 Render] --> [Readback GPU->CPU] --> [JPEG Encode] --> [WebSocket Broadcast]
     |                    |               (libjpeg-turbo)           |
  Pyramid              RGBA pixels        quality 85           [VSCode Extension]
  1280x720            row-major           TJSAMP_420          [Webview + <img> tag]
  60 fps target       8-slot ring                          [data:image/jpeg;base64]
                      buffer
```

## Threading Model

```
Main Thread (Render)          Encode Thread                  IXWebSocket Threads
─────────────────────         ─────────────────────          ─────────────────────
Win32 message loop            pop() from ring buffer         accept WebSocket connections
DX12 render + readback   →    JPEG encode (turbojpeg)   →   broadcast JPEG blob
push() to RingBuffer<8>       WsServer::broadcast()          to all connected clients
Frame-rate limiter (60fps)
```

- **RingBuffer\<FrameData, 8\>**: lock-free SPSC, drop-oldest policy (never blocks render thread)
- **Encode thread** sleeps 1 ms when ring is empty; no busy-wait
- **IXWebSocket** manages its own I/O threads; `broadcast()` is mutex-protected
- **Each WebSocket message**: one complete, self-contained JPEG image

## Module Placements

### Editor (VSCode Extension)
| Module | Location | Purpose |
|--------|----------|---------|
| Scene Tree | VSCode TreeView | Edit node hierarchy |
| Inspector | Webview Form | Edit node properties |
| Asset Browser | VSCode TreeView | Browse/import assets |
| Viewport | WebSocket + Canvas | Live 3D preview |
| Shader Editor | Code + Preview | Edit/hot-reload shaders |
| Console | VSCode OutputChannel | Logs + Lua errors |

### Runtime (UzEngine.exe + game.dll)
| Module | Location | Purpose |
|--------|----------|---------|
| **Scene System** | uz_engine.lib | Node tree, scene loading |
| **Renderer** | uz_engine.lib | DX12 rendering, command lists |
| **Asset Manager** | uz_engine.lib | Resource loading, file I/O |
| **LuaJIT VM** | game.dll | Script execution, game logic |
| **Physics** (Jolt) | game.dll | Rigid bodies, collision, raycast |
| **Audio** (XAudio2) | uz_engine.lib | 3D audio, spatial sound, mastering |
| **ECS** (entt) | game.dll | Component-based architecture |
| **IPC Server** | uz_engine.lib | Command polling, JSON handlers |

> **Key insight**: **LuaJIT is inside game.dll**, not the engine core. This means Lua scripts reload without restarting the engine. Similarly, **Jolt Physics is in game.dll** for easy experimentation. **XAudio2 is in uz_engine.lib** but driven via callbacks from game.dll (audio manager).

### Build Artifacts
```
build/Release/
├── UzEngine.exe          (engine runtime)
├── game.dll              (game logic, scripts, physics)
├── uz_engine.lib         (static, linked into .dll)
├── turbojpeg.dll         (JPEG encoder)
├── UzBuild.exe           (offline asset compiler)
└── preview/              (IPC directory)
    ├── command.json
    └── status.json
```

---

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

**Current implementation: JpegEncoder** (libjpeg-turbo TurboJPEG API)

```c++
class JpegEncoder {
    bool encode(const uint8_t* rgba,  // RGBA pixel data
                int width, int height,
                int quality,           // 1-100, typically 85
                std::vector<uint8_t>& out);  // output JPEG blob
};
```

**Encoding settings:**
- **Input format**: `TJPF_RGBA` (4 bytes per pixel, row-major)
- **Chroma subsampling**: `TJSAMP_420` (4:2:0 — good quality/size balance)
- **Quality**: 85 (default, good trade-off for streaming)
- **DCT method**: `TJFLAG_FASTDCT` (fast Fourier transform)

**Output:**
- Each call to `encode()` returns a self-contained JPEG image (~40-80 KB at 1280×720, quality 85)
- No init segment needed — each JPEG is independently decodable
- Not thread-safe — create one instance per thread

**libjpeg-turbo dependency:**
- Installed via `vcpkg` (`x64-windows` dynamic triplet)
- `turbojpeg.dll` is copied to output directory via `POST_BUILD` CMake command
- Zero external build dependencies on Windows (just the DLL at runtime)

### No Muxer (JPEG Era)

Since each frame is encoded as a complete, self-contained JPEG image, there is no need for:
- Init segments
- Fragmented container format
- Codec configuration boxes

Each WebSocket message is simply the raw JPEG binary blob (~40-80 KB). The receiving end displays it directly.

### Network (`src/network/`)

- **Library:** IXWebSocket (vcpkg)
- **Port:** 9002 (default)
- **Protocol:** binary WebSocket frames
  - Each message: one complete JPEG image
  - No init segment (not needed for independent frames)
  - No handshake or setup message
- **Thread safety:** `std::mutex` on client set; `broadcast()` safe to call from encode thread
- **Client lifecycle**: on connect, immediately start receiving JPEG frames

### Ring Buffer (`src/common/ring_buffer.h`)

Lock-free SPSC ring buffer, capacity `N` (power-of-2, default 8 slots).
- **Producer** (render thread): `push()` — overwrites oldest entry when full
- **Consumer** (encode thread): `pop()` — returns `std::optional<T>`, empty when no data
- Cache-line aligned atomics (`alignas(64)`) to prevent false sharing

### VSCode Extension (`vscode-uzengine/`)

| Item | Detail |
|------|--------|
| Language | TypeScript, compiled with `tsc` |
| Activation | Commands: `uzengine.openStreamViewer`, `uzengine.startServer`, `uzengine.stopServer` |
| Panel | `WebviewPanel`, singleton, `retainContextWhenHidden: true` |
| CSP | `connect-src ws://localhost:*; script-src 'nonce-...'` (no media-src, img-src includes data:) |
| Display | `<img id="stream" alt="">` with `src="data:image/jpeg;base64,..."` |
| Rendering | Simple `img.src` assignment — no canvas API, no MSE |
| FPS counter | Client-side, updated every 1 second |
| Reconnection | Exponential backoff: 1 s → 2 s → 4 s → ... → 16 s cap |
| Configuration | `uzengine.serverUrl` (VSCode setting, default `ws://localhost:9002`) |
| Process mgmt | `ServerManager` spawns `UzEngine.exe`, polls port 9002, kills on exit |
| Status bar | Shows running/stopped state, allows click-to-stop |

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

## Game Loop Data Flow

```
Frame N:
┌──────────────────────────────────────────────────────────────┐
│ 1. Scene Update (ECS system in game.dll)                     │
│    ├─ Lua scripts: on_update(ctx, dt)                        │
│    ├─ Physics step: Jolt::StepSimulation()                   │
│    └─ ECS sync: Copy physics results → components            │
└──────────────────────────────────────────────────────────────┘
                          ↓
┌──────────────────────────────────────────────────────────────┐
│ 2. Audio Update (XAudio2 callbacks in uz_engine.lib)         │
│    ├─ Spatial audio: listener pos, sound sources             │
│    └─ Mastering voice: final mix, volume control             │
└──────────────────────────────────────────────────────────────┘
                          ↓
┌──────────────────────────────────────────────────────────────┐
│ 3. Render (DX12 Renderer in uz_engine.lib)                   │
│    ├─ Draw scene graph (forward rendering)                   │
│    ├─ GPU readback to CPU                                    │
│    └─ RGBA pixels → FrameData                                │
└──────────────────────────────────────────────────────────────┘
                          ↓
┌──────────────────────────────────────────────────────────────┐
│ 4. Encode & Stream (Encode thread)                           │
│    ├─ JPEG encode (libjpeg-turbo)                            │
│    ├─ WebSocket broadcast                                    │
│    └─ VSCode webview displays frame                          │
└──────────────────────────────────────────────────────────────┘
                          ↓
                    Frame Rate Limited
                    (60 fps target)
```

## Phase Progression

Which modules are active in each development phase:

| Phase | Scene | Render | Physics | Audio | Lua | Editor |
|-------|-------|--------|---------|-------|-----|--------|
| **0** | Hardcoded | DX12 pyramid | — | — | — | VSCode viewport |
| **1** | .uzscene format, node tree | PBR forward | — | — | — | Scene Tree, Inspector |
| **2** | Prefabs, LODs | PBR deferred, IBL, CSM | — | — | — | Asset Browser, Shader Editor |
| **3** | Complete | Optimized | Jolt physics | XAudio2 | LuaJIT 2.1 | Play mode, Console |
| **4** | — | Advanced FX | Extended | Streaming | Debugging | Full IDE |

> **Current status**: Phase 0 complete. Phase 1-2 planned. Phase 3+ will expand capabilities.

---

## Key Design Decisions

1. **JPEG over H.264/MSE** — Simpler pipeline (no muxing, no codec config), independent frames, zero header overhead, MSE sandbox restrictions avoided
2. **Root 32-bit constants** over constant buffers for MVP — simpler, no per-frame upload heap management
3. **Upload heap** for vertex buffer — static geometry, no need for default heap + copy queue
4. **Runtime shader compilation** via `D3DCompile` — no build-time `fxc` step, shaders embedded as strings
5. **Drop-oldest ring buffer** — render thread never blocks; encoder silently skips frames when behind
6. **libjpeg-turbo (TurboJPEG API)** — High performance, fast DCT, quality 85 gives ~30-50 KB/frame at 1280×720
7. **Data URL img tags** — Avoids MSE sandbox restrictions, simpler client-side code (no binary buffer management)
8. **Inlined webview script** — no webpack, keeps CSP tight, single HTML file
9. **60 fps render target, 30-40 fps display** — encoding frames as JPEGs takes time; drop-oldest absorbs jitter
10. **File-based IPC** (JSON polling) — VSCode extension can restart engine without process management complexity
