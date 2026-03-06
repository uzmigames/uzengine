# Changelog

All notable changes to this project will be documented in this file.

## [0.4.0] — 2026-03-06

### Added
- **AI Debug Pipeline** — comprehensive debugging and telemetry infrastructure for AI-assisted development:
  - **Crash Reporting**: `CrashReporter` with SEH handler, `StackWalk64` symbolication, minidump generation, structured `latest.json` and `latest.dmp` under `.uzengine/crash_reports/`
  - **Crash Enrichment**: command-line args, memory stats (`GlobalMemoryStatusEx`), loaded modules with versions, custom metadata KV pairs (`set_metadata`/`clear_metadata`), crash GUID deduplication, archived reports
  - **Structured Assertions**: `UZ_CHECK` (fatal) / `UZ_ENSURE` (non-fatal) macros with crash report integration
  - **DX12 DRED**: auto-breadcrumbs + page fault capture on `DXGI_ERROR_DEVICE_REMOVED`, active RenderGraph pass name via `rg_get_active_pass_name()`, serialized to `dred_report.json`
  - **Log Streaming**: `LogStreamer` with severity levels (TRACE–FATAL), IPC subscribe/unsubscribe, system filtering, rate limiting (100 msg/sec/subscription), error/fatal buffer
  - **RenderGraph Introspection**: IPC commands `query_render_graph` (pass names, dependencies, formats), `query_render_targets` (6 known RTs), `query_gpu_memory` (DXGI `QueryVideoMemoryInfo`)
  - **GPU Timestamp API**: RHI vtable entries for `timestamp_query_heap_create/destroy`, `cmd_timestamp_write`, `timestamp_readback`
  - **RenderGraph Diagnostics**: `active_pass_name()` for crash-safe pass tracking, `texture_desc()` for resource queries
  - **Golden Image Testing**: `tools/golden_test.ps1` with SSIM comparison via ImageMagick, baseline directory structure, diff image generation
  - **Build Verification**: `tools/build_and_report.ps1` wrapping CMake with structured JSON error parsing, MCP `uz_build` tool with retry
  - **VS Code Extension**: crash detection on disconnect, `uzengine.diagnoseCrash` / `uzengine.analyzeFrame` commands, auto ERROR/FATAL log subscription, error buffer feeding
  - **Agent Rules**: `AGENTS.md` updated with DEBUG-1 through DEBUG-6 rules for AI-assisted debugging workflow
  - **Documentation**: `docs/architecture/ai-debug-pipeline.md`, `docs/guides/golden-tests.md`

- **PBR Material Rendering**:
  - Static mesh rendering with material binding (albedo, normal, metallic, roughness, AO textures)
  - Scene proxy system with mesh proxy pooling and dirty-flag updates
  - Outline shader pass for selected objects (`outline.vs.hlsl`, `outline.ps.hlsl`)

- **Third-party Header Organization**: `thirdparty/` directory for cgltf and stb headers (moved from `src/common/`)

### Changed
- `scene_clear()` now resets `source_path` to prevent auto-save overwriting files during scene load
- Test binary (`UzEngineTests`) no longer defines `NDEBUG` — enables `assert()`-based death tests in Release builds
- RHI DX12 backend: added `dx12_gpu_memory_info` implementation using `IDXGIAdapter3::QueryVideoMemoryInfo`

### Fixed
- **7 death tests failing in Release** (Arena, Result, UndoStack) — `NDEBUG` was disabling `assert()` while `UZENGINE_DEBUG=1` expected them to fire. Removed `NDEBUG` from test target
- **SceneHandlersTest.RoundTrip_SaveLoad** — `scene_clear()` didn't reset `source_path`, causing the load handler to auto-save an empty scene over the test file before loading it
- **CrashReporterTest.WriteReportContainsEngineVersion** — version string updated to match `0.3.0`
- All **1332 tests passing**, 0 failures, 2 skipped (Linux-only DynLib tests)

---

## [0.3.0] — 2026-03-04

### Added
- **Multi-instance engine architecture**: each scene preview spawns its own `render.exe` process with dedicated WebSocket port and IPC directory
  - `engine-instance-manager.ts` — manages lifecycle of multiple engine instances
  - `engine-instance.ts` — per-scene engine process with independent port allocation
  - `--ws-port` and `--scene` CLI arguments for per-instance configuration
  - Scene-specific IPC directories under `build/Release/preview/<scene>/`

- **Component-based renderer with gizmo system**:
  - Data-oriented component storage with type-erased pools and O(1) access
  - Light gizmo rendering (downward-pointing cone, adjustable thickness)
  - Camera gizmo with frustum wireframe visualization
  - Type-based wireframe colors (cyan=StaticMesh, yellow=Camera, amber=Light)

