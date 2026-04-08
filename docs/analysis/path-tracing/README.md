<!-- doc-meta -->
title: Compute-Based Progressive Path Tracer Implementation Plan
date: 2026-04-07
status: draft
<!-- end-doc-meta -->

# Path Tracer Implementation Plan — Index

This directory contains a comprehensive plan for adding a **compute-based progressive path tracer** to UzEngine, enabling film-quality offline rendering via HLSL compute shaders.

## Quick Overview

The path tracer is a **runtime toggle inside `uzengine.exe`** — flip
`r.PathTracer.Enable=1` and the renderer swaps its lighting pipeline:

- **Replaces** deferred lighting, GI (Brixelizer/DDGI/probes), and shadow passes
- **Runs as compute** in `PathTracerPass`, one thread per pixel
- **Reuses GBuffer** for the first hit (depth + normal + material) — no custom primary-ray intersection
- **Analytic scene** primitives (spheres/quads/emissive lights) via a new `PtPrimitiveComponent`
- **Progressive accumulation**: ping-pong RGBA32F history, Wang-hash RNG, resets on camera cut
- **BRDF**: reuses existing `material/brdf.hlsli` — GGX + Burley diffuse + Schlick Fresnel
- **HDR output** goes to the same `hdrSceneColor` target — bloom / exposure / tonemap / AA **unchanged**
- **Sky** still comes from the existing atmosphere pass cubemap

NOT a separate executable. NOT a demo. NOT using DXR BLAS/TLAS. Pure compute path tracer as a runtime mode.

## Audience

- **Rendering engineers** implementing the tracer
- **Architects** reviewing integration points
- **QA** verifying per-step incremental tests

## Files in This Plan

| File | Purpose |
|------|---------|
| **01-current-state.md** | What exists today: RT infrastructure, compute passes, scene data (file:line citations) |
| **02-architecture.md** | Proposed class layout, CB structure, root signature, frame-graph insertion |
| **03-shader-design.md** | HLSL design: entry point, RNG, BRDF, bounce loop, Russian roulette, accumulation |
| **04-incremental-plan.md** | 8-step implementation with visual verification (per incremental-implementation.md) |
| **05-cvars-and-controls.md** | Runtime CVars (enable, max bounces, SPP, etc.) and registration pattern |
| **06-testing-plan.md** | CPU parity tests, visual regression, Falcor PSNR thresholds |
| **07-risks-and-open-questions.md** | Known blockers and unresolved questions |

## Key Decisions

1. **Runtime toggle** — `r.PathTracer.Enable` skips GI/lighting/shadows and runs `PathTracerPass` instead. Same `uzengine.exe`, same scenes, same downstream post-process.
2. **Falcor baseline** — PSNR ≥ 35 dB vs. Falcor path-traced reference (CLAUDE.md RULE 0)
3. **Incremental steps** — no full implementation at once; Step 1 is solid magenta
4. **Material mapping** — UzEngine `PBRMaterial` (metallic/roughness) maps to the reference `{F0, albedo, spec tint}` inside `pt_material.hlsli`
5. **Scene** — GBuffer for first hit + analytic `PtPrimitiveComponent` for bounces. No DXR BLAS/TLAS.
6. **Accumulation** — ping-pong RGBA32F history, reset on camera cut (view matrix diff)
7. **Entry point** — shader MUST be named `main()` (UzEngine convention)

## Next Steps

1. Read **01-current-state.md** to verify RT infrastructure and compute patterns
2. Review **02-architecture.md** for class design and resource binding strategy
3. Follow **04-incremental-plan.md** step-by-step before writing any code
4. Use **06-testing-plan.md** to validate each step (CPU tests + visual)

---

**Status**: DRAFT — under review. Final approval pending architectural sign-off.
