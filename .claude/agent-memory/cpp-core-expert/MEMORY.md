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
  - `setTextMessageCallback(TextMessageCallback)` for incoming text (non-binary) WS messages
  - Text messages handled in `setOnClientMessageCallback` lambda (checks `!msg->binary`)

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
- **IPC handlers registered in main.cpp**: engine.ping, engine.getStatus, engine.screenshot, scene.setRotation, scene.setBackground, scene.setPaused, scene.setGeometry, scene.setParam, file.read, file.write, file.list, shader.reload, camera.getState, camera.reset
- **Renderer integration**: `DX12Renderer::setSceneParams()` + `getLastFrameCopy()` + background color + pause support
- **Fly Camera**: UE5-style fly camera in editor mode (renderer.cpp update()), driven by SceneParams::CameraInput atomics
  - WS text JSON `{"type":"camera_input", "w":1, ...}` -> camera input atomics (additive mouse/scroll via CAS loop)
  - Scroll wheel adjusts speed (pow(1.1, scroll), clamped 0.1-500)
  - Mouse look only when RMB held, pitch clamped +/-89 deg
  - WASD movement along forward/right vectors, E/Q for up/down, Shift=3x boost
  - deltaTime clamped to 0.1s max to prevent jumps
  - Left-handed coords: +X right, +Y up, +Z forward; yaw=0 = looking along +Z
- **vcpkg location on this machine**: `C:/vcpkg/vcpkg.exe` (not E:/vcpkg)

## Debug/Profiling Module (src/engine/core/)
- **assert.h**: ASSERT, ASSERT_MSG, VERIFY, ENSURE macros. `uz::detail::assert_fail[_msg]()` in debug; no-ops/eval-only in UZ_SHIPPING.
- **profiler.h**: UZ_PROFILE_FUNCTION/ZONE/FRAME_MARK/VALUE/MESSAGE. Tracy-backed when UZ_PROFILING=1, zero-overhead no-ops otherwise.
- **ArenaStats**: struct {used, peak, capacity, name} + `arena_get_stats()` accessor added to arena.h/.cpp.
- Test count after phase1-debug-profiling: 811 (was 763)

## Platform Abstraction Layer (src/engine/core/)
- **platform.h**: Platform detection macros (UZ_PLATFORM_WINDOWS/LINUX/MACOS) + C++ PAL API
  - Timing: `get_time_seconds()`, `get_time_microseconds()` (monotonic)
  - File I/O: `path_exists()`, `file_size()`, `file_read_all()`, `file_write_all()`, `mkdir_recursive()`
  - Threads: `thread_create/join/detach()`, `thread_current_id()`, `thread_sleep_ms()`
  - Mutex: `mutex_create/destroy/lock/unlock/try_lock()`
  - DynLib: `dynlib_open/close/symbol()`
- **platform.cpp**: Full Linux impl (pthreads, dlopen, CLOCK_MONOTONIC, POSIX stat/stdio) + Windows impl (QPC, CreateThread, CriticalSection, LoadLibrary)
- All opaque structs (Thread*, Mutex*, DynLib*) use malloc/free -- PAL objects, not gameplay data
- Namespace: `uz::platform`
- Linux mutex uses PTHREAD_MUTEX_ERRORCHECK for safety during development
- CMakeLists.txt links `-lpthread -ldl` on non-Windows via generator expressions
- Test count after phase1-platform-abstraction-layer: 870 (was 811, +59 new tests)

## Shader Hot-Reload Module
- **file_watcher.h/.cpp** (src/engine/core/): `FileWatcher` - directory monitor with per-path debounce
  - Linux: inotify (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE) + pipe for stop signal
  - Windows: ReadDirectoryChangesW with overlapped I/O + event for stop signal
  - macOS: stub (watch() returns false)
  - Background thread via `uz::platform::thread_create`; `thread_entry` is a static private member (avoids private-member-access issue with free functions)
  - Debounce: per-filename timestamp map, default 200ms window
  - PIMPL pattern with `Impl*`
- **shader_manager.h/.cpp** (src/engine/renderer/): `ShaderManager` - in-memory shader cache
  - `register_shader(name, vs_path, ps_path)` + `reload(path)` + `get_errors(path)` + `list()` + `get_info(name)`
  - Thread-safe via `uz::platform::Mutex` (custom MutexGuard RAII in .cpp)
  - Linux: no DXC compilation; reload always succeeds with is_valid=true
  - Reload callback: `ShaderReloadCallback` invoked per affected shader under lock
  - `list()` returns sorted names for deterministic output
- **ipc_shader_handlers.h/.cpp** (src/common/): IPC handlers for shader.reload, shader.getErrors, shader.list, shader.getSource, shader.setSource
  - Follows same pattern as scene_handlers.cpp (capture pointer, return JSON)
  - shader.getSource/setSource use `uz::platform::file_read_all/file_write_all`
- Test count after phase1-shader-hot-reload: 936 (was 870, +66 new tests)

