<!-- doc-meta
title: Rendering Debug & Diagnostic Infrastructure — Complete Plan
category: analysis
created: 2026-03-30
status: in-progress
-->

# Rendering Debug & Diagnostic Infrastructure — Complete Plan

## Problem Statement

The current LLM-driven development workflow for rendering is **slow and wasteful**:

1. **LLM edits shader code** to add `return float4(value, 0, 0, 1)` debug outputs
2. **Full rebuild** of runtime + demo (30-60 seconds)
3. **Screenshot capture** → visual inspection → guess at the problem
4. **Repeat 5-10 times** per issue, each cycle burning 1-2 minutes

This is the **opposite** of how AAA studios diagnose rendering bugs. Major engines (UE5, Unity, O3DE, Frostbite) use **non-intrusive diagnostic tools** that inspect GPU state **without modifying source code**.

UzEngine already has a **powerful but underutilized** diagnostic infrastructure. This plan organizes it into a systematic workflow.

---

## Current Infrastructure Audit

### What We Have (Production-Quality)

| System | CLI Flag | What It Does | Status |
|--------|----------|-------------|--------|
| DebugViewMode (13 modes) | `--view-mode <name>` | Fullscreen single-channel GBuffer view | **Working** |
| Overview Grid | `--view-mode overview` | 4x2 composite of all GBuffer channels | **Working** |
| All-Modes Capture | `--all-modes <file.png>` | 13-mode labeled grid in one PNG | **Working** |
| PassInspector Vis | `--vis <target>` | Fullscreen visualization of any named render target | **Working** |
| PassInspector Dump | `--pass-dump` | All registered targets as PNGs + INDEX.html | **Working** |
| PassInspector Validate | `--pass-validate` | Readback NaN/Inf/zero check on all targets | **Working** |
| PassInspector Compare | `--pass-compare <name>` | A/B side-by-side with/without a pass | **Working** |
| PassInspector Toggle | `--pass-toggle <name>` | Disable individual passes at runtime | **Working** |
| Screenshot Stdout | `--screenshot-stdout` | Base64 PNG to stdout (headless, no disk I/O) | **Working** |
| Stats Overlay | (always on in editor) | FPS, draw calls, memory, camera, LOD | **Working** |
| DX12 Debug Layer | `--gpu-validation` | Resource state validation | **Working** |
| DRED Breadcrumbs | `--gpu-breadcrumbs` | Device removal diagnostics | **Working** |
| GPU Profiler | `--gpu-profile` | Per-pass timestamp queries | **Working** |
| GPU Breadcrumb Markers | `UZ_GPU_BREADCRUMBS` define | Manual command list markers | **Working** |
| CVar Override | `--cvar name=value` | Override any CVar at startup | **Working** |

### What We Have (Partially Working)

| System | Status | Gap |
|--------|--------|-----|
| Shadow Debug Grid | `--shadow-debug 1` plumbed to GPU | **Shader is a stub** — all functions return pass-through |
| Shadow Atlas Readback | Code exists in `scene_debug.cpp` | **Disabled** — needs render graph integration |
| Vis Display Modes | 6 modes (Color/Gray/Depth/HDR/Normal/Velocity) | Missing: **Cascade index, Shadow value, Stencil** |

### What We Don't Have (Remaining Gaps)

| System | Impact | Industry Reference |
|--------|--------|-------------------|
| **JSON diagnostic output** | LLMs can't parse text logs | UE5 `DumpGPU` HTML viewer | **DONE — `--diag`** |
| Golden image regression pipeline | No automated visual regression detection | UE5 Gauntlet, O3DE Atom |
| Per-pass render target extraction | Can't extract ONE target without dumping ALL | UE5 `r.BufferVisualizationTarget` |
| CB value readback to JSON | Must edit shader to see CB values | UE5 `stat gpu` + CB dump |
| Pixel probe at coordinates | Can't check specific pixel without dump | O3DE pixel validation |
| Automated D3D12 error parsing | Debug layer output goes to `OutputDebugString`, not captured | Studios parse stderr in CI |

---

## The Plan: 4 Phases

