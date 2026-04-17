<!-- doc-meta: category=analysis, area=falcor, topic=gi-raytracing -->

# Falcor GI and Ray Tracing Analysis

## Overview

NVIDIA Falcor (github.com/NVIDIAGameWorks/Falcor) is a real-time rendering research framework
built on DX12 and Vulkan. It is the reference platform for most NVIDIA ray tracing research
papers (ReSTIR, path tracing, NRD, RTXGI). This document analyzes its GI and ray tracing
systems and compares them against UzEngine's current state.

---

## 1. Ray Tracing Infrastructure

### 1.1 Acceleration Structure Management

Falcor abstracts DXR/Vulkan RT acceleration structures behind `RtAccelerationStructure`:

| Feature | Falcor | UzEngine |
|---|---|---|
| TLAS / BLAS abstraction | Yes — `RtAccelerationStructureKind::{TopLevel, BottomLevel}` | No RT pipeline |
| Build flags | AllowUpdate, AllowCompaction, PreferFastTrace, PreferFastBuild, MinimizeMemory | N/A |
| Incremental update | Yes — `AllowUpdate` + `PerformUpdate` | N/A |
| Post-build compaction | Yes — `AllowCompaction` | N/A |
| Procedural geometry (AABBs) | Yes — `ProcedurePrimitives` with custom intersectors | N/A |
| Triangle geometry | Yes — indexed meshes with vertex/index buffers | N/A |
| Instance data | `RtInstanceDesc` mirroring D3D12 (3×4 transform, 24-bit ID, 8-bit mask) | N/A |

**Refitting strategy**: Falcor refits BLAS for dynamic geometry (e.g., animated meshes) using
`PerformUpdate` with pre-allocated `AllowUpdate` BLAS. Full rebuilds are reserved for topology
changes. The `RtAccelerationStructurePostBuildInfoPool` collects compaction sizes asynchronously.

### 1.2 Ray Dispatch Patterns

Falcor uses two ray dispatch models:

**TraceRay (recursive-style)**: Used in `MinimalPathTracer` and `WhittedRayTracer`.
The ray generation shader calls `TraceRay(gScene.rtAccel, ...)` per pixel in a loop —
not truly recursive, but structured as an explicit bounce loop to avoid GPU stack overflow:

```hlsl
for (uint depth = 0; depth <= kMaxBounces && !rayData.terminated; depth++) {
    TraceRay(gScene.rtAccel, rayFlags, 0xff, 0, rayTypeCount, 0, ray, rayData);
}
```

**TraceRayInline (compute/raster)**: Used in `GBufferRT` for ray-traced GBuffer generation.
Inline ray tracing allows compute shaders and pixel shaders to trace rays without a separate
shader table. Falcor exposes this via the `mUseTraceRayInline` flag.

### 1.3 Shader Table Management

Falcor manages shader tables through `RtProgram`/`RtProgramVars` abstractions:
- Ray generation, closest-hit, any-hit, miss, and intersection shaders are registered per pass.
- The `ScatterRayData` payload is up to 72 bytes; `ShadowRayData` is 4 bytes (visibility bool).
- Any-hit shaders perform alpha testing (`IgnoreHit()` for transparent fragments).
- Intersection shaders handle displaced meshes, curves, and SDF grids via `ReportHit()`.

---

## 2. Global Illumination Methods

### 2.1 Wavefront Path Tracer (`PathTracer` render pass)

**Architecture**: Wavefront/compute-based, NOT a megakernel.

Three compute stages per frame:
1. `GeneratePaths.cs` — initializes path state from primary hits (V-buffer)
2. `TracePass.rt` — main DXR ray tracing kernel; handles bounces iteratively
3. `ResolvePass.cs` — accumulates per-sample radiance into final output

**Sampling**:
- BSDF importance sampling or cosine-weighted hemisphere sampling (configurable)
- Multiple Importance Sampling (MIS) with Balance or Power heuristic
- Next Event Estimation (NEE) for direct lighting on non-delta surfaces
- NEE disabled for delta specular lobes (`isDeltaSpecular → samplesLight = false`)
- Russian roulette path termination via luminance-based survival probability

**Light sampling options** (configurable per scene):
- `LightBVHSampler` — importance sampling via light BVH over emissive triangles
- `EmissivePowerSampler` — power-proportional sampling (no spatial hierarchy)
- `EnvMapSampler` — hierarchical importance map (luminance pyramid) for HDRI

**Bounce limits**:
- Diffuse: 3 (default)
- Specular: 3 (default)
- Transmission: 10 (default)
- Hard cap at `kMaxPathLength`

