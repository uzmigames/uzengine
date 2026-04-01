# GPU Debugging — Never Guess, Always Diagnose

When rendering produces wrong output, follow this exact diagnostic order.
The full step-by-step sequence is in `.claude/rules/rendering-diagnostics.md`.

## Quick Reference — Diagnostic Commands

```bash
# Step 1: Which render target is broken? (JSON report, auto-exits)
uzengine.exe --diag --project ./sample/projects/<name>.uzproject --editor

# Step 2: What are the exact GBuffer values at a pixel? (13 values, auto-exits)
uzengine.exe --pixel-debug --pixel-debug-x 960 --pixel-debug-y 540 --project ... --editor

# Step 3: Visualize a specific target
uzengine.exe --vis shadow.atlas --screenshot-stdout --frames 8 --project ... --editor

# Step 4: GBuffer overview grid (all channels at once)
uzengine.exe --view-mode overview --screenshot-stdout --frames 8 --project ... --editor

# Step 5: Dump ALL pass targets as PNGs
uzengine.exe --pass-dump --frames 120 --project ... --editor

# Step 6: Disable a suspected pass
uzengine.exe --pass-toggle bloom --screenshot-stdout --frames 120 --project ... --editor
```

## Parsing JSON Output

### `--diag` → `DIAG_JSON:{...}`

```json
{"frame":11,"targetCount":21,"hasFailure":false,"hasWarning":true,"targets":[
  {"name":"gbuffer.albedo","status":"OK","min":0.20,"max":1.00,"avg":0.53},
  {"name":"shadow.atlas","status":"WARN_ZERO","min":0.00,"max":0.00,"avg":0.00}
]}
```
- `hasFailure: true` → NaN or Inf in a target (shader math bug)
- `hasWarning: true` → a target is all zeros (pass not writing)
- Status: `OK`, `FAIL_NAN`, `FAIL_INF`, `WARN_ZERO`, `SKIPPED`

### `--pixel-debug` → `PIXEL_DEBUG_JSON:{...}`

```json
{"pixel":[960,540],"frame":11,"recordCount":13,"records":[
  {"id":0,"type":"float","value":0.001753},
  {"id":1,"type":"float","value":1.000000},
  {"id":2,"type":"float","value":0.321569}
]}
```
- id 0: depth (reversed-Z)
- id 1: AO
- id 2-4: albedo RGB
- id 5-7: HDR color RGB (after lighting)
- id 8: roughness
- id 9: metallic
- id 10-12: emissive RGB

## DX12 Validation Stack

```bash
# GPU-based validation (catches resource state, descriptor mismatches)
uzengine.exe --gpu-validation --project ... --editor

# DRED breadcrumbs (diagnoses device removal / TDR crashes)
uzengine.exe --gpu-breadcrumbs --project ... --editor

# GPU timestamp profiling (per-pass timing)
uzengine.exe --gpu-profile --project ... --editor
```

## ABSOLUTELY FORBIDDEN

- **Editing shader source for diagnostics** — use `--diag` / `--pixel-debug` / `--vis` instead
- Iterating blindly by changing shader code and hoping it works
- Guessing at parameter values to "fix" visual bugs
- Adding `return float4(value, 0, 0, 1)` debug outputs to shaders
- Adding `UZ_LOG` in the renderer to print CB values
- Compensation factors to make wrong output look right
- GPU readback outside the render graph (causes device removed / hang)

## Implementation Details

- `--diag`: `PassInspector::GenerateReport()` → readback 4x4 center sample from each target
- `--pixel-debug`: `pixel_debug_cs.hlsl` compute shader, [numthreads(1,1,1)] dispatch, reads GBuffer via `ResourceDescriptorHeap[idx]`
- `--vis`: `vis_fullscreen_ps.hlsl` with 6 display modes (Color, Grayscale, Depth, HDR, Normal, Velocity)
- `--pass-dump`: copies all registered targets to PNG + INDEX.html gallery
- All diagnostic flags auto-exit after 120 frames to prevent process hangs
