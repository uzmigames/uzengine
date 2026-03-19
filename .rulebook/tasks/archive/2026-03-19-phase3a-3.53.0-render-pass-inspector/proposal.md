# Proposal: Render Pass Inspector — Visual GPU Pipeline Debugger

## Why

UzEngine has zero visibility into intermediate render targets. When a pass outputs zeros (like SSGI trace currently), the only way to find the problem is guessing, adding temporary debug code, rebuilding, and iterating blindly. This wastes hours.

Professional engines solve this with built-in visual debugging:
- **Unity Frame Debugger**: hierarchical pass list, click to see any RT
- **UE5 `vis` command**: `vis SceneColor`, `vis GBufferA`, etc.
- **Godot**: buffer visualization in editor viewport
- **RenderDoc**: external but integrated via capture API

UzEngine needs an equivalent system that works from the editor (console commands + IPC) and from CLI (headless automation for CI/LLM workflows).

## What This Enables

1. **Instant bug diagnosis**: see which pass produces black/wrong output visually
2. **A/B comparison**: toggle any pass on/off and see the visual delta
3. **Performance profiling**: see each pass's contribution to the final image
4. **LLM workflow**: Claude can capture all intermediate RTs as PNGs and analyze them
5. **CI regression testing**: automated per-pass screenshot comparison between commits

## Design

### Editor Console Commands (runtime, no rebuild needed)

All commands are typed in the in-game console (`~` key) or sent via IPC from VSCode extension.

#### 1. `vis <target>` — Fullscreen Buffer Visualization
Replaces the final output with a single intermediate buffer, fullscreen.

```
vis gbuffer.albedo          — Base color (RGB)
vis gbuffer.normal          — World normals (RGB from octahedral)
vis gbuffer.metallic        — Metallic (grayscale)
vis gbuffer.roughness       — Roughness (grayscale)
vis gbuffer.ao              — Ambient occlusion (grayscale)
vis gbuffer.depth           — Linear depth (grayscale, near=white far=black)
vis gbuffer.velocity        — Motion vectors (RG, visualized as color)
vis gbuffer.emissive        — Emissive (HDR, tonemapped for display)

vis shadow.atlas            — Shadow map atlas (depth)
vis shadow.cascade0..3      — Individual CSM cascade
vis shadow.result           — Final shadow factor (white=lit, black=shadow)

vis lighting.direct         — Direct lighting only (no ambient)
vis lighting.ambient        — Ambient/IBL only (no direct)
vis lighting.specular       — Specular IBL only
vis lighting.diffuse        — Diffuse IBL only (SH irradiance)

vis ssgi.prev_color         — Previous frame scene color (SSGI input)
vis ssgi.trace              — Raw SSGI trace output (before denoise)
vis ssgi.denoised           — Spatially denoised SSGI
vis ssgi.temporal           — Temporally accumulated SSGI
vis ssgi.result             — Final upsampled SSGI
vis ssgi.ao                 — SSGI ambient occlusion mask

vis gtao.raw                — Raw GTAO output
vis gtao.filtered           — Filtered GTAO

vis ssr.trace               — Raw SSR trace
vis ssr.resolved            — Resolved SSR

vis fog.inscattering        — Fog inscattering color
vis fog.opacity             — Fog opacity mask

vis bloom.mip0..5           — Individual bloom mip levels
vis bloom.result            — Combined bloom

vis exposure.histogram      — Auto-exposure histogram (bar chart overlay)
vis exposure.value          — Current exposure value (number overlay)

vis hdr                     — HDR scene color (before tonemap, with false-color for >1.0)
vis final                   — Final output (default, normal rendering)
vis off                     — Disable visualization (same as vis final)
```

#### 2. `pass.dump [frame]` — Dump All Passes as PNGs
Captures every intermediate RT at the specified frame (default: current frame).

```
pass.dump              — Dump current frame
pass.dump 120          — Dump at frame 120 (headless mode)
```

Output directory: `.screenshots/pass-dump-f{N}/`
```
.screenshots/pass-dump-f120/
├── 00-gbuffer-albedo.png
├── 01-gbuffer-normal.png
├── 02-gbuffer-depth.png
├── 03-gbuffer-velocity.png
├── 04-gbuffer-emissive.png
├── 05-shadow-atlas.png
├── 06-shadow-result.png
├── 07-gtao-raw.png
├── 08-gtao-filtered.png
├── 09-ssgi-prev-color.png
├── 10-ssgi-trace.png
├── 11-ssgi-denoised.png
├── 12-ssgi-temporal.png
├── 13-ssgi-upsample.png
├── 14-ssgi-ao.png
├── 15-ssr-trace.png
├── 16-ssr-resolved.png
├── 17-deferred-lit.png
├── 18-aerial-perspective.png
├── 19-fog.png
├── 20-bloom-result.png
├── 21-tonemap-final.png
└── INDEX.html           ← Browser viewer with side-by-side comparison
```

#### 3. `pass.toggle <name>` — Enable/Disable Individual Passes
Toggles a render pass on/off to see its visual contribution.

```
pass.toggle ssgi           — Toggle SSGI on/off
pass.toggle gtao           — Toggle GTAO on/off
pass.toggle ssr            — Toggle SSR on/off
pass.toggle fog            — Toggle height fog on/off
pass.toggle bloom          — Toggle bloom on/off
pass.toggle contact_shadow — Toggle contact shadows on/off
pass.toggle aerial_persp   — Toggle aerial perspective on/off
pass.toggle tonemap        — Toggle tonemapping (show raw HDR)
pass.toggle taa            — Toggle TAA on/off
```