**RTXDI integration**: When `useRTXDI = true`, RTXDI replaces NEE on primary hits only.
Secondary bounces still use conventional NEE.

**NRD integration**: Outputs per-sample `NRD radiance` + `hitDist` buffers. Separate trace
passes for delta reflection/transmission. `PathTracerNRD.slang` contains the demodulation
preprocessing needed before NRD denoising.

**Shader Execution Reordering (SER)**: Optional `SER` support for improved GPU warp coherence
on Ada Lovelace hardware. Reorders path state based on coherence hints (hit type, material).

### 2.2 ReSTIR DI — RTXDI Module

**Reference**: Bitterli et al. 2020 "Spatiotemporal Reservoir Resampling for Real-Time
Ray Tracing with Dynamic Direct Illumination" (SIGGRAPH 2020).

Falcor wraps NVIDIA's RTXDI SDK in `Rendering/RTXDI/RTXDI.{h,cpp,slang}`.

**Pipeline stages**:
1. **Light presampling** — stochastically presample lights into tiles (128 tiles × 1024 samples)
2. **Initial candidate generation** — per-pixel sample from local, infinite, env, and BRDF sources
3. **Visibility testing** — optional pre-validation of initial candidates
4. **Temporal resampling** — merge current reservoir with previous frame (M-cap: 20 frames)
5. **Spatial resampling** — merge with neighbors (radius: 30px, 1 neighbor, 5 iterations)
6. **Bias correction** — four modes: 1/M (fast biased), MIS (unbiased, requires shadow rays)
7. **Final shading** — evaluate BSDF at selected light sample

**Default parameters**:

| Parameter | Value |
|---|---|
| localLightCandidateCount | 24 |
| infiniteLightCandidateCount | 8 |
| envLightCandidateCount | 8 |
| brdfCandidateCount | 1 |
| spatialSamplingRadius | 30 px |
| spatialSampleCount | 1 |
| spatialIterations | 5 |
| maxHistoryLength | 20 frames |
| presampledTileCount | 128 |
| presampledTileSize | 1024 |

**Polymorphic light types** (`PolymorphicLight.slang`): point, spot, directional, area, distant,
emissive triangle — all handled via a unified type-erased interface in the GPU shader.

**RTXDIPass**: Standalone render pass (`Source/RenderPasses/RTXDIPass/`) demonstrating
RTXDI standalone usage: `PrepareSurfaceData.cs` → `RTXDI kernel` → `FinalShading.cs`.

### 2.3 Minimal Path Tracer

`Source/RenderPasses/MinimalPathTracer/` — a self-contained reference implementation:
- Single `.rt.slang` file: ray gen + closest-hit + any-hit + miss shaders
- Iterative bounce loop (no recursion)
- Lambertian BRDF only, NEE with analytic lights
- Intended as a learning/baseline reference, not production

### 2.4 Whitted Ray Tracer

`Source/RenderPasses/WhittedRayTracer/` — classic recursive ray tracer:
- Perfect specular reflection and refraction
- Point light shadow rays
- Not a GI method; included for pedagogical purposes

### 2.5 Differential Rendering

`Source/Falcor/DiffRendering/` + `Source/RenderPasses/WARDiffPathTracer/` — inverse rendering:
- WAR-Diff: Warped-Area Reparameterization for differentiable rendering
- Gradient accumulation through `AggregateGradients.cs`
- Used for material / scene optimization from reference images
- Not a GI method for real-time use

### 2.6 Screen-Space GI

Falcor does NOT have a screen-space GI pass. All GI is performed via ray tracing.

---

## 3. Advanced Lighting

### 3.1 Emissive Triangle Sampling

**Light BVH** (`Rendering/Lights/LightBVH.{h,cpp,slang}`):

Reference: Moreau and Clarberg 2019 "Importance Sampling of Many Lights on the GPU"
(Ray Tracing Gems, Chapter 18).

Binary tree hierarchy over emissive triangles:
- Internal nodes: spatial bounding box + child references
- Leaf nodes: references to emissive triangles (configurable max per leaf)
- Per-triangle bitmask encodes tree path (0=left, 1=right child)
- GPU-friendly traversal via `bindShaderData()` + Slang interface

Three sampling strategies:
- `LightBVHSampler` — uses spatial BVH hierarchy, best for many lights
- `EmissivePowerSampler` — power-proportional (no hierarchy), good for few lights
- `EmissiveUniformSampler` — uniform random selection, baseline

