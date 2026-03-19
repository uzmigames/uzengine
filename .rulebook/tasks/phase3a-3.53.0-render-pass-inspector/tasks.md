# Task 3.53.0 — Render Pass Inspector

## Phase 1: `vis <target>` — Buffer Visualization (Priority: NOW)

### Infrastructure
- [ ] 1.1 Create `runtime/engine/renderer/debug/pass_inspector.h` — PassInspector class with `SetVisTarget(string)`, `GetVisTarget()`, `IsVisActive()`, `GetVisTexture()`.
- [ ] 1.2 Create `runtime/engine/renderer/debug/pass_inspector.cpp` — registry of all named render targets. Each pass registers its output texture + SRV via `RegisterTarget("ssgi.trace", texHandle, srvIndex, format)`.
- [ ] 1.3 Create `runtime/engine/renderer/debug/vis_fullscreen.hlsl` — simple fullscreen PS that reads a texture and displays it. Handles: RGBA color, single-channel grayscale, depth linearization, HDR false-color, motion vector visualization, normal decode.
- [ ] 1.4 Add `vis_renderer.h/.cpp` — dispatches the fullscreen vis pass when active. Replaces the tonemap output with the selected buffer visualization.

### Pass Registration
- [ ] 1.5 In `frame_lighting.cpp`: after each pass dispatch, call `passInspector.RegisterTarget("pass.name", outputTexHandle, outputSrv)`.
- [ ] 1.6 Register GBuffer targets: albedo, normal, depth, velocity, emissive (in frame_setup or gbuffer pass).
- [ ] 1.7 Register shadow targets: atlas, per-cascade, result.
- [ ] 1.8 Register GTAO targets: raw, filtered.
- [ ] 1.9 Register SSGI targets: prev_color, trace, denoised, temporal, result, ao.
- [ ] 1.10 Register SSR targets: trace, resolved.
- [ ] 1.11 Register fog, bloom, exposure targets.
- [ ] 1.12 Register HDR scene color (pre-tonemap) and final (post-tonemap).

### Console Command
- [ ] 1.13 Register `vis` console command in `ConsoleCommandRegistry` — parses target name, sets vis mode.
- [ ] 1.14 `vis off` / `vis final` resets to normal rendering.
- [ ] 1.15 Invalid target name logs available targets: `[vis] Unknown target 'foo'. Available: gbuffer.albedo, gbuffer.normal, ...`

### Display Modes (in vis_fullscreen.hlsl)
- [ ] 1.16 RGBA color: direct display (clamp [0,1] or tonemap HDR).
- [ ] 1.17 Grayscale: single channel R displayed as (R,R,R).
- [ ] 1.18 Depth: linearize reversed-Z depth, map [near..far] to [1..0] grayscale.
- [ ] 1.19 HDR false-color: values >1.0 shown in hot colors (green→yellow→red→white).
- [ ] 1.20 Normals: decode octahedral, map [-1,1] to [0,1] RGB.
- [ ] 1.21 Velocity: RG motion vectors visualized as color (red=right, green=down, blue=magnitude).

### CLI Integration
- [ ] 1.22 `--vis <target>` flag: sets vis mode from command line (works with `--screenshot-stdout`).
- [ ] 1.23 Example: `demo.exe --vis ssgi.trace --screenshot-stdout --screenshot-frame 120 --frames 125`

## Phase 2: `pass.stats` + `pass.validate` — Automated Diagnosis

### Per-Pass Readback
- [ ] 2.1 Create readback ring buffer: small staging buffer (4x4 texels per pass × N passes).
- [ ] 2.2 After each pass dispatch, copy a 4x4 block from the output center to the readback buffer.
- [ ] 2.3 On the NEXT frame, map the readback and compute min/max/avg of the 16 texels.
- [ ] 2.4 Store per-pass stats in PassInspector: `struct PassStats { float min, max, avg; float gpuTimeMs; }`.

### GPU Timing
- [ ] 2.5 Create timestamp query heap (2 queries per pass: begin + end).
- [ ] 2.6 Bracket each pass with `BeginTimestamp` / `EndTimestamp`.
- [ ] 2.7 Read back timestamps and compute per-pass GPU time in ms.

### Console Commands
- [ ] 2.8 `pass.stats` — prints per-pass timing + output stats to console.
- [ ] 2.9 `pass.stats <name>` — detailed stats for one pass.
- [ ] 2.10 `pass.validate` — checks all passes for anomalies (zeros, NaN, Inf, all-same-value). Returns exit code 1 if any FAIL.
- [ ] 2.11 `pass.validate <name>` — validates a specific pass.

### Overlay
- [ ] 2.12 Render pass stats as screen overlay (text) when `pass.stats` is active — similar to the existing FPS/GPU stats overlay.

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
