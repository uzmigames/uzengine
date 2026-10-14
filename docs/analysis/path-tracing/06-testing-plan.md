<!-- doc-meta -->
title: Testing Plan - CPU Parity, Visual Regression, Falcor PSNR
date: 2026-04-07
status: approved
<!-- end-doc-meta -->

# 6. Testing Plan

> Implementation note: the shipped tracer uses DXR-inline pixel shaders, not this compute/analytic design; [08 reconciliation](08-implementation-reconciliation.md) is authoritative.

Per `.claude/rules/shader-testing-pipeline.md` — 4 tiers, all mandatory.

## Tier 1 — CPU parity tests

Every non-trivial HLSL helper gets a C++ mirror + GoogleTest. Location:
`runtime/tests/engine/renderer/test_path_tracer_parity.cpp`.

| HLSL function | C++ mirror | Test case |
|---------------|------------|-----------|
| `WangHash` | `pt_rng.h::WangHash` | Known seed produces known sequence (fixture: seed=1, expect 5 hashed values matching a hand-computed table). |
| `RandUnitVector` | `pt_rng.h::RandUnitVector` | Monte Carlo mean over 1e6 samples → ‖mean‖ < 3/√N. Variance matches uniform sphere. |
| `IntersectSphere` | `pt_geom.h::IntersectSphere` | Known-value: ray (0,0,0)→(0,0,1), sphere (0,0,5,r=1) → dist=4, normal=(0,0,-1). Boundary: tangent hit. Miss: perpendicular. Inside: origin inside sphere. |
| `IntersectQuad` | `pt_geom.h::IntersectQuad` | XZ quad at y=0, ray from (0,1,0) down → dist=1, normal=(0,1,0). Edge/corner hits. Back-face (ray from below) either hits or flips normal — match shader behavior exactly. |
| `CosineWeightedDir` | `pt_sampling.h::CosineWeightedDir` | PDF check: p(ω) = cosθ/π, Monte Carlo integral of 1/pdf over hemisphere → 2π within 1%. |
| Throughput RR loop (pure-math extraction) | `pt_rr.h::RussianRoulette` | Unbiased-estimator test: over 1e6 runs mean throughput preservation within 1%. |

Tolerance: `1e-4` for intersection math, `1e-3` for Monte Carlo means at
N=1e6 samples.

Run: `cd runtime && zig build test` — must pass before any commit that
touches path tracer HLSL.

## Tier 2 — Visual regression

Golden PNGs stored under `.sandbox/golden/path_tracer/`:

| Golden | Scene | Frames | Notes |
|--------|-------|--------|-------|
| `primary_rays.png` | sphere + plane | 1 | Step 2 smoke test. |
| `cornell_1bounce.png` | cornell box, 1 bounce | 500 | Step 3. |
| `cornell_4bounce.png` | cornell box, 4 bounces | 2000 | Step 4, color bleeding. |
| `ggx_row.png` | 5 spheres varying roughness | 2000 | Step 5. |
| `showcase.png` | full demo scene | 4000 | Step 8 Falcor comparison input. |

Capture: `path_tracer.exe --screenshot <path> --screenshot-frame N --frames N+8`.
Compare: `uzengine.exe --diff golden.png captured.png` → PSNR report.

Thresholds: ≥40 dB PASS, 30-40 dB investigate, <30 dB fail.

Goldens updated explicitly when visual behavior changes intentionally —
`scripts/visual_test.ps1 --update-golden path_tracer`.

## Tier 3 — FLIP perceptual A/B

Used when comparing two algorithm variants (e.g. old "glossy lerp" vs new
VNDF sampling in Step 5). Command:
`uzengine.exe --flip ref.png test.png --flip-output diff.png`.

Thresholds from `shader-testing-pipeline.md`: mean <0.02 imperceptible,
<0.05 acceptable, >0.10 investigate.

## Tier 4 — Falcor path-traced ground truth

Ultimate quality gate per CLAUDE.md RULE 0.

Setup script: `scripts/falcor/path_tracer_reference.py`

```python
import falcor
m.loadScene("../../sample/scenes/path_tracer_showcase.pyscene")
g = RenderGraph("PTReference")
g.addPass(createPass("PathTracer", {"samplesPerPixel": 1,
                                     "maxBounces": 4}), "PT")
g.addPass(createPass("AccumulatePass", {"enabled": True}), "Accum")
g.addPass(createPass("ToneMapper"), "Tonemap")
g.markOutput("Tonemap.dst")
m.addGraph(g)
m.clock.pause()
for frame in range(4000): m.renderFrame()
m.frameCapture.outputDir = "../../.sandbox/falcor_reference/path_tracer/"
m.frameCapture.capture()
```

Comparison: `python scripts/falcor/compare_pt.py` → emits PSNR + FLIP.

Pass gate: **PSNR ≥ 35 dB** against Falcor at matching SPP (RULE 0). Failure
means a math bug somewhere in BRDF, tonemap alignment, RNG correlation, or
light sampling — fix UzEngine to match Falcor, never the reverse.

Run cadence: after every Step 5/7/8 change, and before every PR that touches
`path_tracer_cs.hlsl` or `pt_brdf.hlsli`.

## Tier 5 (optional) — Determinism

Because Wang-hash seeding is `(pixel, frameIndex)`, the same frame index
from cold start MUST produce a bit-identical image. Test:
`path_tracer.exe --screenshot a.png --screenshot-frame 500` twice and diff.
PSNR must be infinity (bit-identical). Catches any hidden non-determinism
(e.g. CB aliasing, uninitialized history).

---

Next: See 07-risks-and-open-questions.md
