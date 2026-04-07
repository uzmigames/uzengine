# IBL Pipeline

<!-- doc-meta
title: IBL Pipeline
category: spec
engine: uzengine
theme: rendering
created: 2026-03-17
updated: 2026-03-17
status: active
author: docs-operator
-->

## Overview

The IBL pipeline bakes GPU textures required for physically-based ambient lighting:
a BRDF integration LUT, an irradiance cubemap for diffuse IBL, and a prefiltered
specular cubemap for specular IBL. All three are produced by compute shaders via
`IBLBaker`. Spherical Harmonics projection converts the irradiance cubemap into 7
float4 values for efficient per-pixel diffuse evaluation. A CPU fallback via
`sky_cubemap.cpp` covers SM5.0 paths.

## Architecture

### Pipeline Stages

```
Sky Cubemap Capture (sky_cubemap_cs.hlsl or sky_cubemap_ps.hlsl)
         |
         v
   IBLBaker::BakeEnvironment(envCubemapSrvIndex)
   ├─ irradiance_cs.hlsl  → 32x32 RGBA16F cubemap  (diffuse)
   └─ prefilter_cs.hlsl   → 128x128 RGBA16F, 5 mips (specular)
         |
         v
   SHProjection (sh_projection.h/cpp)
   └─ ProjectCubemapToSH() → SHCoefficients (7 float4, GPU CB)

   IBLBaker::BakeBRDFLut()  (independent, once at init)
   └─ brdf_lut_cs.hlsl    → 512x512 RG16F LUT      (split-sum)
```

`IBLBaker` is lazy: compute pipelines compile on the first `Bake*()` call, not at
`Init()`. Baked textures persist until `Shutdown()`. Subsequent calls to
`BakeBRDFLut()` return the cached SRV index without re-dispatching.

### BRDF LUT

- **Shader**: `brdf_lut_cs.hlsl`
- **Output**: 512x512 RG16F — R = scale (F0 multiplier), G = bias (F0 offset)
- **Purpose**: Precomputes the split-sum approximation of the specular BRDF integral
  for every (NdotV, roughness) combination
- **Dispatched**: once at engine init; result is permanent

### Irradiance Cubemap

- **Shader**: `irradiance_cs.hlsl`
- **Output**: 32x32 RGBA16F Texture2DArray (6 slices, DX cubemap face order)
- **Purpose**: Convolves the environment cubemap with a cosine lobe for diffuse IBL
- **Dispatched**: once per sky change (triggered by `skyIBLDirty` flag)

### Prefiltered Specular Cubemap

- **Shader**: `prefilter_cs.hlsl`
- **Output**: 128x128 RGBA16F Texture2DArray, 5 mip levels
- **Purpose**: Importance-samples the environment cubemap at increasing roughness per
  mip level for the specular split-sum integral
- **Per-mip dispatch**: separate temporary textures created per mip, copied into the
  final mip chain (avoids per-mip UAV descriptor complexity)

### Spherical Harmonics Projection

`ProjectCubemapToSH()` (`sh_projection.h/cpp`) reads back the sky cubemap from GPU,
integrates against 9 real L2 SH basis functions, applies cosine-lobe diffuse
convolution, and packs into 7 `float4` values matching UE5
`SkyIrradianceEnvironmentMap[0..6]`.

The packed layout:

```
coeffs[0] = R channel: L1 + L0 terms
coeffs[1] = G channel: L1 + L0 terms
coeffs[2] = B channel: L1 + L0 terms
coeffs[3] = R channel: L2 terms
coeffs[4] = G channel: L2 terms
coeffs[5] = B channel: L2 terms
coeffs[6] = (R, G, B) Y22 terms, w=1
```

Total: 112 bytes (7 * 16). The GPU shader evaluates diffuse via dot products, no
per-pixel convolution needed.

Sources: `ReflectionEnvironmentCapture.cpp:SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance`,
`ReflectionEnvironmentShared.ush:GetSkySHDiffuse`.

### SkyLight Component Integration

The IBL bake is triggered by the sky change flag (`skyIBLDirty`). The
`AtmosphereRenderer` (`atmosphere_renderer.h/cpp`) orchestrates the full sequence:

1. `AddAtmospherePasses()` — registers sky cubemap capture in the render graph
2. `PostFrameAtmosphereBake()` — after graph execution, calls `BakeEnvironment()`,
   waits for GPU, then runs SH projection

The baked SRV indices and SH coefficients are forwarded to `PerFrameCB` for use by
the deferred lighting pass.

### Light Probe Volume (Author-Placed Irradiance Grid)

`runtime/engine/renderer/gi/irradiance_probe_grid.{h,cpp}` implements a 3D grid
of L2 SH probes within an authored AABB driven by `LightProbeVolumeComponent`.
Used for indoor lighting where the global sky SH is too uniform — the volume
provides spatially-varying indirect diffuse via trilinear blend of 8 surrounding
probes during deferred lighting.

**Per-probe storage**: 7 float4 (112 bytes), matching the L2 SH packing used by
`SkyIrradianceEnvironmentMap`. The grid is allocated as a single
`StructuredBuffer<float4>` with `kSHFloat4PerProbe * probeCount` entries; the
linear index is `x + y*dimX + z*dimX*dimY`.

**Initial fill** — `IrradianceProbeGrid::CaptureFromSkyCubemap(cmdList, sky, faceSize)`
projects the sky cubemap to L2 SH via `ProjectCubemapToSH` and broadcasts the
coefficients to every probe. Sky is positionally invariant, so spatial variation
comes from the SDF/DXR probe-update passes (tasks 3.x / 4.x of the indoor
lighting milestone).

