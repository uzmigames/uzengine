# Rendering Diagnostics — NEVER edit shaders to debug, use diagnostic tools FIRST

## Rule (applies to ALL agents touching rendering code)

When a rendering bug is reported (wrong colors, black screen, missing shadows, etc.),
you MUST use the built-in diagnostic tools BEFORE making ANY code changes.

**ABSOLUTELY FORBIDDEN:**
- Adding `return float4(value, 0, 0, 1)` to a shader to "see what's happening"
- Inserting debug `UZ_LOG` in the renderer to print CB values
- Guessing at the problem and changing shader constants
- "Trying things" without diagnostic data

## Diagnostic Sequence (follow this EXACT order)

### Step 1: Run `--diag` (5 seconds, one command, JSON output)

```bash
demo.exe --diag
```

Output: `DIAG_JSON:{...}` on stdout. Parse it:
- `hasFailure: true` → a target has NaN or Inf (shader math bug)
- `hasWarning: true` → a target is all zeros (pass not writing)
- Check each target's `status`: `OK`, `FAIL_NAN`, `FAIL_INF`, `WARN_ZERO`

This single command tells you WHICH render target is broken without any code changes.

### Step 2: Visualize the broken target (5 seconds)

```bash
# GBuffer overview — 4x2 grid of all channels
demo.exe --view-mode overview --screenshot-stdout --frames 8

# Specific channel
demo.exe --view-mode albedo --screenshot-stdout --frames 8
demo.exe --view-mode depth --screenshot-stdout --frames 8

# Any registered pass target by name
demo.exe --vis shadow.atlas --screenshot-stdout --frames 8
demo.exe --vis ssgi.trace --screenshot-stdout --frames 8
demo.exe --vis hdr --screenshot-stdout --frames 8
```

View the screenshot with the Read tool. Now you know WHERE the bug is visually.

### Step 3: Dump all pass targets (15 seconds)

```bash
demo.exe --pass-dump --frames 120
# Output: .screenshots/pass-dump-fN/ with PNGs + INDEX.html
```

Open the PNGs to find which pass produces wrong output.

### Step 4: Compare with/without a pass (10 seconds)

```bash
# Disable a suspected pass
demo.exe --pass-toggle bloom --screenshot-stdout --frames 120
demo.exe --pass-toggle ssgi --screenshot-stdout --frames 120
```

Does disabling the pass fix the bug? Now you know WHICH pass is the culprit.

### Step 5: Pixel-level GPU printf (10 seconds, zero rebuilds)

If you need to know the EXACT VALUE of GBuffer data at a specific pixel:

```bash
# uzengine.exe format (Config key-value CLI):
uzengine.exe --pixel-debug --pixel-debug-x 960 --pixel-debug-y 540 --project ... --editor

# DemoApplication format (positional args):
demo.exe --pixel-debug 960 540
```

Output: `PIXEL_DEBUG_JSON:{...}` on stdout. A compute post-pass reads GBuffer + HDR at the pixel:
- id 0: depth (reversed-Z)
- id 1: AO
- id 2-4: albedo RGB
- id 5-7: HDR color RGB (after lighting)
- id 8: roughness
- id 9: metallic
- id 10-12: emissive RGB

```json
{"pixel":[960,540],"frame":11,"recordCount":13,"records":[
  {"id":0,"type":"float","value":0.001753},
  {"id":1,"type":"float","value":1.000000},
  {"id":2,"type":"float","value":0.321569}
]}
```

**This replaces adding `return float4(value,0,0,1)` to shaders entirely.**

### Step 6: Validate specific targets

```bash
demo.exe --pass-validate --frames 120
# Logs: [PASS VALIDATE] target: OK/FAIL/WARNING (min, max, avg)
```

### Step 7: ONLY NOW read the code and fix the bug

After steps 1-6, you should know:
1. Which target is broken (from `--diag` JSON)
2. What the broken target looks like (from `--vis` or `--view-mode`)
3. Which pass produces the broken output (from `--pass-dump`)
4. Whether disabling a pass fixes it (from `--pass-toggle`)

Now read the specific pass code and fix the actual bug. One rebuild.

## Available Diagnostic CLI Flags (complete list)

| Flag | Purpose |
|------|---------|
| `--diag` | Run diagnostics, output JSON to stdout, headless, exit |
| `--pixel-debug <x> <y>` | GPU printf — read shader variable values at a pixel, JSON to stdout |
| `--view-mode <name>` | GBuffer visualization (lit/albedo/worldnormal/metallic/roughness/ao/depth/worldposition/lighting/shadows/wireframe/unlit/overview) |
| `--all-modes <file.png>` | All 13 view modes in one labeled grid PNG |
| `--vis <target>` | Fullscreen visualization of any named render target |
| `--pass-validate` | Readback NaN/Inf/zero check on all targets |
| `--pass-dump` | Dump all targets as PNGs + INDEX.html |
| `--pass-toggle <name>` | Disable a pass category at startup |
| `--pass-compare <name>` | A/B comparison with/without a pass |
| `--shadow-debug <N>` | Shadow debug modes (0=off, 1-8) |
| `--screenshot-stdout` | Base64 PNG to stdout (headless, no disk I/O) |
| `--headless` | Hidden window |
| `--gpu-validation` | DX12 debug layer + GPU-based validation |
| `--gpu-breadcrumbs` | DRED auto-breadcrumbs for device removal |
| `--gpu-profile` | Per-pass GPU timestamp queries |
| `--cvar name=value` | Override any CVar at startup |

## Parsing `--diag` JSON Output

The JSON is prefixed with `DIAG_JSON:` on a single stdout line. Structure:

```json
{
  "frame": 120,
  "targetCount": 24,
  "hasFailure": false,
  "hasWarning": true,
  "targets": [
    {"name": "gbuffer.albedo", "status": "OK", "min": 0.0, "max": 1.0, "avg": 0.45},
    {"name": "shadow.atlas", "status": "WARN_ZERO", "min": 0.0, "max": 0.0, "avg": 0.0}
  ],
  "timing": [
    {"name": "gbuffer", "gpuMs": 0.8},
    {"name": "shadow", "gpuMs": 1.2}
  ]
}
```

Status values: `OK`, `FAIL_NAN`, `FAIL_INF`, `WARN_ZERO`, `SKIPPED`.

## Why This Matters

| Old Workflow (FORBIDDEN) | New Workflow (REQUIRED) |
|--------------------------|------------------------|
| Edit shader → rebuild 60s → screenshot → wrong guess | `--diag` 5s → JSON shows shadow.atlas WARN_ZERO |
| Edit shader again → rebuild 60s → still wrong | `--vis shadow.atlas` 5s → atlas is empty |
| Edit shader AGAIN → rebuild 60s → finally found it | Read shadow_depth_vs.hlsl → fix the bug → 1 rebuild |
| **Total: 5-10 min, 3+ rebuilds** | **Total: 30-60s, 1 rebuild** |
