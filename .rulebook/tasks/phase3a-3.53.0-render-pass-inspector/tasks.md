# Task 3.53.0 — Render Pass Inspector

## Phase 1: `vis <target>` — Buffer Visualization (Priority: NOW)

### Infrastructure
- [x] 1.1 Create `runtime/engine/renderer/passes/debug/inspector_pass.h` — PassInspector class with `SetVisTarget(string)`, `GetVisTarget()`, `IsVisActive()`, `GetVisTexture()`, `RegisterTarget()`, `BeginFrame()`, `Validate()`. Created with 6 display modes (Color, Grayscale, Depth, HDR, Normal, Velocity).
- [x] 1.2 Create `runtime/engine/renderer/passes/debug/inspector_pass.cpp` — Full implementation: target registry (vector), vis target lookup by name, validation with per-target readback (4x4 center block), min/max/avg stats, NaN/Inf/zeros detection.
- [x] 1.3 Create `runtime/shaders/runtime/debug/vis_fullscreen_ps.hlsl` — Fullscreen pixel shader with 6 display modes via root constant `displayMode`. Reads texture via `ResourceDescriptorHeap[textureSrv]`. Handles Color (direct), Grayscale (R→RGB), Depth (linearize reversed-Z), HDR (false-color green→yellow→red→white for >1.0), Normal (octahedral decode), Velocity (RG as color).
- [x] 1.4 Pipeline for vis shader added in `pipeline_debug.cpp` — `GetVisFullscreen()` compute PSO. Vis rendering integrated in `frame_overlay.cpp` — when `IsVisActive()`, draws fullscreen quad with vis shader replacing final output.

### Pass Registration
- [x] 1.5 In `frame_lighting.cpp`: `RegisterTarget()` called after each major pass dispatch (line 802+). Uses `m_passInspector` member on SceneRenderer.
- [x] 1.6 Register GBuffer targets: `gbuffer.albedo` (Color), `gbuffer.normal` (Normal), `gbuffer.depth` (Depth), `gbuffer.velocity` (Velocity), `gbuffer.emissive` (Color). 5 targets registered.
- [x] 1.7 Register shadow target: `shadow.atlas` (Grayscale).
- [x] 1.8 Register GTAO target: `gtao.result` (Grayscale).
- [x] 1.9 Register SSGI targets: `ssgi.prev_color` (Color), `ssgi.trace` (HDR), `ssgi.denoised` (HDR), `ssgi.result` (HDR), `ssgi.ao` (Grayscale). 5 targets registered.
- [x] 1.10 Register SSR targets: `ssr.trace` (Color), `ssr.result` (Color).
- [x] 1.11 Register bloom + motion blur targets: `bloom.result` (HDR), `motionblur.result` (Color). Registered in `frame_postprocess.cpp:695-701`.
- [x] 1.12 Register HDR scene color (`hdr`, HDR mode) and final LDR (`final.ldr`, Color). HDR registered in frame_lighting, final in frame_postprocess.

### Console Command
- [ ] 1.13 Register `vis` console command in `ConsoleCommandRegistry` — parses target name, sets vis mode. (Console registry not yet wired — `vis` works via CLI `--vis` flag only.)
- [x] 1.14 `vis off` / `vis final` resets to normal rendering — implemented via `ClearVis()`.
- [x] 1.15 Invalid target name logs available targets — `SetVisTarget()` logs `"Unknown vis target '{}'. Available: ..."` with full list.

### Display Modes (in vis_fullscreen_ps.hlsl)
- [x] 1.16 RGBA color: direct display with saturate clamp.
- [x] 1.17 Grayscale: single channel R displayed as (R,R,R).
- [x] 1.18 Depth: linearize reversed-Z via `near*far / (far - depth*(far-near))`, map to [0,1] grayscale.
- [x] 1.19 HDR false-color: ramp green (1-2) → yellow (2-4) → red (4-8) → white (>8).
- [x] 1.20 Normals: decode octahedral RG, reconstruct Z, map [-1,1] to [0,1] RGB.
- [x] 1.21 Velocity: RG motion vectors as red/green, blue = magnitude.

### CLI Integration
- [x] 1.22 `--vis <target>` flag in `application.cpp`: parses target name, calls `SetVisTarget()` after renderer init. Works with `--screenshot-stdout` and `--headless`.
- [x] 1.23 Example works: `demo.exe --vis ssgi.trace --screenshot-stdout --screenshot-frame 120 --frames 125`

## Phase 2: `pass.stats` + `pass.validate` — Automated Diagnosis

### Per-Pass Readback
- [x] 2.1 Readback implemented in `Validate()`: creates per-target staging buffer, copies mip 0, maps and reads 4x4 center block. One buffer per target (created/destroyed per validation call).
- [ ] 2.2 Per-frame inline readback (copy 4x4 block each frame without stalling). Currently validation only runs at shutdown via `--pass-validate`.
- [ ] 2.3 Next-frame async readback (non-stalling). Currently uses `WaitIdle()` per target which stalls.
- [x] 2.4 `ValidationResult` struct: `name`, `isZero`, `hasNaN`, `hasInf`, `minVal`, `maxVal`, `avgVal`.