**Light BVH Refit**: `LightBVHRefit.cs.slang` updates BVH bounds for dynamic emissive geometry
without rebuilding the tree. Valid when only emitted radiance changes, not topology.

### 3.2 Environment Map Importance Sampling

`Rendering/Lights/EnvMapSampler.{h,cpp,slang}`:
- Hierarchical luminance importance map (multi-level pyramid)
- Setup compute pass (`EnvMapSamplerSetup.cs.slang`) builds the pyramid
- Samples drawn proportional to luminance via the hierarchy
- Enables efficient HDRI environment light sampling without brute force CDF

### 3.3 Volumetric Lighting

`Rendering/Volumes/`:

**Phase functions** (`PhaseFunction.slang`):
- `NullPhaseFunction` — pure absorption
- `IsotropicPhaseFunction` — `f_p = 1 / (4π)`
- `HenyeyGreensteinPhaseFunction` — single-lobe, anisotropy parameter `g ∈ (-1,1)`
- `DualHenyeyGreensteinPhaseFunction` — two-lobe blend for complex cloud/smoke scattering

**Grid volumes** (`Scene/Volume/GridVolume.{h,cpp,slang}`):
- OpenVDB-compatible `Grid<T>` with `BrickedGrid` sparse storage
- `GridVolumeSampler` — heterogeneous media sampling (delta tracking / ratio tracking)
- `HomogeneousVolumeSampler` — Beer-Lambert transmittance for homogeneous media
- Volume integration in the path tracer uses the `InteriorList` to track nested dielectrics

**GridVolume properties**:
- Density grid, emission grid, albedo grid (each separate `Grid<float>`)
- Per-volume temperature multiplier for blackbody emission
- Anisotropy stored per-voxel or globally

### 3.4 Area Lights (LTC)

Falcor does NOT implement LTC (Linearly Transformed Cosines) area lights as a rasterization
technique. Instead, area light contributions are sampled stochastically through the emissive
triangle sampling pipeline in the path tracer. LTC is a rasterization-era approximation
that is superseded by direct triangle sampling when ray tracing is available.

### 3.5 Material System for GI

`Rendering/Materials/` exposes a rich material interface for path tracing:
- `StandardBSDF` — metallic/roughness PBR (GGX specular + Lambertian diffuse)
- `DisneyDiffuseBRDF` / `FrostbiteDiffuseBRDF` / `OrenNayarBRDF` — diffuse alternatives
- `HairChiang16` — physically based hair BSDF (Chiang 2016)
- `ClothBRDF` — microfiber cloth model
- `MERL` / `RGL` — measured material data for research validation
- `LayeredBSDF` — arbitrary material layer stacking
- `IBSDF` interface — all materials implement `sample()`, `eval()`, `evalPdf()`

---

## 4. Denoising

### 4.1 SVGF (`SVGFPass`)

Reference: Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering"
(High Performance Graphics 2017).

**Pipeline** (5 passes):
1. `SVGFPackLinearZAndNormal.ps` — prepare linear depth and encoded normals
2. `SVGFReproject.ps` — reproject previous frame, accumulate moments (alpha=0.05, momentsAlpha=0.2)
3. `SVGFFilterMoments.ps` — 3×3 variance estimation from reprojected moments
4. `SVGFAtrous.ps` × N — iterative à-trous spatial filtering (wavelet decomposition)
5. `SVGFFinalModulate.ps` — multiply denoised illumination by albedo

**Key parameters**:
- `phiColor = 10.0` — color edge-stopping weight
- `phiNormal = 128.0` — normal edge-stopping weight
- `alpha = 0.05` — temporal blend (color)
- `momentsAlpha = 0.2` — temporal blend (moments)
- `filterIterations` — number of à-trous passes (typically 4-5)

**Inputs**: albedo, noisy color, emission, motion vectors, position, normals
**Output**: denoised illumination (separate from albedo for modulation)

### 4.2 NRD (`NRDPass`)

Reference: NVIDIA NRD (Normal-Rough-Depth) denoising library.

**Supported methods**:
- `RelaxDiffuseSpecular` — RELAX combined diffuse + specular (best for GI)
- `RelaxDiffuse` — RELAX diffuse only
- `ReblurDiffuseSpecular` — REBLUR temporal blur (lower latency than RELAX)
- `SpecularReflectionMv` — specular with motion vector guidance
- `SpecularDeltaMv` — sharp specular delta surfaces

**Integration**: NRD requires pre-packed radiance inputs. `PackRadiance.cs.slang` converts
raw path tracer radiance + hitDist into NRD's expected format. The PathTracer already outputs
`mpSampleNRDRadiance` / `mpSampleNRDHitDist` buffers for this purpose.

