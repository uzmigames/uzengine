# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- **GPU Compute IBL Pipeline** — `IblBaker` class with 3 compute shaders replacing CPU-side IBL convolution
  - `ibl_brdf_lut.cs.hlsl`: 256×256 RG16F BRDF integration LUT (1024 Hammersley samples)
  - `ibl_irradiance.cs.hlsl`: cosine-weighted irradiance cubemap (32×32, 1024 hemisphere samples)
  - `ibl_prefilter.cs.hlsl`: GGX importance-sampled prefiltered env map (128×128, 5 mip levels, 256 samples)
  - CPU fallback via `brdf_lut.cpp` and `sky_cubemap.cpp` when GPU compute init fails

- **Post-Process Stack** — full HDR rendering pipeline with 3 post-process passes
  - `hdrRT_`: full-resolution R11G11B10_FLOAT scene color render target
  - `ssao.ps.hlsl` + `ssao_blur.ps.hlsl`: half-resolution SSAO with bilateral blur (hemisphere sampling, depth reconstruction from projection matrix)
  - `bloom_down.ps.hlsl` + `bloom_up.ps.hlsl`: 5-mip progressive downsample/upsample bloom chain
  - `composite.ps.hlsl`: final tonemap (ACES filmic) + gamma correction + SSAO/bloom compositing
  - `fullscreen.vs.hlsl`: shared fullscreen triangle vertex shader for all post-process passes
  - Post-process targets auto-recreated on viewport resize

- **Shadow Mapping** — directional light shadow pass
  - 2048×2048 D32_FLOAT shadow depth texture with SRV access for PCF sampling
  - `shadow_depth.vs.hlsl`: depth-only vertex shader with light-space matrix
  - Poisson-disk 16-tap PCF with distance-based penumbra softening
  - Orthographic light projection, bias/orthoSize/lightDist configurable per-frame

- **Procedural Sky Shader** — fullscreen sky background with atmosphere
  - `sky.vs.hlsl` + `sky.ps.hlsl`: ray-direction reconstruction from camera basis + perspective params
  - Zenith/horizon/haze/ground gradient blending with exponential falloff
  - Sun disk rendering (angular falloff + corona bloom from directional light)
  - `SkyAtmosphereData` component: per-scene sky colors + fog density, driven from `.uzscene`

- **PBR Shader Enhancements** — `pbr.ps.hlsl` expanded from ~100 to ~400+ lines
  - Parallax Occlusion Mapping (POM): steep ray march (8–32 layers based on NdotV) + linear interpolation, with grazing-angle and sphere-pole guards
  - Heightmap self-shadow: 6-tap light-direction trace on displacement map → contact shadows in mortar/ripples/weave
  - IBL split-sum specular: cubemap irradiance (diffuse) + prefiltered env + BRDF LUT (specular)
  - Emission support: HDR emission color × energy multiplier
  - Per-material shader params: `envMultiplier`, `normalStrength`, `specularMultiplier`, `selfShadowStrength`, `cavityStrength`, `uvTiling`
  - Texture slots expanded from 5 (t0–t4) to 10 (t0–t9): +displacement, +shadow map, +BRDF LUT, +irradiance cubemap, +prefiltered env cubemap

- **Material System Expansion** — `MaterialCB` expanded from 8 to 20 DWORDs
  - New `.uzmat` keys: `parallax_scale`, `emission`, `emission_energy`, `emission_texture`, `env_multiplier`, `normal_strength`, `specular_multiplier`, `uv_tiling`, `self_shadow_strength`, `cavity_strength`, `displacement_texture`
  - 5 new materials: `checker`, `gold_paint`, `metal_corroded`, `rattan`, `sand`
  - Updated existing materials (`concrete`, `copper`, `gold`, `iron`, `marble`, `rust`, `stonebricks`, `wood`) with new Poliigon texture paths and PBR params

- **ComponentRegistry** — data-driven node type registry (Godot ClassDB pattern)
  - `component_registry.h/cpp`: string-keyed type registration with property descriptors (`PropParseFn`/`PropSerFn`)
  - New types are registered by a single call — no modification of loader, saver, or renderer
  - `SkyAtmosphereData` registered with 5 property descriptors (sky_zenith, sky_horizon, sky_haze, sky_ground, fog_density)

- **RHI Compute Pipeline** — full GPU compute support in `rhi_dx12.c`
  - `compute_pipeline_create`: dedicated compute root signature (b0 root constants, t0+t1 SRV, u0+u1 UAV, s0 static sampler), D3DCompile cs_5_0
  - `cmd_set_uav(cmd, slot, tex, mip_level)`: per-mip UAV binding via bump allocator in heap region 4096+
  - Compute vs graphics command routing via `is_compute_bound` flag on command list slots
  - `texture_upload_subresource`: per-subresource upload for cubemap face data
  - Typeless depth textures (R32_TYPELESS) for simultaneous DSV + SRV access (shadow map)

- **Texture Asset Reorganization** — per-material directories under `assets/textures/`
  - New Poliigon texture sets: `Concrete/`, `Gold/`, `Grass/`, `Metal/`, `MetalCorroded/`, `RattanWeave/`, `Sand/`, `StoneQuartzite/`, `Stonebricks/`
  - `checker.png` procedural checker pattern (via `tools/gen_checker.py`)
  - Removed flat loose textures (concrete_*.png, copper_*.png, gold_*.png, marble_*.png, rust_*.png, wood_*.png)

- **12 new PBRMaterialTest cases** — displacement parsing, per-material shader params, emission, UV tiling (scalar+vector), minimal defaults, comments, error paths, clear, full sand material round-trip

- **Editor Grid Improvements** — distance-based alpha fade (UE5-style), camera world position passed to basic.ps.hlsl via `ColorCB`

- **Tools** — `tools/gen_checker.py` (procedural checker texture), `tools/gen_plane.py` (plane mesh generator)

### Changed

- **Root signature (graphics)**: expanded from 9 to 14 params — b1 from 8 to 20 DWORDs (MaterialCB), b2 from root constants to root CBV (LightFrameCB), 10 SRV tables (t0–t9), 2 static samplers (s0 wrap, s1 clamp)
- **NodeType enum removed** — replaced with `type_name` string field on `SceneNode`; `node_type_to_string()` and `node_type_from_string()` deleted
- **Scene loader/saver rewritten** — fully data-driven via ComponentRegistry; zero code changes needed for new node types
- **IBL convolution** runs on GPU compute with automatic CPU fallback if compute init fails
- **CBV/SRV/UAV descriptor heap** expanded from 4096 to 8192 (SRV 0..4095, UAV 4096+)
- `scene_create_node()` signature changed: `NodeType type` → `const char* type_name`
- `pbr_test.uzscene` expanded with 10 material spheres + ground plane, SkyAtmosphere node
- `sphere.gltf` and `plane.gltf` regenerated with proper normals/tangents/UVs
- `renderer.md` IBL section updated with GPU compute pipeline documentation
- Stream module refactored (`stream.h/cpp`): broadcast busy flags, encode thread cleanup
- MCP server (`tools/mcp-uzengine/`): updated IPC handlers, engine process management

### Fixed

- **Metallic fallback texture** was BLACK → forced `metallic=0` for all untextured materials. Fixed to WHITE so material `metallic` parameter actually controls the value. All fallback textures now WHITE except normal (flat blue `0x8080FF`) and displacement (BLACK = no offset)

---

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
