# UzEngine - C++ Core Expert Memory

## Project Structure
- **Root**: `E:\UzEngine`
- **Source**: `E:\UzEngine\src/`
- **Renderer**: `E:\UzEngine\src\renderer\` (renderer.h, renderer.cpp)
- **Common**: `E:\UzEngine\src\common\` (types.h, timer.h)
- **Build**: CMake + vcpkg
- **Language**: C++ (CLAUDE.md says "C" but project is clearly C++20)

## Key Constants (types.h)
- `kFrameCount = 2` (double buffering)
- `kDefaultWidth = 1280`, `kDefaultHeight = 720`
- `kTargetFPS = 60`

## Architecture (see [architecture.md](architecture.md))
- DX12Renderer: owns all GPU resources via ComPtr/RAII
- MVP matrix passed via root 32-bit constants (not CBV)
- `createConstantBuffer()` is a declared no-op (header declares it, impl is intentional no-op)
- Shaders compiled at runtime from embedded HLSL strings (kVertexShaderSrc, kPixelShaderSrc)
- UZENGINE_DEBUG macro enables D3D12 debug layer and shader debug flags
- **Geometry**: 4-faced pyramid (tetrahedron) with 12 vertices (no index buffer)
- **Depth buffer**: D32_FLOAT DSV with dedicated heap (`depthBuffer_`, `dsvHeap_`), created in `createDepthBuffer()`
- **Present**: SyncInterval=0 (no vsync) for minimal latency
- **Ring buffer**: kRingSize=2 for low-latency frame handoff to encode thread
- **Encode thread**: uses `std::this_thread::yield()` instead of `sleep_for(1ms)`

## Conventions
- Namespace: `uz`
- Member naming: trailing underscore (e.g., `width_`, `device_`)
- ComPtr for all COM objects
- ThrowIfFailed helper in anonymous namespace for HRESULT error handling
- Comments explain "why", not "what"
- Sequential file editing required (CLAUDE.md mandate)

## Encoder Module (src/encoder/)
- **encoder.h**: Abstract `IFrameEncoder` interface + `CodecConfig` struct
- **nvenc_encoder.h/.cpp**: `NvEncEncoder` - NVENC hardware encoder via dynamic LoadLibrary
  - Stubbed: `isAvailable()` probes DLL, `initialize()` returns false (no SDK headers)
- **ffmpeg_encoder.h/.cpp**: `FFmpegEncoder` - Windows MF H.264 software encoder
  - Uses `CLSID_CMSH264EncoderMFT` via CoCreateInstance (no external deps)
  - Input: ARGB32 preferred, NV12 fallback with software BGRA->NV12 conversion (`useNV12_` flag)
  - On this system ARGB32 is rejected (MF_E_INVALIDMEDIATYPE), NV12 fallback active with color conversion
  - Extracts SPS/PPS from first IDR frame into CodecConfig
  - Links: mf.lib, mfplat.lib, mfuuid.lib, ole32.lib
- **encoder_factory.h/.cpp**: `createEncoder()` tries NVENC first, falls back to MF
- Factory pattern: isAvailable() -> initialize() cascade with fallback

## Muxer Module (src/muxer/)
- **fmp4_muxer.h/.cpp**: `FMP4Muxer` - hand-written ISO 14496-12 fMP4 muxer
  - Produces MSE-compatible init segment (ftyp+moov) and media segments (moof+mdat)
  - Init segment: ftyp(isom) + moov(mvhd+trak(tkhd+mdia(mdhd+hdlr+minf(vmhd+dinf+stbl(stsd(avc1(avcC))+stts+stsc+stsz+stco))))+mvex(trex))
  - avcC box contains SPS/PPS from encoder's CodecConfig
  - Media segments: moof(mfhd+traf(tfhd+tfdt+trun)) + mdat(AVCC NALUs)
  - trun flags=0x0305: data-offset + first-sample-flags + sample-duration + sample-size
  - tfhd flags=0x020000: default-base-is-moof
  - tfdt version=1 for 64-bit base_media_decode_time
  - splitAnnexB() converts Annex B start codes to AVCC 4-byte length prefix, filters SPS/PPS
  - Key frame flags: 0x02000000 (independent), Non-key: 0x01010000 (dependent+non-sync)
  - Default timescale: 90000, frame duration: 3000 (= 90000/30fps)

## Network Module (src/network/)
- **ws_server.h/.cpp**: `WsServer` - WebSocket server via IXWebSocket (vcpkg)
  - PIMPL pattern with `Impl` struct holding `ix::WebSocketServer`
  - Clients stored as `set<shared_ptr<ix::WebSocket>>` with no-op deleter (lib owns socket lifetime)
  - Thread-safe: `clientsMutex` for client set, `initMutex` for init segment
  - `setInitSegment()` + `broadcast()` + `start(port)` + `stop()`

## Build Gotchas
- **ICodecAPI**: Declared in `<strmif.h>`, NOT `<codecapi.h>`. The `<codecapi.h>` only has CODECAPI_* GUIDs.
- **ICodecAPI QueryInterface**: Use `__uuidof(ICodecAPI)` + `reinterpret_cast<void**>`, not `IID_PPV_ARGS` (wrong overload).
- **vcpkg include paths**: MSBuild vcpkg manifest mode may be disabled. CMake `target_include_directories` must explicitly add `${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include` to guarantee headers are found.
- **vcpkg manifest warning**: "The vcpkg manifest was disabled" is benign when include paths are explicit in CMakeLists.txt.

## Runtime Gotchas
- **IXWebSocket requires `ix::initNetSystem()`**: On Windows, must call `ix::initNetSystem()` before any IXWebSocket operations (it calls WSAStartup). Without it, `setsockopt(SO_REUSEADDR)` fails and the server can't bind. Include `<ixwebsocket/IXNetSystem.h>`. Call `ix::uninitNetSystem()` at shutdown.
- **MF H.264 encoder rejects ARGB32 input**: On this system, `SetInputType` with `MFVideoFormat_ARGB32` returns `MF_E_INVALIDMEDIATYPE (0xC00D36B4)`. NV12 fallback is active. `useNV12_` flag tracks this; `createInputSample()` does software BGRA->NV12 conversion (BT.601 full-range, 2x2 chroma subsampling). Without this conversion, raw BGRA bytes fed as NV12 produce garbled color stripes.
- **WIN32 apps + freopen_s data race**: Never redirect both stdout AND stderr to the same file with separate `freopen_s` calls when multiple threads write. The C runtime FILE handles are not thread-safe across different handles on the same file. Use one stream only, or use a proper logging system.
- **WinMain is implicitly noexcept on MSVC**: Throwing from WinMain calls `std::terminate` before any catch block runs. Use `std::set_terminate` to log the error, or catch exceptions inside WinMain.

## IPC Module (src/common/)
- **ipc_registry.h/.cpp**: `IpcRegistry` - named handler map, `add(name, handler)` + `call(name, args)` -> JSON
- **ipc_poller.h/.cpp**: `IpcPoller` - background thread polling `build/preview/command.json` every 50ms
  - Mtime-based change detection via `GetFileAttributesExA` (cheap, no file open)
  - Command dedup by `id` field
  - Atomic file writes via `.tmp` + `MoveFileExA(MOVEFILE_REPLACE_EXISTING)`
  - Writes `status.json` every ~1s with fps/frame/uptime/scene state
  - Writes `response.json` after each command
- **scene_params.h** (src/renderer/): Thread-safe `SceneParams` struct (atomics + mutex for bg color)
- **stb_impl.cpp**: STB_IMAGE_WRITE_IMPLEMENTATION translation unit
- **External dep**: `stb_image_write.h` in `external/stb/`
- **vcpkg dep**: `nlohmann-json` (3.11.3)
- **IPC handlers registered in main.cpp**: engine.ping, engine.getStatus, engine.screenshot, scene.setRotation, scene.setBackground, scene.setPaused, scene.setGeometry, scene.setParam, file.read, file.write, file.list, shader.reload
- **Renderer integration**: `DX12Renderer::setSceneParams()` + `getLastFrameCopy()` + background color + pause support
- **vcpkg location on this machine**: `C:/vcpkg/vcpkg.exe` (not E:/vcpkg)

## User Preferences
- Communicates in Portuguese (BR)
- Prefers DX12 (not DX11)
- NVENC primary + FFmpeg fallback for encoding pipeline