### Phase 1: LLM-Optimized Diagnostic Workflow (Week 1)

**Goal**: Make existing tools the **default first step** for any rendering bug, eliminating the "edit shader → rebuild" loop.

#### 1.1 — Diagnostic Command Cheat Sheet for LLM Agents

Create a `.claude/rules/rendering-diagnostics.md` rule that enforces this diagnostic order:

```
BEFORE editing ANY shader or renderer C++ to diagnose a bug:

Step 1: Capture the overview grid
  demo.exe --view-mode overview --screenshot-stdout --frames 8
  → Which GBuffer channel looks wrong?

Step 2: Isolate the broken channel
  demo.exe --view-mode <channel> --screenshot-stdout --frames 8
  → Is the data correct in the GBuffer? (problem is before or after deferred lighting)

Step 3: Dump all pass targets
  demo.exe --pass-dump --frames 120
  → Open .screenshots/pass-dump-fN/INDEX.html — which pass produces wrong output?

Step 4: Validate all targets for anomalies
  demo.exe --pass-validate --frames 120
  → Any NaN, Inf, all-zero, or all-one targets?

Step 5: Compare with/without a pass
  demo.exe --pass-compare <suspect_pass> --screenshot-stdout --frames 120
  → Does disabling the pass fix the issue?

Step 6: Inspect CB values via RenderDoc capture (Phase 2)

ONLY after steps 1-5 fail to identify the problem should you edit shader code.
```

#### 1.2 — Shadow Debug Grid Implementation

The shadow debug grid shader (`shadow_debug.hlsli`) is a **stub**. Implement the full 3x3 grid:

| (0,0) Albedo | (1,0) Normal | (2,0) Shadow Atlas Raw |
|---|---|---|
| **(0,1) Shadow Value** | **(1,1) Final LIT** | **(2,1) Linear Depth** |
| (0,2) Cascade Index | (1,2) AO | (2,2) Roughness/Metallic |

This grid renders entirely in `deferred_lighting_ps.hlsl` using existing GBuffer SRVs — **no new passes, no new textures, no render graph changes**. Just a UV-based tile selector that samples different textures per tile.

#### 1.3 — Single-Target Extraction Flag

Add `--pass-extract <target_name> <output.png>` — extracts ONE named target to a PNG without dumping all of them. Faster than `--pass-dump` for targeted diagnosis.

```bash
# Extract just the shadow atlas
demo.exe --pass-extract shadow.atlas shadow_atlas.png --frames 8

# Extract the bloom intermediate
demo.exe --pass-extract bloom.downsample bloom_down.png --frames 8
```

**Implementation**: PassInspector already registers targets by name. Add a mode that dumps only the matching target.

#### 1.4 — `--diag` JSON Diagnostic Command (IMPLEMENTED)

Single command that runs validation on all registered render targets and outputs structured JSON:

```bash
demo.exe --diag
# Output: DIAG_JSON:{"frame":120,"targetCount":24,"hasFailure":false,"hasWarning":true,"targets":[...]}
```

**Implementation**: `inspector_pass_report.cpp` + `DiagnosticReport::ToJSON()`.
Implies `--headless`, auto-sets 120 frame limit, suppresses console logging.
JSON prefix `DIAG_JSON:` mirrors `SCREENSHOT_BASE64:` pattern.

---

### Phase 2: CB Readback & Pixel Sampling (Week 2)

**Goal**: Inspect constant buffer values and specific pixel values **without external tools**.

#### 2.1 — CB Readback Pass

Add a render graph pass that reads back constant buffer contents after each major pass and logs them as JSON. Triggered by `--diag-cb` flag:

```bash
demo.exe --diag-cb
# Output: CB_JSON:{"pass":"shadow","cb0":{"lightDir":[0.5,-0.7,0.5],"bias":0.001,...}}
# Output: CB_JSON:{"pass":"lighting","cb0":{"ambientColor":[0.1,0.1,0.15],...}}
```

**Implementation**: Each pass already binds CBs via `SetGraphicsRootConstantBufferView()`. After bind, copy the mapped CPU data to a structured log. No GPU readback needed — CBs are CPU-visible.