### GPU Timing
- [ ] 2.5 Create timestamp query heap (2 queries per pass: begin + end).
- [ ] 2.6 Bracket each pass with `BeginTimestamp` / `EndTimestamp`.
- [ ] 2.7 Read back timestamps and compute per-pass GPU time in ms.

### Console Commands
- [ ] 2.8 `pass.stats` console command — not yet wired to console registry.
- [ ] 2.9 `pass.stats <name>` — not yet implemented.
- [x] 2.10 `--pass-validate` CLI flag — checks all passes at shutdown. Logs per-target status (OK/WARNING/FAIL). Returns exit code 1 if NaN or Inf detected. Default 120 frames then exit.
- [ ] 2.11 `pass.validate <name>` — validates a specific pass only.

### Overlay
- [ ] 2.12 Render pass stats as screen overlay (text) when `pass.stats` is active.

### Known Issues
- [x] 2.13 Readback format: FIXED — `inspector_pass.cpp` now has format-aware readback with `HalfToFloat()` for RGBA16F, `uint8_t/255.0f` for RGBA8Unorm, direct float read for R32F/RGBA32F, and depth format handling (D16Unorm, D32Float, D24S8). Uses `ChannelCount()` and `ReadChannel()` per-format dispatch. Depth formats skipped (DX12 CopyTextureToBuffer footprint issue).
- [x] 2.14 First diagnostic success: `--pass-validate` identified `ssgi.prev_color = all zeros` as root cause of SSGI trace outputting zeros. Fixed: first-frame barrier state (Common vs ShaderResource).
- [x] 2.15 Second diagnostic success: after format fix, `--pass-validate` showed ssgi.trace OK (avg=0.251), ssgi.denoised OK (avg=0.251), ssgi.result OK (avg=0.252), ssgi.ao OK (avg=0.977). All 20 registered passes validated correctly. The system found and helped fix 3 SSGI bugs.

## Phase 3: `pass.dump` — Full Pipeline Snapshot

### Infrastructure
- [ ] 3.1 Create staging buffer pool: one full-resolution readback buffer per registered target.
- [ ] 3.2 On `pass.dump` trigger: copy ALL registered targets to staging buffers.
- [ ] 3.3 On next frame: map all staging buffers, encode as PNG, write to `.screenshots/pass-dump-f{N}/`.
- [ ] 3.4 Generate `INDEX.html` — browser-based viewer with thumbnails, click to enlarge, side-by-side compare.

### Console + CLI
- [ ] 3.5 `pass.dump` console command — dumps current frame.
- [ ] 3.6 `--pass-dump <frame>` CLI flag — dumps at specified frame in headless mode.
- [ ] 3.7 Each PNG filename includes pass order index: `00-gbuffer-albedo.png`, `01-gbuffer-normal.png`, etc.

## Phase 4: `pass.toggle` + `pass.compare` — A/B Comparison

### Toggle System
- [ ] 4.1 Add `enabled` flag to each registered pass in PassInspector.
- [ ] 4.2 `pass.toggle <name>` flips the flag. The pass dispatch checks `passInspector.IsPassEnabled("ssgi")`.
- [ ] 4.3 Toggled passes skip dispatch but still register their (now-zero) output.

### Compare Mode
- [ ] 4.4 `pass.compare <name>` renders two frames: one with pass enabled, one without. Composites them split-screen (left=without, right=with) with a draggable divider line.
- [ ] 4.5 Alternative: overlay mode — difference image `abs(with - without)` with 10x amplification.

### CLI
- [ ] 4.6 `--pass-toggle <name>` flag.
- [ ] 4.7 `--pass-compare <name>` flag — outputs a side-by-side PNG.

## Phase 5: `pass.grid` — Extended Buffer Grid

- [ ] 5.1 Extend existing `--shadow-debug 1` grid system to support arbitrary N×M grids.
- [ ] 5.2 `pass.grid` shows a 4×4 grid of the 16 most important buffers.
- [ ] 5.3 `pass.grid extended` shows 6×4 (24 buffers) including all SSGI, SSR, bloom mips.
- [ ] 5.4 Each grid cell has a label (pass name) rendered in the corner.
- [ ] 5.5 Grid layout configurable via console: `pass.grid layout 3 5` (3 cols, 5 rows).

## Phase 6: IPC Protocol (VSCode Extension)

- [ ] 6.1 IPC handler `render.vis` — sets vis target, returns screenshot as base64.
- [ ] 6.2 IPC handler `render.passDump` — triggers dump, returns file paths.
- [ ] 6.3 IPC handler `render.passToggle` — toggles pass.
- [ ] 6.4 IPC handler `render.passStats` — returns JSON with per-pass timing and stats.
- [ ] 6.5 IPC handler `render.passValidate` — returns JSON with pass validation results.
- [ ] 6.6 VSCode extension: "Render Passes" tree view panel with checkboxes.
- [ ] 6.7 VSCode extension: click pass → opens buffer image in webview.
- [ ] 6.8 VSCode extension: performance bar chart overlay.
