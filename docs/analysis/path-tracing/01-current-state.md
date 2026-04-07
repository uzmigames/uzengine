<!-- doc-meta -->
title: Current State - What UzEngine Already Provides
date: 2026-04-07
status: draft
<!-- end-doc-meta -->

# 1. Current State

Scope note: this is a **pure path tracer** (Shadertoy-style Monte Carlo in a
compute shader with analytic scene primitives), NOT a DXR ray-tracing
renderer. The existing `runtime/engine/renderer/raytracing/` DXR stack is
intentionally ignored — it's a separate feature.

## 1.1 Compute pass reference — GTAO

File: `runtime/engine/renderer/passes/postprocess/gtao_pass.{h,cpp}`

The GTAO pass is the cleanest template for a new compute pass:

- Lifecycle: `Init(device, shaderMgr, width, height)` → per-frame `Execute(...)` → `Shutdown()`
- Members: non-owning `m_device`, `m_shaderMgr`; owned GPU textures, pipelines, SRV/UAV indices
- Shader loaded via `ShaderManager::LoadComputeShader("postprocess/gtao_cs.hlsl", "main")`
- Root signature: root constants at `b0` (16 DWORDs / 64 bytes) + bindless heap
- Dispatch: `[numthreads(8,8,1)]`, groups = `((w+7)/8, (h+7)/8, 1)`

Other compute references worth skimming: `svgf_pass.cpp` for ping-pong
history management, `taa_upscaler.cpp` for camera-cut detection and history
invalidation.

## 1.2 Shader common libraries (reusable)

`runtime/shaders/runtime/common/`:
- `constants.hlsli` — `PI`, `TWO_PI`, `M_1_PI`, `PT_FAR`, tiny-epsilon constants
- `sampling.hlsli` — `SampleCosineHemisphere(u, pdf)`, `RadicalInverseVdC`, `Hammersley`
- `math.hlsli` — basic vector helpers
- `bindless.hlsli` — `ResourceDescriptorHeap[]` accessors
- `encoding.hlsli` — octahedral normal encode/decode

`runtime/shaders/runtime/material/`:
- `brdf.hlsli` — `D_GGX`, `G_SmithGGXCorrelated`, `F_Schlick` with full citations
- `diffuse.hlsli` — Lambert, Burley diffuse
- `specular.hlsli` — specular helpers

The path tracer reuses all of these — we add only path-tracer-specific
files (RNG, scene intersection, bounce loop).

## 1.3 HDR scene color target

The path tracer's output must land in whatever texture the existing bloom /
exposure / tonemap chain consumes. Search anchor:

- `runtime/engine/renderer/scene/scene_renderer_init.cpp` — constructs the HDR
  scene color target
- `runtime/engine/renderer/scene/scene_render_frame.cpp` — passes it between
  lighting → bloom → tonemap → final
- Format: RGBA16F (confirm by grep on creation site when implementing)

Writing linear HDR here (no tonemap in the tracer) lets every downstream
effect — bloom, exposure, tonemap, FXAA/SMAA — work unchanged.

## 1.4 Camera / per-frame constants

Search anchor: `runtime/engine/renderer/core/params/per_frame_params.{h,cpp}`
and `runtime/engine/renderer/core/params/constant_buffers.h`. The existing
`FrameConstants` CB carries view/proj/inv-view/inv-proj, camera world
position, jitter, frame index, render size. The path tracer consumes this
CB directly — no new per-frame CB needed.

## 1.5 CVar system

`runtime/core/cvar.h` — macros:
- `UZ_CVAR_BOOL(name, default, help)`
- `UZ_CVAR_INT(name, default, help)`
- `UZ_CVAR_FLOAT(name, default, help)`
- `UZ_CVAR_EXEC(name, lambda, help)` for commands

Registration: static locals inside the owning .cpp (pattern used by GTAO,
bloom, lens flare, rt_manager). Access: `cv_Name->value` each frame. The
path tracer pass owns all `r.PathTracer.*` CVars.

## 1.6 Material model

`runtime/engine/renderer/material/material.h` — `PBRMaterial`:
- `float4 baseColor` (rgb albedo, a opacity)
- `float metallic`, `float roughness`, `float ao`
- `float4 emissive`

Mapping to the shadertoy reference (`albedo, emissive, percentSpecular,
roughness, specularColor`):

| Reference | UzEngine |
|---|---|
| `albedo` | `baseColor.rgb * (1 - metallic)` (dielectrics only; metals have black diffuse) |
| `emissive` | `emissive.rgb * emissive.a` (intensity) |
| `percentSpecular` | `lerp(0.04, 1.0, metallic)` (Fresnel F0 dielectric baseline) |
| `roughness` | `roughness` (already squared inside BRDF per Burley convention) |
| `specularColor` | `lerp(float3(1), baseColor.rgb, metallic)` (metallic tint) |

This mapping lives in `pt_material.hlsli` and keeps the path tracer in
lock-step with the rasterized PBR lighting.

## 1.7 What does NOT exist and needs creation

- No compute-based progressive accumulation target (TAA has history but
  different semantics)
- No analytic scene primitive component for the path tracer
- No `path_tracer_cs.hlsl` / `path_tracer_resolve_cs.hlsl`
- No `PathTracerPass` class
- No `r.PathTracer.*` CVars
- No `uz.pathtracer` Lua table
- No Falcor reference script `scripts/falcor/path_tracer_reference.py`
- No demo `demos/path_tracer/`
- No golden images under `.sandbox/golden/path_tracer/`

Everything else (RHI, bindless heap, shader manager, CB ring, frame graph,
CVar system, tonemap/bloom/exposure stack) is reusable as-is.

---

Next: See 02-architecture.md
