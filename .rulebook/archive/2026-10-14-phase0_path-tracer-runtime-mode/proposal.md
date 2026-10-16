# Proposal: Compute Path Tracer Runtime Mode (Phase 0)

## Why

UzEngine's lighting pipeline (deferred + Brixelizer GI + cascaded shadow maps + IBL) is highly optimized for real-time but cannot serve as a ground-truth reference for material/lighting validation. There is no way to ask the engine "what does this scene actually look like with full multi-bounce global illumination?" without leaving the runtime and using Falcor.

A **runtime path tracer mode** — toggleable via `r.PathTracer.Enable=1` — gives users a one-flag switch to render the same scene with progressive Monte Carlo path tracing inside `uzengine.exe`. Use cases: BRDF/material validation, lightmap/IBL ground truth, screenshot-quality renders, art-direction reviews, and the in-engine baseline for the Falcor PSNR comparison required by CLAUDE.md RULE 0.

The path tracer is **pure compute, analytic scene** (Shadertoy-style), NOT DXR-based. It reuses the existing GBuffer for the first hit and iterates analytic primitives (spheres/quads/emissive lights) plus the sky cubemap for bounces. No BLAS/TLAS, no DXR hardware requirement.

Full design: `docs/analysis/path-tracing/` (8 documents).

## What Changes

### New Systems

1. **PathTracerPass** (`runtime/engine/renderer/passes/pathtracing/path_tracer_pass.h/.cpp`) — owns ping-pong RGBA32F accumulation textures, two compute pipelines (trace + resolve), camera-cut detection, history reset. Replaces deferred lighting / GI / shadow passes when `r.PathTracer.Enable=1`.

2. **Path tracer compute shaders** (`runtime/shaders/runtime/pathtracing/`):
   - `path_tracer_cs.hlsl` — Monte Carlo trace, GBuffer first hit, multi-bounce loop, Russian roulette, Wang-hash RNG, GGX VNDF + cosine diffuse.
   - `path_tracer_resolve_cs.hlsl` — accumulation blend, write to `hdrSceneColor`.
   - `include/pt_common.hlsli`, `pt_scene.hlsli`, `pt_brdf.hlsli`, `pt_material.hlsli`.

3. **PtPrimitiveComponent** (`runtime/engine/ecs/pathtracing/pt_primitive_component.h`) — analytic spheres / quads / emissive quads authored in the editor; gathered into a `StructuredBuffer<PtPrimitive>` per frame.

4. **Lua bindings** (`runtime/engine/scripting/api/lua_pathtracer.cpp`) — `uz.pathtracer.{enable, is_enabled, set_max_bounces, set_samples_per_frame, reset_accum, samples_accumulated}`.

5. **Falcor reference** (`scripts/falcor/path_tracer_reference.py`, `scripts/falcor/compare_pt.py`) — Mogwai render graph + PSNR comparison for RULE 0 parity gate.

### Refactoring

- `runtime/engine/renderer/scene/scene_render_frame.cpp` — top-level branch on `cv_PtEnable`: when true, skip shadow passes / GI passes / deferred lighting and dispatch `PathTracerPass` instead.
- `runtime/engine/renderer/scene/scene_renderer_init.cpp` — construct/destroy `PathTracerPass`, gate transient resource allocation for skipped passes.
- `runtime/engine/renderer/core/params/constant_buffers.h` — add `PathTracerConstants` struct.
- `runtime/engine/renderer/frame/frame_lighting.cpp`, `frame_gi_passes.cpp`, `frame_shadows.cpp` — early-out when PT enabled.
- `runtime/engine/scripting/lua_vm.cpp` — register `uz.pathtracer` table.
- `runtime/tests/engine/renderer/test_path_tracer_parity.cpp` — new CPU parity tests.

### CVars

- `r.PathTracer.Enable` (bool, default 0) — master toggle, **forces `r.TAA=0` and disables upscalers when on**
- `r.PathTracer.MaxBounces` (int, default 4, range 1-16)
- `r.PathTracer.SamplesPerFrame` (int, default 1, range 1-16)
- `r.PathTracer.RussianRoulette` (bool, default 1)
- `r.PathTracer.RussianRouletteMinBounce` (int, default 2)
- `r.PathTracer.EnvIntensity` (float, default 1.0)
- `r.PathTracer.ClampFirefly` (float, default 10.0)
- `r.PathTracer.JitterAA` (bool, default 1)
- `r.PathTracer.Accumulate` (bool, default 1)
- `r.PathTracer.ResetAccum` (exec) — manual history reset

## Impact

- **Affected specs**: `docs/specs/rendering/renderer.md` (new section), new `docs/specs/rendering/path-tracer.md`
- **Affected code**: `runtime/engine/renderer/passes/pathtracing/`, `runtime/engine/renderer/scene/`, `runtime/engine/renderer/frame/`, `runtime/shaders/runtime/pathtracing/`, `runtime/engine/ecs/pathtracing/`, `runtime/engine/scripting/api/`
- **Breaking change**: NO — additive feature, default off. Rasterized pipeline behavior unchanged when `r.PathTracer.Enable=0`.
- **Memory cost**: ~64 MiB at 1080p (two RGBA32F accumulation textures) — only allocated when feature is enabled.
- **User benefit**: One-flag ground-truth renderer inside the same exe. Material validation, IBL/lighting reference, screenshot quality, Falcor PSNR baseline.
- **Enables**: Falcor parity gate (RULE 0), future denoiser milestone, future BVH-meshes milestone.
- **Risks**: TAA/upscaler interaction (auto-disabled), CB ring stale-buffer race (tracked separately), firefly clamping bias (small, standard practice).

## Out of Scope (separate milestones)

- Triangle mesh path tracing (requires BVH builder — open question Q2 in `docs/analysis/path-tracing/07-risks-and-open-questions.md`)
- SVGF / OIDN denoiser
- Bidirectional path tracing / MIS / NEE for multiple area lights
- Spectral rendering
- Volumetrics inside the path tracer (existing volumetric fog stays in rasterized path only)
