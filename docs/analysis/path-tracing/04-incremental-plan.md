<!-- doc-meta -->
title: Incremental Implementation Plan - 8 Steps
date: 2026-04-07
status: approved
<!-- end-doc-meta -->

# 4. Incremental Plan

> Implementation note: the shipped tracer uses DXR-inline pixel shaders, not this compute/analytic design; [08 reconciliation](08-implementation-reconciliation.md) is authoritative.

Per `.claude/rules/incremental-implementation.md`: build + screenshot after
**every** step. Do not proceed if a step fails. If a step fails 2-3 times,
delete and restart from scratch.

All steps run **inside the existing runtime** (`uzengine.exe`) on an
existing sample project. No separate demo. Toggle via
`--cvar r.PathTracer.Enable=1`. When on, the runtime skips GI, shadows, and
deferred lighting and the path tracer pass writes the lit result to
`hdrSceneColor` — everything downstream (bloom, exposure, tonemap) works
unchanged.

Standard verification command used throughout:

```bash
uzengine.exe --project sample/projects/pbr_showcase.uzproject --editor \
             --cvar r.PathTracer.Enable=1 --cvar r.PathTracer.MaxBounces=<n> \
             --screenshot-stdout --frames <N>
```

## Step 1 — Pipeline skeleton: solid color

Scope:
- Create `PathTracerPass` class + `path_tracer_cs.hlsl` + `path_tracer_resolve_cs.hlsl`
- Wire the enable toggle in `scene_renderer_init.cpp` / `scene_render_frame.cpp`
- Skip deferred lighting, shadows, GI when enable is on
- Shader writes `float4(1, 0, 1, 1)` to every pixel into `hdrSceneColor`

Verify: screenshot shows **magenta** scene with tonemap/bloom active.
Pass: full magenta frame. If you see the rasterized scene, the skip logic
is wrong — fix that before anything else.

## Step 2 — Primary rays + first-hit via GBuffer

Scope:
- Instead of tracing primary rays, reuse the GBuffer (depth, normal, material)
  as a free first hit — massive perf win and visually consistent geometry
- Output `float4(normal*0.5+0.5, 1)` as the first-frame result

Verify: screenshot shows the scene as a normal-buffer visualization.
Pass: matches the existing `--view-mode worldnormal` output.

## Step 3 — One-bounce diffuse from GBuffer first hit

Scope:
- From each GBuffer pixel, shoot ONE bounce ray into a cosine-weighted
  hemisphere (analytic scene intersection: only the sun + sky environment)
- Return `albedo * sky_color` (pure diffuse direct-sky bounce)
- Enable ping-pong accumulation with sample counter

Verify: screenshot at `--frames 120` and `--frames 1200`
Pass: noise visibly decreases between the two; image converges toward a
stable ambient-occlusion-like look.

## Step 4 — Multi-bounce + Russian roulette + direct sun

Scope:
- Loop up to `r.PathTracer.MaxBounces` (default 4)
- Sample sun at each bounce with shadow ray through analytic scene
  primitives (Step 7 adds real scene data; for now the "scene" is sun +
  sky + GBuffer first hit)
- Russian roulette past bounce 2

Verify: `--frames 2000`
Pass: soft shadows from sun, visible indirect brightening in shaded areas,
no fireflies > 10× mean luminance.

## Step 5 — GGX specular via existing brdf.hlsli

Scope:
- Replace the "glossy lerp hack" from the shadertoy reference with proper
  GGX VNDF sampling (`@ref Heitz 2018`) from `material/brdf.hlsli`
- Map `PBRMaterial` (metallic/roughness) to `{F0, albedo, spec tint}` via
  `pt_material.hlsli`

Verify: compare against a rasterized screenshot of the same scene.
Pass: metallic balls show sharp reflections of the sky, dielectrics show
Fresnel rim lighting, material identity matches rasterized output.

## Step 6 — Camera-cut accumulation reset

Scope:
- Detect view-matrix change in `PathTracerPass::Execute`
- Reset history on change, also on enable toggle + resolution change
- Add `r.PathTracer.ResetAccum` exec

Verify: fly the editor camera; stationary frames should re-converge, moving
frames should show live 1-spp noise.
Pass: no ghosting after camera stops; new view converges clean.

## Step 7 — Analytic scene primitives from ECS

Scope:
- Add `PtPrimitiveComponent` (sphere / quad / emissive quad)
- Gatherer fills a `StructuredBuffer<PtPrimitive>` each frame
- Shader iterates this buffer for every bounce (not just the first hit)
- First hit still comes from GBuffer for screen pixels; bounces can hit
  primitives too

Verify: author a Cornell box scene in the editor using primitive
components, toggle PT on.
Pass: color bleeding on walls, direct shadow from emissive ceiling quad,
smooth convergence over 2000 frames.

## Step 8 — Falcor PSNR parity (RULE 0)

Scope:
- `scripts/falcor/path_tracer_reference.py` renders the same scene via
  Mogwai `PathTracer.py`
- `scripts/falcor/compare_pt.py` computes PSNR at matching SPP

Pass gate: **PSNR ≥ 35 dB** per CLAUDE.md RULE 0. Below that, debug BRDF,
tonemap alignment, or light sampling — fix UzEngine, never Falcor.

---

## Global verification after every step

```bash
# Health check (NaN/Inf/zero)
uzengine.exe --project ... --editor --cvar r.PathTracer.Enable=1 --diag --frames 16

# Visual
uzengine.exe --project ... --editor --cvar r.PathTracer.Enable=1 \
             --screenshot-stdout --frames <N>

# Pass dump when something looks wrong
uzengine.exe --project ... --editor --cvar r.PathTracer.Enable=1 \
             --pass-dump --frames 16
```

Do NOT proceed to step N+1 until step N passes all three.

---

Next: See 05-cvars-and-controls.md