#### 2.2 — Pixel Probe

Add `--probe <x> <y>` flag that reads back a single pixel from all registered targets:

```bash
demo.exe --probe 960 540 --diag
# Adds to DIAG_JSON: "probe":{"x":960,"y":540,"gbuffer.albedo":[0.8,0.2,0.1,1.0],...}
```

#### 2.3 — CB Diff Between Runs

Save diagnostic JSON to `logs/diagnostics/` with timestamps. Diff two runs:

```bash
demo.exe --diag > logs/diagnostics/before.json
# ... make a code change ...
demo.exe --diag > logs/diagnostics/after.json
# Parse both JSONs and compare target values
```

---

### Phase 3: Golden Image Regression Pipeline (Week 3)

**Goal**: Automatically detect visual regressions on every build.

#### 3.1 — Golden Image Baseline

Create a set of reference screenshots at known camera positions for each demo:

```
tests/golden/
  local-shadows/
    default.png           — default camera position
    altitude-700.png      — --camera-z 700
    altitude-1500.png     — --camera-z 1500
    overview.png          — --view-mode overview
    shadow-grid.png       — --shadow-debug 1
  pbr-showcase/
    default.png
    metallic.png          — --view-mode metallic
    roughness.png         — --view-mode roughness
```

#### 3.2 — Comparison Script

```bash
# scripts/visual-regression.sh
for test in tests/golden/*/; do
  demo=$(basename $test)
  for golden in $test/*.png; do
    name=$(basename $golden .png)
    # Capture current frame with same flags
    current=$(demos/$demo/zig-out/bin/$demo.exe --screenshot-stdout ...)
    # Compare
    psnr=$(magick compare -metric PSNR $golden $current /dev/null 2>&1)
    if (( $(echo "$psnr < 40" | bc -l) )); then
      echo "REGRESSION: $demo/$name PSNR=$psnr (threshold: 40)"
      exit 1
    fi
  done
done
```

#### 3.3 — CI Integration

- Run `visual-regression.sh` on every PR that touches `runtime/shaders/` or `runtime/engine/renderer/`
- Generate a diff heatmap image using `--diff` (already implemented in UzEngine)
- Post the heatmap as a PR comment if PSNR < 40

#### 3.4 — Deterministic Frame Rendering

For golden images to be comparable, the frame must be deterministic:
- Disable TAA jitter in test mode (`--cvar r.TAA.Enabled=0`)
- Fixed camera position (`--camera-x/y/z/yaw/pitch`)
- Fixed time-of-day if applicable (`--cvar r.TimeOfDay=12.0`)
- Disable any animation/motion (`--frames 1` after scene load)

---

### Phase 4: Structured Diagnostic Output for LLM Agents (Week 4)

**Goal**: Every diagnostic tool outputs **machine-parseable JSON** so LLM agents can reason about results without visual interpretation.

#### 4.1 — JSON Diagnostic Reporter

Add a `DiagnosticReporter` class that collects results from all diagnostic systems and outputs structured JSON:

```json
{
  "engine_version": "0.9.0",
  "backend": "dx12",
  "shader_model": "sm6.6",
  "gpu": "NVIDIA RTX 3080",
  "frame": 5,
  "resolution": [1920, 1080],
  "passes": {
    "gbuffer": {"gpu_ms": 0.8, "status": "ok"},
    "shadow": {"gpu_ms": 1.2, "status": "ok"},
    "lighting": {"gpu_ms": 0.5, "status": "warning", "detail": "NaN in 3 pixels"}
  },
  "targets": {
    "gbuffer.albedo": {"min": [0.0, 0.0, 0.0], "max": [1.0, 0.95, 0.88], "has_nan": false},
    "shadow.atlas": {"min": [0.0], "max": [1.0], "all_zero": false, "all_one": false}
  },
  "validation_errors": [],
  "d3d12_errors": [],
  "screenshot_base64": "..."
}
```

#### 4.2 — LLM Agent Rule: Parse Before Guess

Update `.claude/rules/rendering-diagnostics.md` to require agents to **parse diagnostic JSON** before forming any hypothesis:

```
When a rendering bug is reported:
1. Run: demo.exe --diag quick --screenshot-stdout --frames 8
2. Parse the JSON output
3. Check: any targets with ALL_ZERO? → shadow/depth pass not writing
4. Check: any NaN? → division by zero in shader math
5. Check: any D3D12 errors? → resource state or descriptor issue
6. ONLY after parsing these results, form a hypothesis
7. NEVER skip to "let me try changing this value"
```

#### 4.3 — Diagnostic History Log

Save diagnostic results to `logs/diagnostics/` with timestamps. When debugging an issue across multiple attempts, the LLM can `diff` two diagnostic reports to see what changed:

```bash
# Before fix attempt:
demo.exe --diag quick > logs/diagnostics/2026-03-30_14-30_before.json

# After fix attempt:
demo.exe --diag quick > logs/diagnostics/2026-03-30_14-35_after.json

# Diff:
diff logs/diagnostics/*before.json logs/diagnostics/*after.json
```

---

## Implementation Priority

| Priority | Item | Effort | Status |
|----------|------|--------|--------|
| **P0** | 1.1 — Diagnostic rule for LLM agents | 1 hour | **DONE** — `.claude/rules/rendering-diagnostics.md` |
| **P0** | 1.4 — `--diag` JSON diagnostic command | 4 hours | **DONE** — `inspector_pass_report.cpp` + CLI flag |
| **P0** | 1.2 — Shadow debug grid (stub → real) | 4 hours | Pending |
| **P1** | 1.3 — Single-target extraction (`--pass-extract`) | 2 hours | Pending |
| **P1** | 2.1 — CB readback pass (`--diag-cb`) | 4 hours | Pending |
| **P1** | 4.1 — Extended JSON with GPU info + resolution | 2 hours | Pending |
| **P2** | 2.2 — Pixel probe (`--probe x y`) | 3 hours | Pending |
| **P2** | 3.1-3.2 — Golden image baseline + comparison | 8 hours | Pending |
| **P3** | 2.3 — CB diff between runs | 2 hours | Pending |
| **P3** | 3.3-3.4 — CI integration + determinism | 6 hours | Pending |

**Total estimated effort**: ~36 hours remaining across 3 phases.

---

## Expected Outcome

### Before (Current Workflow)
```
Bug reported → Edit shader → Rebuild (60s) → Screenshot → Wrong guess
→ Edit shader again → Rebuild (60s) → Screenshot → Still wrong
→ Edit shader AGAIN → Rebuild (60s) → Finally found it
Total: 5-10 minutes per diagnosis, 3+ rebuilds
```

### After (New Workflow)
```
Bug reported → --diag (8s) → JSON shows shadow.atlas WARN_ZERO
→ --pass-compare shadow (15s) → Confirms shadow pass is the problem
→ --vis shadow.atlas (5s) → Atlas is empty, depth not writing
→ Read shadow_depth_vs.hlsl → Fix the actual bug → One rebuild
Total: 30-60 seconds diagnosis, 1 rebuild
```

**10x faster diagnosis, fewer wasted rebuilds, more systematic root cause identification.**

---

## References

- UE5 Buffer Visualization: `r.BufferVisualizationTarget`, `r.BufferVisualizationDumpBaseFilename`
- UE5 Gauntlet: Automated rendering regression framework with headless capture
- UE5 RDG DumpGPU: `DumpGPU` console command — injects copy-to-readback after every RDG texture
- O3DE Atom: Per-pass golden image comparison in CI (Breakpoint 2 conference)
- Frostbite Frame Graph: DOT dump + per-edge resource copy for diagnostics (GDC 2017)
- Shader Printf via Structured Buffer: `therealmjp.github.io/posts/hlsl-printf/`
- D3D12 GPU-Based Validation: `learn.microsoft.com/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation`
- DRED: `microsoft.github.io/DirectX-Specs/d3d/DeviceRemovedExtendedData.html`
- Wicked Engine GPU Profiler: Always-on timestamp queries with on-screen overlay
- GPU-Driven Debug Text: `jorenjoestar.github.io/post/gpu_driven_text/`