- **Scene state persistence**: save and restore camera positions, node transforms, and editor state across scene switches
  - JSON-based `.uzscene` format with full node hierarchy serialization
  - Editor camera state preservation on scene switch

- **Stream performance optimization**: broadcast busy-flag per encode thread to reduce backpressure stalls
  - `g_broadcastBusyA` / `g_broadcastBusyB` — per-thread atomic flags tracking WebSocket broadcast state
  - Prevents readback skip when broadcast (not encode) is the bottleneck

### Changed
- Light gizmo points downward with raised base position and thicker lines for better visibility
- Code formatting standardized to 2-space indentation in `viewport-panel.ts`

---

## [0.2.0] — 2026-03-03

### Added
- **Dual JPEG encoder threads**: round-robin frame dispatch to 2 independent `JpegEncoder` instances running on separate threads, doubling encode throughput from ~78 fps to ~143 fps (near 144 fps target)
  - `g_frameRingA` / `g_frameRingB` — two SPSC ring buffers, one per encoder thread
  - `g_dispatchToggle` — render thread alternates dispatch between the two rings
  - Backpressure: readback skipped only when both rings are backed up (≥2 frames each)
  - Fallback dispatch: if preferred ring is full, tries the other; drops frame only when both are full
  - Replaced `sleep_for(1ms)` with `yield()` in encode loop (reduces latency under load)

- **FPS overlay in render output**: "FPS: XXX" stamped into the top-left of every streamed frame
  - Wired existing `FPSOverlay` class into `DX12Renderer` (was unused)
  - `fpsOverlay_.update(deltaTime)` called in `update()`, `fpsOverlay_.renderInto()` in `performReadback()`

- **Conditional GPU readback**: skip the expensive GPU copy + fence wait + 3 MB memcpy when encode threads are backed up
  - `renderer.render(bool readback)` — when `readback=false`, skips copy-to-readback-buffer; transitions directly RENDER_TARGET → PRESENT
  - Render loop still runs at full 144 fps for smooth camera movement even when skipping readback

- **Dynamic viewport resize**: render target adapts to VSCode panel size changes in real-time
  - `scene_params.h` — packed `atomic<uint64_t>` for thread-safe resize requests (`requestResize()`/`consumeResize()`)
  - `renderer.cpp` — full DX12 resize pipeline: GPU drain → release RT/depth/readback → `ResizeBuffers()` → recreate resources → update viewport/scissor
  - `viewport-panel.ts` — `ResizeObserver` + 500ms debounce to avoid GPU stalls during drag, periodic 3s fallback
  - `renderer.resize` and `renderer.info` IPC handlers for manual resize/debugging
  - WebSocket `viewport_resize` text message from webview to engine

- **Editor fly camera** (UE5-style): RMB+WASD/EQ navigation in the viewport
  - `scene_params.h` — `CameraInput` struct with 11 atomic fields (move, mouse delta, scroll, RMB, shift)
  - `renderer.cpp` — yaw/pitch look, forward/right/up movement, scroll-wheel speed adjust, Shift=3x boost
  - `viewport-panel.ts` — `pointerlockchange` capture, keyboard state tracking, 60fps `camera_input` JSON via WS
  - `camera.getState` and `camera.reset` IPC handlers

- **Editor scene rendering**: ground grid + wireframe cubes at node positions
  - `renderer.h` — `EditorNode` struct (position, rotation, scale, color), `loadEditorScene()` API
  - `renderer.cpp` — line pipeline state, grid vertex buffer (21x21 lines, red Z / green X axes), wireframe cube buffer (24 lines)
  - `scene.load` IPC handler with type-based wireframe colors (cyan=StaticMesh, yellow=Camera, amber=Light)
  - Color multiplier via `cbuffer ColorCB : register(b1)` in vertex shader

- **144fps render target**: `kTargetFPS` raised from 60 → 144 for smoother editor experience

- **4-byte viewport ID framing**: JPEG frames now prefixed with `uint32_t viewport_id` for future multi-viewport demux

- **VSCode extension enhancements**:
  - `buildViewportStreamHtml()` extracted as reusable pure function for UzScene editor embedding
  - Scene tree provider, stream panel, and extension updates for new editor features
  - New test suites for extension, scene-tree-provider, stream-panel, viewport-panel

### Fixed
- **DX12 fence deadlock on resize** — After `ResizeBuffers()` changed the back-buffer index, `fenceValues_[]` for the old slot were stale, causing `moveToNextFrame()` to signal a fence value *lower* than the current completed value. The fence went backwards and `WaitForSingleObjectEx` hung forever. Fix: sync all `fenceValues_[i]` to `fence_->GetCompletedValue() + 1` after resize.
- **Resize race condition** — Two separate `atomic<int>` for width/height allowed the render thread to read width before height was stored (getting `rh == 0`). Fix: packed both into a single `atomic<uint64_t>`.
- **sendResize dedup preventing retry** — `lastSentWidth/Height` stored before checking WS open state, preventing retry on reconnect. Fix: moved storage inside the `readyState === OPEN` guard.
- **renderer.resize() inconsistent state on failure** — `width_/height_` set before `ResizeBuffers()`, leaving wrong dimensions if it failed. Fix: set only after success.

