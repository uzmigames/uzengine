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

Point and spot PCSS functions and their shipped call sites are documented in
[Local Shadow Shader Functions](rendering/shader-library.md).

Key HLSL functions have C++ mirrors in `engine/renderer/lighting/brdf_math.h` for unit testing:

| HLSL Function | C++ Mirror | Test File |
|---------------|-----------|-----------|
| `D_GGX()` | `brdf::D_GGX()` | `test_brdf_parity.cpp` |
| `F_Schlick()` | `brdf::F_Schlick()` | `test_brdf_parity.cpp` |
| `V_SmithGGXCorrelated()` | `brdf::VisibilitySmithGGX()` | `test_brdf_parity.cpp` |
| `EvalFrostbiteDiffuse()` | `brdf::DiffuseDisneyBurley()` | `test_brdf_parity.cpp` |
| `ApplyToneCurve()` | `brdf::ApplyToneCurve()` | `test_brdf_parity.cpp` |
| `ACESHillFit()` | `brdf::ACESHillFit()` | `test_brdf_parity.cpp` |
| `ACESFilmic()` | `brdf::ACESNarkowicz()` | `test_brdf_parity.cpp` |
| `ComputeSpecularOcclusion()` | `brdf::ComputeSpecularOcclusion()` | `test_brdf_parity.cpp` |
| `D_GGXAniso()` | `brdf::D_GGXAniso()` | `test_brdf_parity.cpp` |
| `V_SmithGGXCorrelated_Aniso()` | `brdf::V_SmithGGXCorrelated_Aniso()` | `test_brdf_parity.cpp` |
| `RemapAnisotropy()` | `brdf::RemapAnisotropy()` | `test_brdf_parity.cpp` |

## Froxel Volumetric Fog (`atmosphere/volumetric_fog_*`)

Three shaders share one 512-byte `VolumetricFogCB` at **b2** (scalars only — an HLSL `float3` at a 4-byte-aligned offset would move to the next 16-byte row; `scripts/rendering/test_cbuffer_layouts.py` checks every offset against `VolumetricFogCB` in `volumetric_fog_pass.h`, `test_volumetric_fog.cpp` checks the C++ side). @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"; Wronski 2014.

| Shader | Threads | Bindings | Writes |
|--------|---------|----------|--------|
| `volumetric_fog_inject_cs.hlsl` | `[8,8,4]`, one per froxel | b1 `LightingCB` (point / spot arrays, the prefix of `constant_buffers.h`), b2, b4 `ShadowDataCB`, CSM + local shadow atlases via `shadow.hlsli` | injected volume: rgb in-scatter, a extinction (per unit length) |
| `volumetric_fog_scatter_cs.hlsl` | `[8,8,1]`, one per column | b2, s2 history sampler (bilinear clamp), injected + history SRVs | integrated volume: rgb in-scatter, a transmittance; this frame's filtered history |
| `volumetric_fog_composite_ps.hlsl` | fullscreen | b0 root constants (depth SRV, integrated SRV), b2, s0 linear | HDR target, SrcAlpha / InvSrcAlpha |

Functions (the depth-slice set is copied identically into each shader that needs it):

| Function | Shader | Meaning |
|----------|--------|---------|
| `SliceCoordToDepth(t)` | inject | `startZ * (farZ / startZ)^t`, `startZ = max(nearZ, fogStartDistance)`: the exponential slice distribution (UE5 `GetVolumetricFogGridZParams`); `t = (slice + 0.5 + jitter) / gridZ` |
| `SliceToDepth(slice, n)` | scatter, composite | the slice centre's view depth, `t = (slice + 0.5) / n` |
| `DepthToSliceCoord(d)` | scatter | inverse: `log(d / startZ) / log(farZ / startZ)` (the history fetch) |
| `SliceThickness(slice, n)` | scatter | centre-to-centre span, slice 0 from `startZ`: the integration step |
| `DepthToFroxelUVW(uv, d)` | composite | `(uv, saturate(log(max(d, startZ) / startZ) / log(farZ / startZ)))`: sky and geometry beyond the grid read the far edge |
| `UnprojectFar(ndc)`, `FroxelWorldPos(uv, d)` | inject, scatter | the point on the camera ray through the froxel column whose view depth is `d` (unjittered inverse view-projection) |
| `HenyeyGreenstein(cosTheta, g)` | inject | `(1 - g^2) / (4 pi (1 + g^2 - 2 g cosTheta)^1.5)` |
| `Pcg3d(v)`, `FroxelJitter(froxel)` | inject | froxel hash + the frame's R3 point: a `[-0.5, 0.5]^3` sample offset the temporal average integrates (Jarzynski & Olano 2020; Roberts 2018) |
| `LocalLightAttenuation(distSq, radius, exponent, flags)` | inject | the deferred pass's windowed inverse-square / custom-exponent falloff (Karis 2013) |
| `LocalShadowVolumeTap(entry, worldPos)`, `PointLightFace(d)` | inject | one linear-distance comparison tap in a local light's atlas tile (no normal offset), the point light's cube face by major axis |
| `LocalLightsInscatter(worldPos, viewDir, shadowed)` | inject | sum over the frame's point and spot lights of colour x attenuation x cone x shadow x HG |
| `CascadeShadowVolume(worldPos, dist)` | `lighting/shadow.hlsli` | one CSM comparison tap for a point in mid-air (the sun's volumetric shadow) |
| `TemporalFilter(injected, history, froxel, cur)` | scatter | reproject the froxel centre with `prevViewProj`, trilinear history, 3x3x3 min/max box intersected with the mean +- `temporalClamp` sigma band, `lerp(hist, cur, temporalAlpha)`; returns `cur` behind the previous camera or outside its frustum / grid |
| `LinearizeDepthReversedZ(depth, near, far)` | `include/depth_common.hlsli` | the composite's finite-far-plane depth linearisation |

The integration per slice (scatter) is Hillaire 2015 eq. 5: `S_int = fade * (S - S * T_slice) / max(sigma, 1e-6)`, `accum += T * S_int`, `T *= lerp(1, T_slice, fade)`, `T_slice = exp(-sigma * thickness)`, `fade = saturate(accumDepth / nearFadeInDistance)`.

CPU mirror for tests: the depth-slice functions and the composite's W coordinate are mirrored in `runtime/tests/engine/renderer/test_volumetric_fog.cpp` (`SliceParams`); the froxel placement and the whole scatter loop are ported per pixel by `scripts/rendering/test_froxel_fog.py` (compiled from the HLSL text with `zig c++`, and in NumPy).

## Vertex Formats

| Format | Size | Fields | Shader Struct |
|--------|------|--------|---------------|
| `Vertex` | 48B | position(f3), normal(f3), texcoord(f2), tangent(f4) | `mesh_vs.hlsl::Vertex` |
| `VertexPacked` | 32B | position(f3), packedNormal(i8x4), texcoord(f2), packedTangent(i8x4), pad | `mesh_vs.hlsl::VertexPacked` |

### Packed Normal Decode (HLSL)

```hlsl
// In mesh_vs.hlsl — decode int8x4 packed normals/tangents
float3 vNormal  = float3(pv.packedNormal.xyz) / 127.0;
float4 vTangent = float4(float3(pv.packedTangent.xyz) / 127.0,
                         (pv.packedTangent.w >= 0) ? 1.0 : -1.0);
```

CVar `r.Mesh.PackedNormals` (default=false) toggles packed export during mesh import.
Push constant `usePackedNormals` selects shader decode path at draw time.