**Parameters**:
- `maxIntensity = 1000.0` — radiance clamping to suppress fireflies
- `disocclusionThreshold = 2.0` — depth-based disocclusion detection
- World-space or screen-space motion vectors

### 4.3 OptiX Denoiser (`OptixDenoiserPass`)

NVIDIA OptiX AI denoiser integration:
- Converts Falcor textures to OptiX buffers (4 compute passes for format conversion)
- Supports AOV (Albedo/Normal) guided denoising
- Supports motion-vector temporal denoising
- Output converted back to Falcor textures

**Use case**: Offline / production quality reference renders.

---

## 5. VBuffer vs GBuffer

Falcor uses a **V-buffer** (Visibility Buffer) as its primary GBuffer alternative:
- Stores `{ instanceID, primitiveID, barycentric UV }` at 8 bytes/pixel
- Full shading data (normals, textures, BSDF) is reconstructed on demand
- Enables material re-evaluation without storing all GBuffer channels
- `VBufferRaster.3d.slang` — rasterized V-buffer
- `VBufferRT.rt.slang` — ray-traced V-buffer (primary hit only via DXR)

The path tracer loads shading data from the V-buffer in `LoadShadingData.slang` per-bounce,
which is more bandwidth-efficient than a full GBuffer when tracing secondary rays.

---

## 6. UzEngine vs Falcor — Feature Comparison

### 6.1 GI Feature Matrix

| Feature | Falcor | UzEngine | Gap |
|---|---|---|---|
| DXR / Vulkan RT | Full DXR + inline RT | None | CRITICAL |
| TLAS / BLAS management | Full, refittable | None | CRITICAL |
| Wavefront path tracer | Full (GeneratePaths + Trace + Resolve) | None | CRITICAL |
| ReSTIR DI (RTXDI) | Full SDK integration | None | HIGH |
| Light BVH (emissive sampling) | Full (Moreau 2019) | None | HIGH |
| Env map importance sampling | Hierarchical luminance pyramid | None | HIGH |
| SVGF denoising | Full 5-pass implementation | None | HIGH |
| NRD denoising (REBLUR/RELAX) | Full SDK integration | None | HIGH |
| Volumetric rendering (heterogeneous) | GridVolume + phase functions | Fog only (homogeneous) | HIGH |
| Screen-space GI | None (uses RT instead) | SSGI (5-pass compute) | UzEngine ahead |
| World-space radiance probes | None (uses RT path tracer) | RadianceCache (SH, GDF-traced) | UzEngine ahead |
| Global Distance Field | None | 4-cascade clipmap GDF | UzEngine ahead |
| IBL baking (BRDF LUT, irradiance, prefilter) | Offline bake | GPU compute baker | Equivalent |
| SH projection | None | `sh_projection.{h,cpp}` | UzEngine ahead |
| Area lights (LTC) | RT emissive only | None | HIGH |
| Hair BSDF (Chiang 2016) | Yes | None | MEDIUM |
| Layered BSDF | Yes | None | MEDIUM |
| Measured BRDF (MERL/RGL) | Yes | None | LOW |
| OptiX denoiser | Yes | None | LOW |
| Differential rendering | Yes (WAR-Diff) | None | LOW |
| V-buffer (visibility buffer) | Yes | No (classic GBuffer) | MEDIUM |

### 6.2 UzEngine Strengths Not in Falcor

UzEngine has built several rasterization-based real-time GI approximations that Falcor
intentionally excludes (Falcor targets ray tracing research):

1. **SSGI (Screen-Space GI)**: 5-pass compute pipeline (reduce + trace + spatial denoise +
   temporal accum + bilateral upsample) producing one-bounce diffuse indirect. Falcor has
   no equivalent because it uses path tracing.

2. **Global Distance Field**: 4-cascade clipmap SDF (64³ per cascade, extents 25m/100m/400m/1600m).
   Used as the ray tracing primitive for software GI. Falcor uses hardware BVH instead.

3. **Radiance Cache**: World-space 16³ probe grid with L2 SH (9 coefficients, 3 channels).
   32 rays/probe/frame through GDF, EMA temporal blend. Reference: McGuire 2017.
   Falcor uses real path tracing instead of probe approximations.

4. **IBL GPU Compute Baker**: Bakes BRDF LUT (512×512 RG16F), irradiance cubemap (64×64),
   and prefiltered specular (1024×1024, 8 mips) entirely on GPU. Falcor has equivalent
   offline baking but no real-time bake pipeline.

### 6.3 Critical Missing Features in UzEngine