**CB layout** — populated each frame in
`runtime/engine/renderer/frame/frame_constants.cpp`:

| Field | Bytes | Purpose |
|---|---|---|
| `lpvEnabled` | 4 | 1 = volume active |
| `lpvProbeBufferSrv` | 4 | Bindless SRV of probe SH buffer |
| `lpvDimX/Y/Z` | 12 | Probes per axis |
| `lpvOriginX/Y/Z` | 12 | World-space AABB min corner (cm) |
| `lpvInvSizeX/Y/Z` | 12 | Pre-divided 1 / extentSize |
| `lpvIntensity` | 4 | Probe contribution multiplier |

`PerFrameCB` was grown 768 → 1024 bytes (4 × 256) to fit these fields plus
forward-looking padding. Mirrored byte-for-byte in `PerFrameParams`.

**Shader sampling** — `deferred_lighting_ps.hlsl` performs trilinear blend of
the 8 surrounding probes after the global sky SH and radiance cache paths,
lerping the result into `irradiance` by `lpvIntensity`. Bindless-only path.

### Equirectangular to Cubemap

`equirect_to_cubemap_cs.hlsl` converts an HDR equirectangular environment map to a
cubemap. Used when loading `.hdr` or `.exr` environment maps.

### CPU Sky Cubemap Fallback

`sky_cubemap_cs.hlsl` and `sky_cubemap_ps.hlsl` provide two variants for capturing
the procedural sky into a cubemap. The CS variant dispatches a compute shader; the
PS variant renders 6 fullscreen quads with face-specific view matrices. Both produce
the same RGBA16F 6-face cubemap consumed by `BakeEnvironment()`.

## Key Files

| File | Purpose |
|---|---|
| `runtime/engine/renderer/ibl/ibl_baker.h/cpp` | Compute pipeline for BRDF LUT, irradiance, prefiltered specular |
| `runtime/engine/renderer/ibl/sh_projection.h/cpp` | L2 SH projection and GPU readback |
| `runtime/engine/renderer/atmosphere/atmosphere_renderer.h/cpp` | Orchestrates sky capture + IBL bake sequence |
| `runtime/shaders/runtime/ibl/brdf_lut_cs.hlsl` | BRDF integration LUT compute |
| `runtime/shaders/runtime/ibl/irradiance_cs.hlsl` | Irradiance cubemap convolution |
| `runtime/shaders/runtime/ibl/prefilter_cs.hlsl` | Prefiltered specular cubemap |
| `runtime/shaders/runtime/ibl/equirect_to_cubemap_cs.hlsl` | Equirectangular to cubemap conversion |
| `runtime/shaders/runtime/atmosphere/sky_cubemap_cs.hlsl` | Sky cubemap capture (compute) |
| `runtime/shaders/runtime/atmosphere/sky_cubemap_ps.hlsl` | Sky cubemap capture (raster) |
| `runtime/shaders/runtime/include/ibl.hlsli` | IBL helper functions (GGX importance sampling, Hammersley) |
| `runtime/shaders/runtime/include/sh_irradiance.hlsli` | SH evaluation helpers for the lighting pass |

## UE5 Reference

| UzEngine System | UE5 Equivalent |
|---|---|
| `IBLBaker` | `FReflectionEnvironmentCaptureUtilities`, `ReflectionEnvironmentCapture.cpp` |
| BRDF LUT | `IntegrateBRDF` in `PrimitiveDistanceFieldShadowingShader.usf` / `ReflectionEnvironmentShaders.usf` |
| Irradiance cubemap | `FilterReflectionEnvironment` (irradiance pass), `ReflectionCapture.usf` |
| Prefiltered specular | `FilterReflectionEnvironment` (specular mip pass) |
| SH projection + packing | `SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance`, `ReflectionEnvironmentCapture.cpp` |
| SH evaluation | `GetSkySHDiffuse`, `ReflectionEnvironmentShared.ush` |

## Status

| Feature | Status |
|---|---|
| BRDF LUT compute (512x512 RG16F) | Done |
| Irradiance cubemap (32x32 RGBA16F) | Done |
| Prefiltered specular (128x128, 5 mips, RGBA16F) | Done |
| Equirectangular to cubemap | Done |
| Sky cubemap capture (CS + PS variants) | Done |
| L2 SH projection (GPU readback path) | Done |
| L2 SH projection (CPU faceData path) | Done |
| SH evaluation in deferred lighting shader | Done |
| IBL bake triggered by sky change flag | Done |
| IBL from HDR equirectangular file (Phase 3 artist workflow) | Planned |
| IBL from in-scene cubemap capture actors | Planned |
| Brixelizer GI diffuse + specular composite in deferred lighting | In Progress |

### Brixelizer GI Integration

AMD FidelityFX Brixelizer GI adds spatially-varying indirect lighting (diffuse + specular) that supplements the global IBL pipeline. While IBL provides uniform ambient from the sky cubemap, Brixelizer GI provides **local** indirect light that varies based on geometry (e.g., bright near windows, dark in corridors).

In `deferred_lighting_ps.hlsl`, Brixelizer GI output is sampled after IBL and added to the ambient terms:
- `diffuseAmbient += brixDiffuse × baseColor × (1 - metallic)`
- `specularAmbient += brixSpecular × lerp(F0, 1, roughness × 0.5)`

The GI output SRV index is passed via `brixGIDiffuseSrv` in PerFrameCB (offset 764). Specular SRV is at `diffuseSrv + 1` by allocation convention. Gated by `r.BrixelizerGI` CVar (default: off). See `docs/specs/rendering/gi-system.md` for full Brixelizer GI specification.