- **Engine IPC protocol** (`add-engine-ipc`): file-based IPC via `build/Release/preview/`
  - `src/common/ipc_registry.h/cpp` — function dispatch table (`string → handler`)
  - `src/common/ipc_poller.h/cpp` — dedicated thread polling `command.json` every 50ms, atomic writes via `.tmp + rename`, `status.json` updated every 1s
  - `src/renderer/scene_params.h` — thread-safe scene state (`std::atomic` for rotation/pause, `std::mutex` for background color)
  - 12 registered handlers: `engine.ping`, `engine.getStatus`, `engine.screenshot`, `scene.setRotation`, `scene.setBackground`, `scene.setGeometry`, `scene.setPaused`, `scene.setParam`, `file.read`, `file.write`, `file.list`, `shader.reload` (stub)
  - `external/stb/stb_image_write.h` + `src/common/stb_impl.cpp` — PNG screenshot capture
  - `nlohmann-json` added to `vcpkg.json` and `CMakeLists.txt`
  - FPS tracking in main loop via QPC, updates `g_metrics.fps` every second

- **MCP server** (`add-mcp-primitives`): `@uzmigames/uzengine-mcp` at `tools/mcp-uzengine/`
  - 5 stable primitive tools: `uz_call`, `uz_screenshot`, `uz_status`, `uz_read_file`, `uz_write_file`
  - `uz_screenshot` returns inline base64 PNG — Claude sees the frame directly
  - `ipc-client.js` handles command/response polling with UUID-based dedup
  - Added `uzengine` entry to `.mcp.json` — auto-discovered by Claude Code on workspace open
  - `docs/ai-interaction.md` — full tool API reference and Skills model documentation

- **H.264 encoder improvements**:
  - Profile/level fallback cascade: `Main@4.2 → Main@4.0 → Baseline@4.2 → Baseline@4.0 → Baseline@3.1`
  - Fixes `SetOutputType failed: 0xC00D36B6` at 60fps (Baseline@3.1 too low for 60fps)
  - BGRA→NV12 conversion with BT.601 coefficients when MFT rejects ARGB32 input
  - `useNV12_` flag auto-detected from `SetInputType` fallback

- **60fps pipeline**:
  - `kTargetFPS = 60` in `src/common/types.h`
  - Muxer ticks: `90000 / kTargetFPS`, keyframe interval: `frameCount % kTargetFPS`
  - Ring buffer reduced to 2 frames for lower latency
  - Encode thread uses `yield()` instead of `sleep(1ms)`
  - Removed vsync: `Present(0, 0)` instead of `Present(1, 0)`

- **3D pyramid geometry**:
  - Replaced flat triangle with 4-face pyramid (apex yellow, base RGB vertices)
  - Depth buffer added (D32_FLOAT DSV heap), Z-test enabled in PSO
  - Rotation: Y-axis + 0.3 rad X tilt for 3D depth perception

- **Headless window mode**:
  - `UZENGINE_HEADLESS=1` env var suppresses window
  - Window created with `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_POPUP` at position `-32000,-32000`
  - `ShowWindow` never called in headless mode — zero taskbar presence, zero Alt+Tab

- **VSCode extension**:
  - Extension starts render.exe automatically when stream viewer opens
  - MSE codec string updated: tries `Main@4.2 → Main@4.0 → Baseline@4.0` in order
  - Buffer trim reduced to 0.3s + live-edge seek when >0.5s behind buffered end
  - `.vscodeignore` fix: `server-manager.js` now included in `.vsix`

### Fixed
- `ix::initNetSystem()` missing on Windows — caused immediate crash on startup
- `ICodecAPI` undeclared — added `#include <strmif.h>`
- `QueryInterface` wrong overload — changed to `__uuidof(ICodecAPI)` form
- Colored vertical stripes in stream — BGRA readback was fed as NV12 without conversion
- `ixwebsocket` include path — added explicit vcpkg include dir to CMakeLists
- `zlib1.dll` missing at runtime — added `zlib` to vcpkg dependencies

---

## [0.1.0] — Initial Release

- DX12 spinning triangle renderer with GPU readback
- NVENC primary encoder + MF H.264 (Windows Media Foundation) fallback
- Hand-written fMP4 muxer (fragmented MP4, no external libs)
- WebSocket server via IXWebSocket for real-time H.264 streaming
- VSCode extension with MSE player in webview (MediaSource Extensions)
- Full pipeline: DX12 → readback → H.264 → fMP4 → WebSocket → VSCode webview