## Viewport Manager Module (src/engine/renderer/)
- **viewport_registry.h/.cpp**: `ViewportRegistry` - manages N independent viewports with lifecycle state machine
  - State machine: INACTIVE -> ACTIVE <-> SUSPENDED, any -> DESTROYED
  - Hard caps: kMaxViewports=16, kMaxActiveViewports=4, kVramBudgetBytes=512MB
  - Slot array: `ViewportInfo slots_[kMaxViewports]` with monotonic IDs
  - VRAM enforcement: auto-suspends oldest ACTIVE (smallest frame_count) when budget exceeded
  - Heartbeat watchdog: auto-suspends all if seconds_since_heartbeat() > 30s
  - Thread-safe via `uz::platform::Mutex` (PIMPL + MutexGuard RAII)
  - `ViewportSuspendCallback` invoked on auto-suspend
- **ipc_viewport_handlers.h/.cpp** (src/common/): IPC handlers for viewport.create/destroy/setActive/resize/getStatus/list
  - Same pattern as ipc_shader_handlers (capture pointer, return JSON)
- **Gotcha**: nlohmann::json `is_number_unsigned()` returns false for positive signed ints like `1920`. Use `is_number_integer()` instead for JSON integer validation.
- Test count after phase1-viewport-manager: 1022 (was 936, +86 new tests)

## Crash Reporter Module (src/engine/core/)
- **crash_reporter.h/.cpp**: `CrashReporter` - portable signal/exception handler
  - Linux: sigaction(SA_SIGINFO | SA_RESETHAND) for SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS
  - Signal handler: async-signal-safe (open/write/close/_exit, backtrace as best-effort)
  - Manual write_report(): stdio path with backtrace_symbols for symbolic traces
  - Report format: [crash] (timestamp, signal, details) + [stack] + [engine] (version, build)
  - Static state: s_output_dir[460], s_last_report_path[512], s_installed, s_pre_crash_callback
  - install()/uninstall() for lifecycle, set_pre_crash_callback() for pre-crash hooks
  - Windows: SetUnhandledExceptionFilter stub (Phase 2)
- Test count after phase1-crash-reporting: 1053 (was 1022, +31 new tests)

## Render Graph Module (src/engine/renderer/)
- **render_graph.h/.cpp**: `RenderGraph` - frame-local DAG for GPU resource management and pass scheduling
  - Resource handles: `RgTextureHandle`, `RgBufferHandle` (uint32_t indices into resources vector)
  - `kRgHandleInvalid = UINT32_MAX`, `kRgMaxPasses = 64`, `kRgMaxResources = 128`
  - Resource types: `RgTextureDesc`, `RgBufferDesc` with name[64] char arrays
  - Pass types: Graphics, Compute, Copy (enum class `RgPassType`)
  - `RgPassBuilder`: fluent API for declaring read/write deps + force_run()
  - Compile phase: Kahn's toposort + backward dead-pass culling from "output frontier"
  - Output frontier: force_run passes + passes writing to imported/persistent resources
  - Liveness propagation: backward BFS through reverse adjacency
  - Execute: iterates execution_order_, calls execute_fn(cmd_list) for live passes
  - No DX12/Vulkan/Metal headers -- backend-agnostic via RHI handles
  - PIMPL pattern with `Impl*` (std::vector storage, not platform::Mutex -- not thread-safe by design, frame-local)
  - Move semantics: move ctor/assign transfer impl_ ownership
  - Cycle detection: compile() returns false if toposort doesn't cover all passes
- **Gotcha**: `std::strncpy` not available in `<cstring>` on Clang C++23 without explicit include. Use `#include <cstring>` or bare `strncpy`.
- Test count after phase1-render-graph: 1086 (was 1022, +64 new tests)

## Lua Scripting Module (src/engine/scripting/)
- **lua_vm.h/.cpp**: `LuaVM` - LuaJIT wrapper with sandbox (strips os/io/debug/require/etc.)
  - Bindings: uz.delta_time(), uz.total_time(), uz.log(), uz.scene.*, uz.camera.*
  - VM pointer stored in Lua registry for binding callbacks
- **script_runner.h/.cpp**: `ScriptRunner` - manages script instances, ticks each frame
  - init/shutdown, attach_script/detach_scripts, clear, scan_scene, find_nodes_by_script_path
- **lua_script_instance.h/.cpp**: Per-script Lua env (isolated table with __index=_G)
  - NODE_ID available in script env, on_start/on_update/on_stop lifecycle
- **lua_script_reloader.h/.cpp**: Hot-reload — on_file_changed(path) detaches+reattaches
- **Test count**: 108 tests in tests/scripting/test_lua_scripting.cpp
- **Build**: LuaJIT linked to both UzEngine and UzEngineTests, lua51.dll copied to output dirs
- **Gotcha**: Scene struct is ~26MB (65536 SceneNodes) — NEVER stack-allocate, always heap

## User Preferences
- Communicates in Portuguese (BR)
- Prefers DX12 (not DX11)
