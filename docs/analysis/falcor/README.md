<!-- doc-meta: category=analysis, area=falcor, topic=master-report -->

# NVIDIA Falcor Analysis — Master Report

> Comprehensive comparison of NVIDIA Falcor v8.0 vs UzEngine
> Date: 2026-03-30

## Overview

[NVIDIA Falcor](https://github.com/NVIDIAGameWorks/Falcor) is a real-time rendering research
framework built by NVIDIA. It targets DX12/Vulkan with a strong focus on ray tracing (DXR),
path tracing, and denoising. It uses the Slang shader language and a Python-scriptable render
graph. This analysis covers architecture, render passes, shaders/materials, GI/ray tracing,
and post-processing — comparing each area against UzEngine's current state.

---

## Detailed Reports

| Report | File | Agent |
|--------|------|-------|
| Architecture & Infrastructure | [architecture-and-infrastructure.md](architecture-and-infrastructure.md) | architecture |
| Render Passes | [render-passes.md](render-passes.md) | render-passes |
| Shaders & Materials | [shaders-and-materials.md](shaders-and-materials.md) | shaders-materials |
| GI & Ray Tracing | [gi-and-raytracing.md](gi-and-raytracing.md) | gi-raytracing |
| Post-Processing & Effects | [postprocess-and-effects.md](postprocess-and-effects.md) | postprocess-effects |

---

## Executive Summary

### Where UzEngine is AHEAD of Falcor

| Area | UzEngine | Falcor |
|------|----------|--------|
| Sky Atmosphere | Hillaire 2020 full LUT pipeline (transmittance, multi-scatter, AP) | None (path-tracer only) |
| Volumetric Clouds | GPU ray-march, temporal reprojection, weather maps | None |
| Height Fog | Exponential height fog with inscattering | None |
| Shadow Mapping | CSM + PCSS + contact shadows + DFS + local light atlas + importance sorting | Basic raster shadows only (RT shadows preferred) |
| Auto-Exposure | 64-bin histogram + percentile filtering | Simple mip-pyramid average |
| VFX Pipeline | GPU particles (sim+render+trails+distortion), decals | None |
| Post-Processing Suite | Bloom, lens flare, DoF, motion blur, CAS sharpening | Minimal (tone map + TAA only) |
| Upscaling | FSR2 + TSR + SMAA + TAA | TAA + DLSS only |
| AO | GTAO with temporal accum, 4 quality presets | None (relies on RT AO) |
| SSGI | 5-pass compute (reduce+trace+spatial+temporal+upsample) | None (uses path-traced GI) |
| Distance Field GI | 4-cascade clipmap, SH probe grid, GDF-traced radiance cache | None |
| Diagnostic Tools | `--diag`, `--vis`, `--pass-dump`, `--shadow-debug`, `--pass-toggle` | None |
| Render Graph | Per-frame `RgBuilder` rebuild (dynamic toggling) | Persistent graph with recompile flag |
| ECS Integration | Full ECS `World` -> `DrawList` separation | Monolithic render-only `Scene` |

### Where Falcor is AHEAD of UzEngine

| Area | Falcor | UzEngine Gap |
|------|--------|--------------|
| Ray Tracing | Full DXR pipeline (TLAS/BLAS, shader tables, TraceRay) | Zero RT infrastructure |
| Path Tracing | Wavefront 3-stage (GeneratePaths+Trace+Resolve) | None |
| ReSTIR DI | RTXDI SDK, spatiotemporal reservoir resampling | None |
| ReSTIR GI | Full screen-space + world-space reservoirs | None |
| Denoising | SVGF (Schied 2017) + NRD SDK (ReLAX/ReBLUR) | None |
| Material Types | 10+ types (Standard, Cloth, Hair, MERL, RGL, PBRT) | 1 type (metallic-roughness PBR) |
| Shader Language | Slang (interfaces, generics, modules) | HLSL + `#include` |
| Shader Variants | `DefineList` -> separate compiled PSOs | None (manual `#ifdef`) |
| Light BVH | Emissive triangle importance sampling (Moreau 2019) | None |
| Volumetric Media | OpenVDB density grids, delta tracking, HG phase | None |
| Auto Root Signature | Shader reflection -> auto layout | Manual `root_slots.h` |
| Multiple Cameras | `getCameras()` vector + viewpoints | Single camera only |
| Plugin Passes | DLL-based distributable render passes | Static compile-time only |
| DLSS | Native DLSS integration | Stub exists, no impl |
| Tone Mapping | Selectable operators (ACES, Reinhard, Hable, etc.) | ACES only |
| BSDF Library | Oren-Nayar, sheen/cloth, hair, Fresnel conductor/dielectric, layered | Disney diffuse + GGX only |
| FLIP Metric | Perceptual quality comparison tool | None |

---

## Prioritized Gap Adoption Roadmap

### P0 — Quick Wins (rasterization pipeline improvements)

These can be adopted independently, no RT infrastructure needed.

| # | Feature | Effort | Value | Source Reference |
|---|---------|--------|-------|-----------------|
| 1 | **Shader utility library consolidation** | 1-2 days | High | Move Hammersley/Sobol/cosine-hemisphere to shared `sampling.hlsli`; add `branchlessONB` (Pixar), PCG hash, octahedral encoding |
| 2 | **Selectable tone mapping operators** | 1 day | Medium | Add Reinhard, Modified Reinhard, Hable UC2 alongside ACES |
| 3 | **Color grading LUT pass** | 1 day | Medium | `color_grading_lut_cs.hlsl` already exists, needs C++ pass wiring |
| 4 | **Physical camera exposure** | 1 day | Medium | ISO/f-number/shutter speed -> EV100 calculation |
| 5 | **White balance** | 0.5 day | Low | Kelvin -> RGB chromaticity matrix |
| 6 | **Vignette + Chromatic Aberration** | 0.5 day | Low | Single compute pass, SimplePostFX pattern |
| 7 | **FLIP metric pass** | 1-2 days | High | Automated visual regression testing between backends |

### P1 — Medium-Term (material system expansion)

| # | Feature | Effort | Value |
|---|---------|--------|-------|
| 8 | **Shader variant/permutation system** | 1 week | Critical | `DefineList` -> separate PSOs per material/feature combo |
| 9 | **Fresnel conductor/dielectric** | 2 days | High | Physical metals + glass/IOR (replaces Schlick approximation for metals) |
| 10 | **Oren-Nayar diffuse BRDF** | 1 day | Medium | Rough diffuse surfaces (clay, concrete, fabric) |
| 11 | **Cloth/Sheen BSDF** | 2 days | Medium | Estevez-Kulla 2017 for fabric materials |
| 12 | **VNDF importance sampling** | 1 day | Medium | Lower variance GGX sampling (Heitz 2018) |
| 13 | **Multiple camera support** | 2 days | Medium | Camera vector + switchable viewpoints |
| 14 | **UpdateFlags propagation** | 2 days | High | Avoid full CB rebuild every frame |
| 15 | **TAA Catmull-Rom history** | 1 day | Medium | Bicubic history sampling reduces temporal blur |

### P2 — Long-Term (ray tracing pipeline)

| # | Feature | Effort | Value |
|---|---------|--------|-------|
| 16 | **DXR infrastructure** (TLAS/BLAS/shader tables) | 2-3 weeks | Critical prerequisite |
| 17 | **Minimal path tracer** | 1 week | Learning/validation baseline |
| 18 | **ReSTIR DI** | 2 weeks | Highest RT value, layerable on deferred pipeline |
| 19 | **SVGF denoiser** | 1 week | Self-contained, no SDK dependency |
| 20 | **Hair BSDF** (Chiang 2016) | 1 week | Character rendering |
| 21 | **Light BVH** | 1 week | Emissive triangle importance sampling |
| 22 | **NRD integration** | 1 week | After full RT pipeline exists |
| 23 | **DLSS integration** | 1 week | Stub already exists |

### P3 — Research / Low Priority

| # | Feature | Notes |
|---|---------|-------|
| 24 | Slang shader language | Interesting but massive migration; HLSL is fine for production |
| 25 | Plugin-based render passes | Nice-to-have; static compile is simpler for game engine |
| 26 | Python render graph scripting | Lua scripting already covers game-side; editor-side graph is fine in C++ |
| 27 | OpenVDB volumetrics | Only needed for cinematic rendering |
| 28 | Differentiable rendering | Pure research |

---

## Key Architectural Lessons from Falcor

### 1. Shader Variant System is the #1 Gap

Falcor's `DefineList` + `ProgramVersion` system allows automatic PSO creation per material/feature
permutation. UzEngine's manual `#ifdef` approach doesn't scale to multiple material types.
This is the single most important architectural gap to close before adding cloth/hair/glass materials.

### 2. Material Interface Dispatch

Falcor uses Slang's interface polymorphism (`IMaterial`, `IBSDF`) to dispatch to the correct
BSDF at runtime without switch statements. In HLSL, this can be approximated with a material
type ID + function table pattern (similar to how UE5 handles shading models).

### 3. Decomposed Illumination for Denoising

Falcor separates diffuse/specular/emission before denoising (NRD requires this), then
recombines via `ModulateIllumination`. UzEngine should plan for this decomposition when
adding RT support — retrofitting is much harder.

### 4. Per-Frame Render Graph is Superior

UzEngine's per-frame `RgBuilder` rebuild is actually better than Falcor's persistent graph
for game engine use cases (dynamic feature toggling, LOD-driven pass changes). Keep this design.

### 5. Diagnostic Tooling is a Competitive Advantage

UzEngine's `--diag` / `--vis` / `--pass-dump` / `--shadow-debug` CLI suite has no equivalent
in Falcor. This is a genuine competitive advantage for debugging and CI/CD integration.
Continue investing in this tooling.

---

## Conclusion

Falcor excels as a **ray tracing research framework** with world-class path tracing, ReSTIR,
and denoising. UzEngine excels as a **production game engine** with superior rasterization
pipeline, atmosphere, VFX, diagnostics, and post-processing.

The most valuable near-term adoptions from Falcor are:
1. **Shader utility library** consolidation (sampling, encoding, hashing)
2. **Shader variant/permutation system** (prerequisite for material expansion)
3. **Additional BSDFs** (Fresnel conductor, Oren-Nayar, cloth)
4. **Selectable tone mapping operators** + color grading LUT wiring
5. **FLIP metric** for automated visual regression

The ray tracing pipeline (DXR, path tracing, ReSTIR, denoising) is a long-term investment
that should wait until the rasterization pipeline is fully mature.
