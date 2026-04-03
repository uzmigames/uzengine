<!-- doc-meta
title: Shader Library
category: rendering
status: current
updated: 2026-04-01
-->

# Shader Library

Runtime HLSL shader library organized by subsystem. All shaders compile with DXC for SM6.6 (bindless) with SM5 fallback via `#ifdef UZ_BINDLESS`.

## Directory Structure

```
runtime/shaders/runtime/
├── common/          # Shared headers (constants, math, encoding, sampling, bindless)
├── material/        # PBR material model (BRDF, diffuse, specular, sheen, clearcoat, POM)
├── include/         # Cross-subsystem headers (envmap_sampling)
├── gbuffer/         # GBuffer write (mesh_vs, gbuffer_ps, decal_vs/ps, debug)
├── lighting/        # Deferred lighting, shadow, IBL, light types, env MIS
├── ibl/             # IBL compute (BRDF LUT, irradiance, prefilter, equirect, luminance pyramid)
├── atmosphere/      # Sky atmosphere, transmittance, fog, clouds, god rays, aerial perspective
├── postprocess/     # Bloom, tonemap, TAA/TSR, GTAO, SSR, motion blur, DoF, SMAA, LUT, lens flare
├── compute/         # GPU culling, HiZ, draw compact, mipgen, decal apply
├── shadows/         # Distance field shadow cone trace, cull, upsample
├── gi/              # Global SDF composite, mesh SDF bake, radiance cache
├── ssgi/            # Screen-space GI trace, temporal, denoise, upsample
├── vfx/             # Particle spawn/update/sort, billboard/mesh rendering, trails, decals
└── debug/           # Debug views, pixel debug, visualization
```

## Shader Count by Subsystem

| Subsystem | Shaders | Key References |
|-----------|---------|----------------|
| Common headers | 6 | constants, math, color, encoding, sampling, bindless |
| Material model | 8 | Burley 2012, Heitz 2014, Schlick 1994, Estevez 2017 |
| GBuffer | 5 | mesh_vs, gbuffer_ps, mesh_ps, decal_vs/ps, debug |
| Lighting | 6 | deferred_lighting_ps, shadow, ibl, light_types, cluster_assign, env_mis |
| IBL compute | 5 | Karis 2013, Lagarde 2014, Vose 1991 |
| Atmosphere | 16 | Bruneton 2017, Hillaire 2020, Schneider 2015 |
| Post-process | 20 | Karis 2014, Salvi 2016, Jimenez 2016, ACES S-2014-006 |
| Compute utils | 5 | Wihlidal 2015 (GPU culling) |
| Shadows (DFS) | 3 | SDF cone trace |
| GI | 3 | SDF composite, mesh SDF, radiance cache |
| SSGI | 5 | HiZ trace, SVGF denoise |
| VFX | 14 | Billboard, mesh, trail, decal, distortion |
| Debug | 3 | 15 view modes, pixel debug, visualization |
| **Total** | **~145 shaders** | |

## Bindless vs SM5

All shaders support two paths via `#ifdef UZ_BINDLESS`:

- **SM6.6 (bindless)**: `ResourceDescriptorHeap[index]` for all textures/buffers. SRV indices passed via root constants or PerFrameCB.
- **SM5 (fallback)**: Explicit `register(tN)` bindings. Textures bound via `SetFragmentTexture(slot, handle)`.

The shader compiler adds `UZ_BINDLESS=1` automatically when targeting SM6.0+.

## Entry Point Convention

All shader entry points MUST be named `main` — the ShaderManager compiles with `entryPoint = "main"`. See `.claude/rules/shader-entry-point.md`.

## CPU Math Parity

Key HLSL functions have C++ mirrors in `engine/renderer/lighting/brdf_math.h` for unit testing:

| HLSL Function | C++ Mirror | Test File |
|---------------|-----------|-----------|
| `D_GGX()` | `brdf::D_GGX()` | `test_brdf_parity.cpp` |
| `F_Schlick()` | `brdf::F_Schlick()` | `test_brdf_parity.cpp` |
| `V_SmithGGXCorrelated()` | `brdf::VisibilitySmithGGX()` | `test_brdf_parity.cpp` |
| `EvalFrostbiteDiffuse()` | `brdf::DiffuseDisneyBurley()` | `test_brdf_parity.cpp` |
| `ApplyToneCurve()` | `brdf::ApplyToneCurve()` | `test_brdf_parity.cpp` |