#### Priority 1 — CRITICAL (blocks real-time RT GI)

- **No DXR ray tracing pipeline**: UzEngine has no acceleration structure, no TLAS/BLAS,
  no shader table, and no `TraceRay()` infrastructure. Adding this is a prerequisite for
  all other RT features.

#### Priority 2 — HIGH VALUE (reachable near-term)

- **ReSTIR DI**: Even without full path tracing, ReSTIR DI can be layered on top of the
  existing deferred pipeline to dramatically improve direct lighting quality with many lights.
  Requires: TLAS, shadow ray infrastructure, and RTXDI SDK integration.

- **Light BVH**: Required for scenes with many emissive lights. Currently UzEngine has no
  emissive triangle sampling. LightBVH (Moreau 2019) is a well-understood technique.

- **SVGF denoising**: UzEngine's SSGI has a custom denoiser (cross-bilateral + temporal accum)
  that is weaker than SVGF. Implementing Schied 2017 SVGF would improve GI denoising quality
  significantly even for screen-space signals.

- **Heterogeneous volumetric rendering**: Falcor's GridVolume supports OpenVDB-style heterogeneous
  density fields with delta tracking. UzEngine only has homogeneous exponential height fog.

#### Priority 3 — MEDIUM VALUE

- **V-buffer**: Replace the classic GBuffer with a visibility buffer for better bandwidth
  efficiency when secondary rays are added. Falcor's VBuffer stores only 8 bytes/pixel.

- **NRD integration**: Once DXR is added, NRD (RELAX/REBLUR) denoising should be integrated
  via the same SDK path Falcor uses. NRD is purpose-built for RT denoising.

- **Environment map importance sampling**: UzEngine's IBL uses a simple spherical sample;
  adding a luminance hierarchy (Falcor's `EnvMapSampler` pattern) would improve IBL quality.

---

## 7. Recommended Adoption Roadmap for UzEngine

### Phase 1 — RT Infrastructure (prerequisite for all else)

1. Add `RtAccelerationStructure` abstraction to UzEngine's RHI
   - TLAS / BLAS handles
   - Build flags (AllowUpdate, AllowCompaction, PreferFastTrace)
   - Incremental refit for dynamic meshes
2. Add shader table management to RHI (ray gen, closest-hit, any-hit, miss)
3. Add `TraceRay()` and `TraceRayInline()` to the RHI command list interface
4. Build scene TLAS from static mesh list on first frame

### Phase 2 — Minimal Path Tracer (learning path)

5. Implement `MinimalPathTracer` equivalent: ray gen + Lambert BRDF + NEE + env map miss
   - Single `.hlsl` file, iterative bounce loop
   - Use existing GBuffer as primary hit data source
6. Validate against Falcor's `MinimalPathTracer` reference renders

### Phase 3 — ReSTIR DI

7. Integrate RTXDI SDK (or implement reservoir resampling from scratch)
   - Initial candidate generation from existing light list
   - Temporal reservoir reuse (20-frame history)
   - Spatial reuse (30px radius, 5 iterations)
8. Layer ReSTIR DI on top of existing deferred pass for direct lighting quality

### Phase 4 — Denoising

9. Implement SVGF for SSGI denoising (replaces current cross-bilateral filter)
   - SVGFReproject → SVGFFilterMoments → SVGFAtrous × 4 → SVGFFinalModulate
10. Evaluate NRD SDK integration for RT-specific denoising

### Phase 5 — Light BVH and Full PT

11. Implement `LightBVH` (Moreau 2019) for emissive triangle importance sampling
12. Upgrade to wavefront path tracer (GeneratePaths + Trace + Resolve)
13. Add NRD RELAX/REBLUR for path traced output denoising

---

## 8. References

- Bitterli et al. 2020 "Spatiotemporal Reservoir Resampling" — SIGGRAPH 2020 (ReSTIR DI)
- Moreau and Clarberg 2019 "Importance Sampling of Many Lights on the GPU" — RTGems Ch.18 (LightBVH)
- Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering" — HPG 2017 (SVGF)
- McGuire et al. 2017 "Real-Time Global Illumination using Precomputed Light Field Probes" (RadianceCache)
- Henyey and Greenstein 1941 "Diffuse radiation in the Galaxy" (Phase Function)
- NVIDIA NRD SDK — developer.nvidia.com/rendering-technologies/denoising
- NVIDIA RTXDI SDK — developer.nvidia.com/rtxdi
- Chiang et al. 2016 "A Practical and Controllable Hair and Fur Model for Production Path Tracing" (Hair BSDF)
