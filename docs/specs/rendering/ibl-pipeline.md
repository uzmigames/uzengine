# IBL Pipeline

<!-- doc-meta
title: IBL Pipeline
category: spec
engine: uzengine
theme: rendering
created: 2026-03-17
updated: 2026-10-08
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

**SkyLight terms on the per-frame SH (as built, 2026-10-08).** Every frame,
`runtime/engine/renderer/frame/frame_constants.cpp` copies the baked SH into
`PerFrameCB.skyIrradianceSH`, multiplied by
`shScale = indirectLightingIntensity x r.SkyLight.IndirectIntensity` (`:421`, `:428-436`). When
`lowerHemisphereIsBlack` is false, it also adds a hemisphere light of radiance
`lowerHemisphereColor` below the horizon: `+L/2` on the constant term and `-L/2` on the up term of
the cube frame, both times `shScale` (`:438-454`, correct since `c14215ed`). The SkyLight
`intensity` is not applied to these coefficients.

The SH stays in the cubemap frame (+Y up), and
`runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:1037` evaluates it with
`WorldToCubemap(N)`. It has two readers:

- the sky-SH fallback of the deferred lighting, which tints it with the sky light colour but not
  with the intensity (`:714`). When `IsSHZero` reads the SH as empty (it tests only the red
  channel's L1 terms, `runtime/shaders/runtime/lighting/ibl.hlsli:58-61`), as a `shScale` of 0
  makes it, the fallback samples the irradiance cubemap instead, scaled by neither intensity
  (`deferred_lighting_ps.hlsl:1040-1044`);
- the seed of the inert light probe volume (below).

Fog and cloud ambient read the unscaled L0 term times the intensity instead
(`runtime/engine/renderer/frame/frame_atmosphere_passes.cpp:123-125`). Section 2 of
`docs/guides/indoor-lighting-guide.md` lists which SkyLight setting reaches which lighting path.

### Light Probe Volume (author-placed irradiance grid): inert

> **Status (2026-10-08): inert. The volume contributes nothing to any frame.** This section was
> corrected by the review of `phase0_indoor-lighting-system` on 2026-10-08. The version written on
> 2026-04-07 described a `CaptureFromSkyCubemap` seed and a blend by `lpvIntensity` that the code
> does not perform. Whether to keep the code or remove it is that task's open decision N3.

`runtime/engine/renderer/gi/irradiance_probe_grid.{h,cpp}` holds a 3D grid of L2 SH probes inside
the AABB of a `LightProbeVolumeComponent`
(`runtime/engine/ecs/environment/light_probe_volume_component.h`). The component loads from the
scene JSON key `light_probe_volume` (`light_probe_volume_component.cpp:15-52`).

**Per-probe storage.** Each probe takes 7 float4 (112 bytes), in the packing of the sky SH above.
The grid is one upload-heap `StructuredBuffer<float4>` with an SRV and no UAV use
(`irradiance_probe_grid.cpp:123-145`). The linear index is `x + y*dimX + z*dimX*dimY`
(`runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:1005-1007`).

**Seed.** `runtime/engine/renderer/frame/frame_constants.cpp:906-977` creates the grid for the
first enabled volume only (`:975`). When the volume is loaded or created (`needsRebuild`), every
probe receives the same coefficients: the per-frame sky SH, after the SkyLight's `shScale` and
lower-hemisphere terms (`:428-454`), converted from the cubemap frame to the world frame by
`FillFromSkySH(SHCubemapToWorld(skySH))` and then `UploadToGpu()` (`:936-947`). The probes
therefore carry no spatial variation, and a later sky re-bake does not re-seed them.
`IrradianceProbeGrid::CaptureFromSkyCubemap` (`irradiance_probe_grid.cpp:193-216`), which this
section used to give as the initial fill, is never called.

**CB layout.** The fields are filled each frame at `frame_constants.cpp:949-972`. They occupy 48
bytes of `PerFrameCB` (`runtime/engine/renderer/core/params/constant_buffers.h:249-260`):

| Field | Bytes | Purpose |
|---|---|---|
| `lpvEnabled` | 4 | 1 = a volume is allocated |
| `lpvProbeBufferSrv` | 4 | Bindless SRV of the probe SH buffer |
| `lpvDimX/Y/Z` | 12 | Probes per axis |
| `lpvOriginX/Y/Z` | 12 | World-space AABB min corner (cm) |
| `lpvInvSizeX/Y/Z` | 12 | `1 / (extentMax - extentMin)` per axis |
| `lpvIntensity` | 4 | Contribution multiplier. **Always 0**: hard-coded at `frame_constants.cpp:967-972` since `29c83ba5`, the commit that added these fields |

`PerFrameCB` grew from 768 to 1024 bytes (4 × 256) for these fields and the DDGI fields; the
`static_assert` is at `constant_buffers.h:321`, and `PerFrameParams` mirrors the layout. The tail
padding left by that growth has since been filled by region-dump and diagnostic fields (offsets
992-1023, `constant_buffers.h:300-320`), so the buffer is full.

**Shader sampling.** `deferred_lighting_ps.hlsl:973-1028` blends the 8 surrounding probes
trilinearly and evaluates `EvalSH_L2` with the world normal. The block runs only when
`_lpvIntensity > 0` (`:978`), so it never runs. Nor is the volume lerped into the irradiance after
the sky SH and the radiance cache, as this section used to say. It is source 3 of a chain that
takes one source per pixel (DDGI, then radiance cache, then light probe volume, then sky SH;
`:739-754`), and once the radiance cache has produced a result it answers every pixel (`:968-969`).
Even with a non-zero intensity, the volume would only replace the sky-SH fallback, untinted
(`:1069-1072`).

**Not implemented:**

- `autoFitScene` and `autoFitPadding`, which only the serializer reads.
- More than one volume: only the first enabled volume is used.
- An explicit release at shutdown. The grid is freed by its destructor only
  (`irradiance_probe_grid.cpp:25-27`; member at
  `runtime/engine/renderer/scene/scene_renderer_impl.h:403`).

No scene uses a volume: none of the 90 `.uzscene` files has `light_probe_volume`, and Sponza's
`InteriorProbeVolume` was removed in `eb839ed9` (2026-04-07). Spatially varying indirect diffuse
comes from the radiance cache, the SDF final gather and DDGI (`docs/specs/rendering/gi-system.md`,
`docs/engine/12-rendering/33-sdf-gi-final-gather.md`, `docs/guides/indoor-lighting-guide.md`).

**DDGI atlases.** These are the format the indoor-lighting task's item 7.2 asked this page to
record. Both atlases cover the radiance cache's 16³ probe positions
(`runtime/engine/renderer/gi/ddgi_system.h:62-74`; allocated at `ddgi_system.cpp:47-90`):

- **Irradiance:** `RGBA16Float`, 2048 × 128, with 6 × 6 octahedral texels per probe plus a
  1-texel border.
- **Depth:** `RG16Float`, 4608 × 288, with 16 × 16 texels per probe plus the border. It stores the
  mean distance and the mean squared distance, normalised by the maximum trace distance
  (`deferred_lighting_ps.hlsl:832-841`).

`ddgi_probe_update_cs.hlsl` writes both from the per-probe rays that `ddgi_probe_trace_cs.hlsl`
traces once per frame (block B.6, `r.GI.DDGI.RayBuffers 1` by default;
`docs/analysis/lighting-block-b-06-ddgi-dedup.md`). They are read
with the Chebyshev visibility test at `deferred_lighting_ps.hlsl:759-880`.

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
| Brixelizer GI diffuse composite in deferred lighting | Dormant: never taken; task closed as superseded 2026-10-08 |

### Brixelizer GI Integration (dormant)

> **Status (2026-10-08): dormant; the composite below never runs.** The Brixelizer pass compiles as
> a no-op stub (`runtime/build.zig:903-911` defines `UZ_BRIXELIZER_ENABLED` only when an MSVC-built
> wrapper DLL exists, and nothing in the repo builds it). Its construction has been commented out
> since 2026-04-10 (`runtime/engine/renderer/scene/scene_renderer_init.cpp:666-674`) and its dispatch
> disabled since 2026-04-07 (`runtime/engine/renderer/frame/frame_gi_passes.cpp:387`).
> `phase0_brixelizer-gi-integration` was closed as superseded on 2026-10-08. The engine's spatially
> varying indirect light comes from the SDF radiance cache, the SDF final gather and SDF reflections,
> with DDGI as the DXR option (`docs/specs/rendering/gi-system.md`,
> `docs/engine/12-rendering/33-sdf-gi-final-gather.md`).

What the dormant branch in `deferred_lighting_ps.hlsl` (`:1298-1324`) does, for the record:

- It is gated only by the SRV index. `brixGIDiffuseSrv` (PerFrameCB offset 764) is `UINT32_MAX`
  unless a Brixelizer pass is initialised (`frame_constants.cpp:868-872`, which does not read
  `r.BrixelizerGI`), so the branch is never taken today.
- When the SRV is valid and the sample's luma exceeds 0.001, it **replaces** the diffuse ambient
  instead of adding to it: `diffuseAmbient = (1 - metallic) × brixDiffuse × baseColor` (`:1321`).
  That would also discard the world final gather and any legacy SSGI already in `diffuseAmbient`
  (`:1148-1150`, `:1277-1295`) and its `(1 - totalScatterDielectric)` factor (`:1246-1247`).
- There is no specular term. The `diffuseSrv + 1` specular read was removed in `29c83ba5`, and the
  "+1" convention does not hold: SRVs and UAVs come from one allocator
  (`runtime/engine/rhi/dx12/dx12_texture_creation.cpp:195`, `:232`).