#### 4. `pass.compare <name>` — Side-by-Side A/B Compare
Shows the scene with and without a specific pass, split-screen.

```
pass.compare ssgi          — Left half: without SSGI, Right half: with SSGI
pass.compare gtao          — Left: no AO, Right: with AO
pass.compare fog           — Left: no fog, Right: with fog
```

#### 5. `pass.grid` — All Passes Grid View
Shows a grid of ALL major render targets simultaneously (extension of existing `--shadow-debug 1`).

```
pass.grid                  — Show 4x4 grid of 16 key buffers
pass.grid extended         — Show 6x4 grid of 24 buffers (includes SSGI, SSR, bloom mips)
```

#### 6. `pass.stats` — Per-Pass GPU Timing + Readback Stats
Shows timing and min/max/avg for each pass's output.

```
pass.stats                 — Show overlay with per-pass GPU time and output stats
pass.stats ssgi            — Show detailed SSGI stats only
```

Output (overlay or console):
```
Pass                    GPU Time    Output Min    Max       Avg
────────────────────────────────────────────────────────────────
GBuffer                 0.31ms      —             —         —
ShadowMap               0.18ms      0.000         1.000     0.456
GTAO                    0.22ms      0.000         1.000     0.723
SSGI_PrevColorReduce    0.05ms      0.000         3.450     0.340
SSGI_Trace              0.42ms      0.000         1.230     0.085
SSGI_SpatialDenoise     0.08ms      0.000         0.980     0.079
SSGI_TemporalAccum      0.06ms      0.000         0.950     0.081
SSGI_Upsample           0.04ms      0.000         0.920     0.078
SSR_Trace               0.35ms      0.000         5.200     0.120
DeferredLighting        0.28ms      0.000        12.500     1.230
HeightFog               0.15ms      0.000         0.950     0.120
Bloom                   0.18ms      0.000         2.300     0.045
Tonemap                 0.12ms      0.000         1.000     0.450
Total                   2.44ms
```

#### 7. `pass.validate` — Automated Pass Validation
Runs all passes and checks for anomalies (zeros, NaN, Inf, all-same-value).

```
pass.validate              — Check all passes
pass.validate ssgi         — Check SSGI pipeline only
```

Output:
```
[PASS VALIDATE] GBuffer.Albedo: OK (min=0.02, max=0.95, avg=0.41)
[PASS VALIDATE] GBuffer.Normal: OK (min=-1.0, max=1.0, avg=0.12)
[PASS VALIDATE] SSGI.PrevColor: WARNING — all zeros (prev frame not captured?)
[PASS VALIDATE] SSGI.Trace: FAIL — all zeros (no ray hits, check prev color input)
[PASS VALIDATE] SSGI.Denoised: FAIL — all zeros (input was zeros)
[PASS VALIDATE] DeferredLit: OK (min=0.0, max=8.5, avg=0.89)
```

### CLI Flags (headless / CI mode)

```bash
# Dump all passes at frame 120
demo.exe --pass-dump 120 --frames 125

# Dump specific buffer as base64 to stdout
demo.exe --vis ssgi.trace --screenshot-stdout --screenshot-frame 120 --frames 125

# Run pass validation and exit with error code if any FAIL
demo.exe --pass-validate --frames 120

# Compare with/without a pass
demo.exe --pass-compare ssgi --screenshot e:/out/compare.png --screenshot-frame 120 --frames 125
```

### IPC Protocol (VSCode Extension)

```json
// Request: visualize a buffer
{"method": "render.vis", "params": {"target": "ssgi.trace"}}

// Request: dump all passes
{"method": "render.passDump", "params": {"frame": 120}}

// Request: toggle a pass
{"method": "render.passToggle", "params": {"pass": "ssgi"}}

// Request: get pass stats
{"method": "render.passStats", "params": {}}
// Response: {"passes": [{"name": "SSGI_Trace", "gpuTimeMs": 0.42, "outputMin": 0.0, ...}]}

// Request: validate passes
{"method": "render.passValidate", "params": {}}
// Response: {"results": [{"pass": "SSGI.Trace", "status": "FAIL", "reason": "all zeros"}]}
```

### VSCode Extension UI (Future)

- **Render Passes panel**: tree view of all passes with enable/disable checkboxes
- **Buffer Viewer**: click any pass → shows its output in a webview panel
- **Performance overlay**: bar chart of per-pass GPU timing
- **Regression diff**: compare pass outputs between git commits

## Impact
- Affected code: `runtime/engine/renderer/` (new pass inspector subsystem), console command registry, IPC handlers
- New files: `pass_inspector.h/.cpp`, `vis_renderer.h/.cpp`, readback infrastructure
- Performance: ZERO overhead when disabled. When `vis` is active, one extra copy per frame. When `pass.dump`, one-frame stall for readback.
- Breaking change: NO (additive feature)

## Implementation Priority
1. `vis <target>` — immediate value for debugging (solves the SSGI zeros problem NOW)
2. `pass.stats` + `pass.validate` — automated diagnosis
3. `pass.dump` — full pipeline snapshot
4. `pass.toggle` + `pass.compare` — A/B comparison
5. `pass.grid` — extended buffer grid
6. IPC protocol — VSCode integration
7. CLI flags — CI automation
