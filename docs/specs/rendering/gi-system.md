<!-- doc-meta
type: spec
category: rendering/gi
title: Global Illumination System Specification
created: 2026-03-20
updated: 2026-10-08
status: active
authors: UzEngine Core Team
references: Wright 2015 SDF GI, Majercik 2019 DDGI, Hillaire 2016 Volume Rendering
-->

# Global Illumination System Specification

## Overview

### Indirect diffuse exchange convention

Indirect diffuse irradiance is exchanged as E/pi in linear scene-radiance units
(albedo excluded, Lambert 1/pi included, sky colour x sky intensity included by
every traced writer), never pre-multiplied by AO; the consumer multiplies by
(1-metallic)*baseColor*(1-dielectricScatter) once and by ao^k once;
r.GI.Intensity scales traced probe sources only, so like-for-like blending needs
r.GI.Intensity=1.

The legacy screen trace stores E_screen/pi with zero radiance for misses. Its AO
target stores the cosine-weighted miss fraction, so the unified diffuse is
`ssgi + saturate(miss) * fallback`: screen hits own the covered hemisphere and
the DDGI/RC/LPV/sky cascade fills only the uncovered fraction. The SDF final
gather replaces the cascade and bypasses this blend. Sky tint applies to the sky
fallback before blending, not to traced scene radiance. The compact SH packing
already bakes A_l/pi, so no additional pi conversion belongs in the consumer.

This convention lives here rather than in `docs/specs/rendering/cb-layouts.md`,
which does not exist; `docs/analysis/lighting/uzengine/cb-layouts.md` describes
constant-buffer memory layout rather than radiometric units.

UzEngine implements a **three-tier SDF global illumination system** (Wright 2015 SDF), with Majercik 2019 DDGI as the DXR option. The system computes indirect lighting via:

1. **Tier 1: Mesh SDF** — Per-mesh signed distance fields (32×32×32, R16F) baked on the GPU
2. **Tier 2: Global Distance Field** — 4-level cascaded clipmaps (64×64×64 each) compositing all mesh SDFs
3. **Tier 3: Radiance Cache** — World-space probe grid (16×16×16 = 4096 probes) with L2 spherical harmonics

> **Brixelizer GI, listed here as Tier 4 until 2026-10-08, is dormant and contributes nothing.** The AMD FidelityFX Brixelizer integration (SDK 1.1.4) exists as source only: the build compiles it as a no-op stub because the MSVC-built wrapper DLL it needs is never built, the pass has not been constructed since 2026-04-10 or dispatched since 2026-04-07, and its task (`phase0_brixelizer-gi-integration`) was closed as superseded on 2026-10-08. The facts are in the Tier 4 section at the end of this page. The GI that runs on Sponza is Tiers 1-3 with the SDF final gather (`r.GI.FinalGather`) and SDF reflections (`r.GI.Reflections`), all enabled by `sample/projects/sponza.uzproject:12-14` and described in `docs/engine/12-rendering/33-sdf-gi-final-gather.md`; DDGI (`r.RayTracing.GI`) is the DXR option (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-06-ddgi-dedup.md`).

The radiance cache is updated incrementally. Each frame, `r.GI.RadianceCache.ProbesPerFrame` (1024) of its 4096 probes each trace `r.GI.RadianceCache.RaysPerProbe` (32, at most 64) rays through the global distance field. The rays follow a Hammersley sequence over the full sphere. Their radiance is projected into L2 SH and blended with the probe's history at α = 0.12 (`RadianceCache::kTemporalBlendAlpha`; `runtime/engine/renderer/gi/radiance_cache.h:91-103`, `runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl:358-374`, `:622-630`).

Deferred lighting takes the indirect diffuse of each pixel from one source, by priority: DDGI, then the radiance cache, then the light probe volume (inert), then the sky SH (`runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:739-1060`). When the SDF final gather is on, it replaces that result in every pixel (`:1148-1155`). See [Irradiance sources and priority](#irradiance-sources-and-priority-as-built-2026-10-08) below. (Corrected 2026-10-08 under `phase0_indoor-lighting-system`: this paragraph used to describe 32 Fibonacci rays per probe per frame and the cached irradiance applied to all pixels, and the feature list below gave α = 0.05.)

**Key features:**
- Camera movement updates the GI in real time (toroidal clipmaps, only the entering slabs are recomposed since 2026-10-05); a mesh that moves, appears or changes material recomposes only its neighbourhood (per-entity diff, truncation band `r.GI.GDF.BandVoxels`) and only the radiance-cache probes and SSGI pixels inside the change boxes drop their history — a moving object no longer resets the GI (block B.2 phases 3-4, `docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-02-gdf-incremental.md`)
- Temporal coherence — EMA blending (α = 0.12 as built; the March design said 0.05) reduces noise. A freshly loaded scene keeps converging for about 200 frames (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-02-gdf-incremental.md:152-154`)
- Memory efficient — ~6 MB GPU memory for full 3-tier system
- Cascaded coverage — 2.5 km far-field distance across 4 GDF cascades

---

## Architecture Overview

### Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│ TIER 1: MESH SDF GENERATION (Per-Mesh)                         │
├─────────────────────────────────────────────────────────────────┤
│ Input: Mesh vertices + indices                                  │
│ ├─ GPU Compute: mesh_sdf_bake_cs.hlsl (32³ voxels, R16F)       │
│ └─ Output: MeshSDF struct (texture3D, srvIndex, origin, extent) │
└──────────────────────────────────┬──────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────┐
│ TIER 2: GLOBAL DISTANCE FIELD (Clipmap Composite)              │
├─────────────────────────────────────────────────────────────────┤
│ Input: All MeshSDFs + camera position                           │
│ ├─ 4 Cascades (2500/10000/40000/160000 cm extents)             │
│ ├─ Each: 64³ R16F texture, camera-tracked center               │
│ ├─ Dirty-flag tracking for incremental updates                 │
│ └─ GPU Compute: global_sdf_composite_cs.hlsl                   │
│    └─ Per-voxel: sample all mesh SDFs, store min distance      │
└──────────────────────────────────┬──────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────┐
│ TIER 3: RADIANCE CACHE (Probe Grid)                            │
├─────────────────────────────────────────────────────────────────┤
│ Input: Global Distance Field + sky environment                  │
│ ├─ 16³ probes = 4096 total (uniform world-space grid)          │
│ ├─ Per-probe: 7 float4s (L2 SH packed, 3 channels)             │
│ │  [0..2]: R/G/B L0 + L1 terms                                 │
│ │  [3..5]: R/G/B L2 terms                                      │
│ │  [6]: remaining L2 terms                                     │
│ ├─ GPU Compute: radiance_cache_trace_cs.hlsl                    │
│ │  └─ 32 Hammersley sphere rays per probe (at most 64),         │
│ │     1024 of 4096 probes per frame → sphere march → SH         │
│ └─ Temporal blend: SH_new = (1-α)·SH_old + α·SH_trace           │
│    where α = 0.12 (kTemporalBlendAlpha, as built)               │
└──────────────────────────────────┬──────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────┐
│ INTEGRATION: DEFERRED LIGHTING                                  │
├─────────────────────────────────────────────────────────────────┤
│ Pixel Shader (deferred_lighting_ps.hlsl, as built)              │
│ ├─ One irradiance source per pixel, by priority:                │
│ │  DDGI > radiance cache > light probe volume (inert) > sky SH  │
│ ├─ Radiance cache: 8 probes, trilinear × facing² weights,       │
│ │  invalid probes skipped, L2 SH at the normal, grid clamped    │
│ ├─ r.GI.FinalGather replaces the result in every pixel          │
│ └─ diffuse = (1-metallic) × E × baseColor × (1 - scatter)       │
└─────────────────────────────────────────────────────────────────┘
```

---

## Tier 1: Mesh SDF Generation

### Overview

Each mesh that contributes to GI gets a signed distance field baked on the GPU. The SDF is a dense 3D texture (R16F, typically 32³ voxels) that stores the signed distance to the nearest triangle surface, in world units (cm). Negative distances indicate points inside the mesh; positive distances indicate outside.

**Why GPU baking:** The original asset importer's CPU-based SDF bake is 2500× slower than GPU compute. GPU bake time: ~20 ms per 32³ mesh. CPU bake time: ~50 seconds.

### C++ Interface: `MeshSDF` Struct

```cpp
struct MeshSDF {
    RHITextureHandle texture3D;           // R16F 3D texture of distances
    uint32_t         srvIndex;            // Bindless SRV index in descriptor heap
    float            volumeOrigin[3];     // World-space center of SDF volume (cm)
    float            volumeExtent[3];     // World-space half-extents (cm)
    uint32_t         resolution;          // Voxels per axis (32 typical)
    bool             valid;               // True if texture is created and uploaded
    float            invTransform[12];    // Cached world-to-local 3×4 matrix (row-major)
    RHIBufferHandle  pendingTriBuffer;    // Transient GPU triangle buffer (deferred destruction)
};
```

**Memory per mesh SDF:**
- Texture: 32³ × 2 bytes (R16F) = 64 KB
- Descriptor heap entry: 1 SRV
- Constant buffer data (in GDF composite): 96 bytes

### GPU Bake: `MeshSdfBaker::BakeMeshGPU()`

> Since 2026-10-05 (block B.3) the scene renderer does not call this whole bake: `MeshSdfBakeQueue`
> runs `MeshSdfBaker::PrepareCPU` (volume placement, triangle records, BVH) on worker threads,
> looks the field up in the disk cache `cache/meshsdf` (`AssetCache`; key = mesh bytes + bake
> options + bake shader bytecode; entry = header + LZ4 block of the GPU readback and the records,
> XXH64 checksum), and on a miss records the dispatch in the frame's command list in Z slabs
> (`r.GI.MeshSdf.BakeSlicesPerFrame`, unthrottled during the preload). The field is valid only when
> the renderer's frame fence has passed; `IsSceneFullyLoaded` counts the bakes in flight; switching
> `r.RadianceCache` on after load queues the missing bakes. `BakeMeshGPU` remains for tools.


**Input:**
- Vertex positions (float × 3 per vertex)
- Triangle indices (uint32 × 3 per triangle)
- Target resolution (32 or 64 voxels per axis)

**Process:**

1. Compute mesh AABB from vertex bounds
2. Expand AABB by 10% for narrow-band padding
3. Compute voxel grid step size: `voxelSize = AABB.extent / resolution`
4. Upload triangle data to GPU StructuredBuffer (32-byte stride per vertex, 64 bytes per triangle)
5. Dispatch compute shader: `[numthreads(8, 8, 8)]` → 32³ = 32,768 threads

**Per-voxel compute kernel (mesh_sdf_bake_cs.hlsl):**

```hlsl
[numthreads(8, 8, 8)]
void main(uint3 voxelPos : SV_DispatchThreadID)
{
    // 1. Convert voxel grid coordinate to world position at voxel center
    float3 worldPos = volumeMin + voxelPos * voxelSize + voxelSize * 0.5f;

    // 2. Brute-force iterate all triangles
    float minDist = 1e6f;
    float3 closestPoint;

    for (uint tri = 0; tri < triangleCount; tri++)
    {
        // 3. Find closest point on triangle surface
        float3 closest = ClosestPointOnTriangle(worldPos, v0, v1, v2);
        float dist = distance(worldPos, closest);

        if (dist < minDist)
        {
            minDist = dist;
            closestPoint = closest;
        }
    }

    // 4. Determine sign via pseudo-normal (dot with face normal)
    float3 faceNormal = precomputedFaceNormal;
    float3 toSurface = closestPoint - worldPos;
    float signBit = dot(toSurface, faceNormal) < 0.0f ? -1.0f : 1.0f;

    // 5. Write signed distance to output texture
    float signedDist = signBit * minDist;
    outputUAV[voxelPos] = signedDist;
}
```

**Complexity:** O(V × T) where V = voxels (32³ = 32k) and T = triangles. For a 50k-triangle mesh, ~1.6 billion ops. GPU handles this in ~20 ms via 32k parallel threads.

### Closest Point on Triangle

Implemented via Ericson's Voronoi region decomposition (Real-Time Collision Detection Ch 5.1.5). Classifies the query point into one of 7 regions (3 vertex, 3 edge, 1 face) and projects onto the corresponding feature.

```cpp
/// @source Real-Time Collision Detection Ch 5.1.5 — ClosestPtPointTriangle
float3 ClosestPointOnTriangle(float3 p, float3 a, float3 b, float3 c)
{
    float3 ab = b - a;
    float3 ac = c - a;
    float3 ap = p - a;

    // Compute barycentric coordinates and test all 7 Voronoi regions
    // Region A: vertex a, Region AB: edge ab, Region ABC: face, etc.
    // [Full algorithm in mesh_sdf_bake_cs.hlsl lines 71-150]
}
```

### SDF Sampling

Mesh SDFs are sampled in local space. The GDF composite shader transforms world positions into each mesh's local SDF coordinate system, then trilinear-samples the R16F texture.

```hlsl
/// @source sdf_common.hlsli
float SampleMeshSDF(
    Texture3D<float> sdfTexture,
    SamplerState sdfSampler,
    float3 worldPos,
    float3 volumeOrigin,
    float3 volumeExtent,
    uint resolution)
{
    // Transform world → normalized [0,1]³ UV within the SDF volume
    float3 localPos = (worldPos - volumeOrigin) / (volumeExtent * 2.0f) + 0.5f;

    // Out-of-volume returns large positive distance (outside)
    if (any(localPos < 0.0f) || any(localPos > 1.0f))
        return 1e6f;

    return sdfTexture.SampleLevel(sdfSampler, localPos, 0).r;
}
```

---

## Tier 2: Global Distance Field (Cascaded Clipmaps)

### Overview

The global distance field (GDF) composites all mesh SDFs into a hierarchical 4-level clipmap structure. Each clipmap is a 64³ R16F texture centered on the camera. The cascades cover 4× the extent of the previous level, achieving coverage from 25 m to 1.6 km.

**Why cascades:** A single 64³ texture covers ~5 km, but fine detail is lost. Cascades provide detail at multiple scales: cascade 0 is fine (per-voxel ~39 cm), cascade 3 is coarse (per-voxel ~2500 cm). Ray tracing starts in cascade 0, falls through to coarser cascades as the ray exits each volume.

### Clipmap Configuration

| Cascade | Extent (cm) | Radius (m) | Voxel Size (cm) | Coverage |
|---------|-------------|------------|-----------------|----------|
| 0       | 2500        | 25         | 39              | Fine detail (character scale) |
| 1       | 10000       | 100        | 156             | Mid field (small rooms) |
| 2       | 40000       | 400        | 625             | Far field (large outdoor areas) |
| 3       | 160000      | 1600       | 2500            | Distant field (distant mountains) |

**Memory:** 4 × (64³ × 2 bytes + SRV + UAV) = ~2 MB + descriptor overhead

### Clipmap Update Strategy

#### Centering and Dirty Flags

Clipmaps are camera-tracked. The center snaps to a voxel grid boundary to prevent temporal aliasing:

```cpp
// In GlobalDistanceField::Update(cameraPos)
for (uint i = 0; i < kNumClipmaps; i++)
{
    // Snap center to voxel grid for this cascade
    float voxelSize = kClipmapExtents[i] * 2.0f / kResolution;
    Vec3 snappedCenter = Vec3(
        floor(cameraPos.x / voxelSize) * voxelSize,
        floor(cameraPos.y / voxelSize) * voxelSize,
        floor(cameraPos.z / voxelSize) * voxelSize
    );

    // Mark dirty when the snapped centre moved by a whole voxel; the whole-voxel shift is recorded and the composite rewrites only the entering slabs (toroidal storage)
    float tolerance = voxelSize * 0.5f;
    if (distance(snappedCenter, m_clipmaps[i].center) > tolerance)
    {
        m_clipmaps[i].center = snappedCenter;
        m_clipmaps[i].dirty = true;
    }
}
```

#### Lazy Composite

Only dirty clipmaps are recomposited each frame. A typical frame composites 1-2 cascades (others remain cached). Full recomposite: ~3 ms for cascade 0, ~24 ms for cascade 3 (64³ voxels, 32 iterations of 256 mesh SDFs).

### GPU Composite Shader

Each thread handles one voxel. Brute-force iterates all mesh SDFs, samples each at the world position, and stores the global minimum:

```hlsl
// global_sdf_composite_cs.hlsl
[numthreads(4, 4, 4)]
void main(uint3 voxelPos : SV_DispatchThreadID)
{
    if (any(voxelPos >= g_resolution))
        return;

    // 1. Voxel center in world space
    float3 clipmapMin = float3(g_clipmapCenterX, g_clipmapCenterY, g_clipmapCenterZ)
                      - g_clipmapExtent;
    float voxelSize = (g_clipmapExtent * 2.0f) / float(g_resolution);
    float3 worldPos = clipmapMin + (float3(voxelPos) + 0.5f) * voxelSize;

    // 2. Sample all mesh SDFs at this world position
    float minDist = 1e6f;
    for (uint m = 0; m < g_meshCount; m++)
    {
        MeshSdfDesc mesh = g_meshSDFs[m];

        // Transform world → mesh local space
        float3 localPos;
        localPos.x = dot(mesh.invTransformRow0.xyz, worldPos) + mesh.invTransformRow0.w;
        localPos.y = dot(mesh.invTransformRow1.xyz, worldPos) + mesh.invTransformRow1.w;
        localPos.z = dot(mesh.invTransformRow2.xyz, worldPos) + mesh.invTransformRow2.w;

        // Sample mesh SDF
        float meshDist = SampleMeshSDF(...);
        minDist = min(minDist, meshDist);
    }

    // 3. Write global minimum distance
    RWTexture3D<float> output = ResourceDescriptorHeap[g_outputUav];
    output[voxelPos] = minDist;
}
```

**Dispatch:** `Dispatch(16, 16, 16)` → 4096 threads per voxel layer. For 64³ volume: 64 layers, 4096 threads/layer = 262,144 threads total.

### Root Constants for Composite

```cpp
struct GdfCompositeConstants {
    uint32_t outputUav;           // [0] UAV index for target clipmap texture
    uint32_t meshCount;           // [1] Number of mesh SDFs
    uint32_t clipmapIndex;        // [2] Cascade index (0-3)
    uint32_t resolution;          // [3] 64
    float    clipmapCenterX;      // [4] World-space center X (cm)
    float    clipmapCenterY;      // [5] World-space center Y (cm)
    float    clipmapCenterZ;      // [6] World-space center Z (cm)
    float    clipmapExtent;       // [7] Half-extent in world units (cm)
};
// 32 bytes (8 × uint32), bound at root param [0]
```

### Mesh SDF Buffer (Constant Buffer Array)

Per-mesh data uploaded to a cbuffer array at b2:

```cpp
struct GdfMeshSdfGPU {
    float originX, originY, originZ;  // World-space volume center
    float padding0;
    float extentX, extentY, extentZ;  // World-space half-extents
    float padding1;
    float invTransform[12];           // World-to-local 3×4 matrix (row-major)
    uint32_t srvIndex;                // Bindless SRV for the R16F texture
    uint32_t resolution;              // Voxels per axis
    uint32_t _pad2[2];
};
// 96 bytes per mesh, max 256 meshes = 24 KB
```

---

## Tier 3: Radiance Cache (Probe Grid + SH)

### Overview

The radiance cache is a world-space probe grid with L2 spherical harmonics. Each frame, probes trace rays through the GDF, accumulate incoming radiance into SH coefficients, and blend with history. The deferred lighting pass samples cached irradiance via trilinear interpolation.

> **As built (checked 2026-10-08).** The constants and pseudo-code in the rest of this section are
> the March 2026 design. The trace that ships
> (`runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl`, a real implementation since `29c83ba5`,
> 2026-04-07, which marched a stub distance field until the 2026-09-08 SDF port) differs as follows:
> - **Rays:** Hammersley points mapped to the full sphere (`:358-370`), jittered per frame
>   (`:483`), not a Fibonacci spiral. One probe per thread, `[numthreads(64, 1, 1)]` (`:374`),
>   dispatched as `(probeBudget + 63) / 64` groups
>   (`runtime/engine/renderer/gi/radiance_cache.cpp:920-921`).
> - **Budget:** `r.GI.RadianceCache.RaysPerProbe` rays (32, at most 64,
>   `radiance_cache_trace_cs.hlsl:140`) for `r.GI.RadianceCache.ProbesPerFrame` probes per frame
>   (1024 of 4096, so each probe is retraced every 4 frames;
>   `runtime/engine/renderer/gi/radiance_cache.h:91-103`, `radiance_cache.cpp:56-58`). The whole
>   grid is retraced on the first write, after a history invalidation and on a grid recentre
>   (`radiance_cache.cpp:700-701`, `:857-859`).
> - **Blend:** α = 0.12 (`RadianceCache::kTemporalBlendAlpha`, `radiance_cache.h:95`, passed at
>   `radiance_cache.cpp:769`, applied at `radiance_cache_trace_cs.hlsl:622-630`). The history is
>   shifted on grid recentres and replaced inside regional change boxes (block B.2).
> - **Grid:** centred on the camera, with spacing 2 x extent / 15 (`radiance_cache.cpp:116`), not
>   2 x extent / 16. `r.GI.RadianceCache.GridExtent` defaults to 5000 cm; the Sponza project uses
>   2000.
> - **Shading:** hits are shaded with the field normal, a sun shadow ray and the previous frame's
>   probe irradiance (multi-bounce feedback). Escaping rays sample the sky cubemap times
>   `skyLightColor x skyLightIntensity` (`radiance_cache_trace_cs.hlsl:529-532`,
>   `radiance_cache.cpp:839-841`). Probes carry a validity flag
>   (`docs/engine/12-rendering/33-sdf-gi-final-gather.md:82-83`), and the candidate classifications
>   can relocate them (`r.GI.RadianceCache.ProbeClassification`, `.ReachRelocate`;
>   `radiance_cache.cpp:64-75`; the Sponza project sets 3 and 1).
> - **Cost:** the trace measures 0.72 ms at 1080p on Sponza
>   (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-01-pass-timings.md:76`).

### Probe Grid Configuration

- **Grid size:** 16 × 16 × 16 = 4096 probes
- **Grid extent:** 5000 cm (50 m) half-extent from camera, centered on camera
- **Probe spacing:** 2 × 5000 / 16 = 625 cm (6.25 m)
- **Total memory:** 4096 × 7 × 4 bytes = ~449 KB
- **Update cost:** 4096 probes × 32 rays = 131,072 rays/frame at 1080p

### Spherical Harmonics Packing

Each probe stores L2 SH (9 coefficients per channel, 3 channels = 27 values total) packed into 7 float4s:

```cpp
// Per-probe SH buffer layout (7 float4s per probe)
struct ProbeShCoeffs {
    float4 rSH0;  // [0] R channel: L0 (1 coeff) + L1 (3 coeffs)
    float4 gSH0;  // [1] G channel: L0 + L1
    float4 bSH0;  // [2] B channel: L0 + L1
    float4 rSH1;  // [3] R channel: L2 (5 coeffs)
    float4 gSH1;  // [4] G channel: L2
    float4 bSH1;  // [5] B channel: L2
    float4 sh6;   // [6] Remaining L2 terms (R_xy, G_xy, B_xy, unused)
};
```

**Packing pattern (reference SkyIrradianceEnvironmentMap):**
- `rSH0 = (L0_dc, L1x, L1z, L1y)`
- `rSH1 = (L2_0, L2_1, L2_2, L2_-1)`
- `sh6.x = L2_xy` (for R channel)

### Ray Tracing: Fibonacci Sampling

Each probe traces 32 rays per frame via Fibonacci spiral distribution with per-frame jitter for temporal variation:

```hlsl
/// @ref Keinert et al. 2015 "Spherical Fibonacci Point Sets"
float3 SphericalFibonacci(uint sampleIndex, uint totalSamples, uint seed)
{
    static const float kGoldenRatio = 1.6180339887f;
    float i = float(sampleIndex) + float(seed % 97) * 0.618f;

    // Spherical coordinates on unit sphere
    float phi = 2.0f * 3.14159f * frac(i / kGoldenRatio);
    float cosTheta = 1.0f - 2.0f * i / float(totalSamples);
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

    return float3(
        sinTheta * cos(phi),
        sinTheta * sin(phi),
        cosTheta
    );
}
```

**Fibonacci vs uniform:** Fibonacci spiral provides excellent stratification without clustering, superior to random or grid sampling for low sample counts.

### Probe Update Shader: `radiance_cache_trace_cs.hlsl`

```hlsl
[numthreads(16, 16, 1)]  // 256 threads per wave
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint probeIndex = threadId.x + threadId.y * 16;
    if (probeIndex >= g_totalProbes)
        return;

    // 1. Compute probe world position from grid coordinates
    uint3 probeCoord = uint3(
        probeIndex % 16,
        (probeIndex / 16) % 16,
        (probeIndex / (16 * 16))
    );
    float3 probeWorldPos = g_gridOrigin + float3(probeCoord) * g_probeSpacing;

    // 2. Accumulate SH from 32 rays
    float4 shAccum[7] = { (float4)0, ... };

    for (uint rayIdx = 0; rayIdx < g_raysPerProbe; rayIdx++)
    {
        // 3. Generate ray direction via Fibonacci sampling
        float3 rayDir = SphericalFibonacci(rayIdx, g_raysPerProbe,
                                           probeIndex + g_frameIndex * 9973);

        // 4. Trace ray through GDF via sphere marching
        float hitDist;
        float3 hitNormal;
        bool hit = SphereMarchGlobalSDF(gdfClipmaps, probeWorldPos, rayDir,
                                        g_maxTraceDistance, hitDist, hitNormal);

        // 5. Evaluate outgoing radiance at hit point
        float3 incomingRadiance;
        if (hit)
        {
            // Single-bounce indirect: evaluate light at hit point
            float3 hitPos = probeWorldPos + rayDir * hitDist;
            incomingRadiance = EvaluateLightingAtPoint(hitPos, hitNormal);
        }
        else
        {
            // Miss: use sky environment
            incomingRadiance = SampleSkyEnvironment(rayDir);
        }

        // 6. Project incoming radiance onto L2 SH basis
        float4 shBasis[9] = ComputeSHBasis(rayDir);
        for (uint coeff = 0; coeff < 9; coeff++)
        {
            shAccum[coeff] += float4(incomingRadiance, 1.0f) * shBasis[coeff];
        }
    }

    // 7. Divide by sample count to get average irradiance
    for (uint i = 0; i < 9; i++)
        shAccum[i] /= g_raysPerProbe;

    // 8. Temporal blend with previous frame history
    uint baseIndex = probeIndex * 7;
    for (uint i = 0; i < 7; i++)
    {
        float4 oldSH = g_shBuffer[baseIndex + i];
        float4 newSH = shAccum[i];
        float4 blended = lerp(oldSH, newSH, g_temporalBlendAlpha);
        g_shBuffer[baseIndex + i] = blended;
    }
}
```

**Dispatch:** `Dispatch(1, 1, 1)` with `[numthreads(16, 16, 1)]` → 256 threads handle 256 probes per batch. For 4096 probes, dispatch 16 times (or use indirect).

### Temporal Blending

EMA blending over history provides temporal stability with low latency:

```
SH_new = (1 - α) × SH_old + α × SH_traced
```

where α = 0.05 in this design. As built, α = 0.12 (`RadianceCache::kTemporalBlendAlpha`). With each probe retraced every fourth frame, that is about 33 frames per e-fold before the multi-bounce feedback, and a freshly loaded scene keeps converging for about 200 frames (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-02-gdf-incremental.md:112-117`, `:152-154`).

### Root Constants for Trace

```cpp
struct RadianceCacheTraceConstants {
    float    gridOriginX, gridOriginY, gridOriginZ;  // Grid corner (minimum)
    float    probeSpacing;                           // Distance between probes (cm)

    uint32_t probesPerAxis;                          // 16
    uint32_t totalProbes;                            // 4096
    uint32_t raysPerProbe;                           // 32
    uint32_t frameIndex;                             // Frame counter for jitter

    float    maxTraceDistance;                       // Max trace distance (cm)
    float    temporalBlendAlpha;                     // 0.05
    uint32_t shBufferUav;                            // UAV index for SH buffer
    uint32_t _pad;

    // GDF clipmap parameters (4 cascades)
    float4   gdfClipmap0;  // xyz = center, w = extent
    float4   gdfClipmap1;
    float4   gdfClipmap2;
    float4   gdfClipmap3;

    uint32_t gdfClipmapSrv0, gdfClipmapSrv1, gdfClipmapSrv2, gdfClipmapSrv3;
    uint32_t numGdfClipmaps;

    // Sky and light parameters
    float3   skyColor;
    float    skyIntensity;
    float3   lightDir;
    float    lightIntensity;
    float3   lightColor;
    float    groundAlbedo;
};
// 176 bytes, bound at b2 (cbuffer root param)
```

---

## Integration: Deferred Lighting

### Irradiance sources and priority (as built, 2026-10-08)

> The subsections after this one are the March 2026 design. Where they disagree with this one,
> this one describes the code. The shipped consumer applies no `x pi` or `/ pi`: the SH is
> cosine-convolved, so its evaluation is the irradiance E
> (`deferred_lighting_ps.hlsl:1074-1078`). It has no boundary fade: the grid coordinate is clamped
> (`deferred_lighting_ps.hlsl:889`). And the sources replace one another instead of adding up.

`runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` takes the indirect diffuse irradiance
of a pixel from one source, by priority (`deferred_lighting_ps.hlsl:739-754`):

1. **DDGI** (`deferred_lighting_ps.hlsl:759-880`).
   - **When it is bound.** Only while the DXR writer is on: `r.RayTracing` and `r.RayTracing.GI`
     (`RTManager::IsGIEnabled`), a completed first update, and an initialised radiance cache
     (`runtime/engine/renderer/frame/frame_constants.cpp:885-904`). The writer
     (`runtime/engine/renderer/frame/frame_gi_passes.cpp:329-376`) runs only with a TLAS that holds
     instances (`:336-338`) and inside the radiance cache's pass, so it also needs `r.RadianceCache`.
   - **What it reads.** Eight probes on the radiance cache's grid, with the same origin and spacing
     (`frame_constants.cpp:893-895`). For each probe it samples the octahedral irradiance at the
     shading normal and weights it by the trilinear weight times a Chebyshev visibility from the
     depth atlas, sharpened as p³ (`deferred_lighting_ps.hlsl:846-855`). The result is multiplied
     by `r.GI.Intensity` (`deferred_lighting_ps.hlsl:876`, `frame_constants.cpp:883`).
   - **When it answers.** Only when the visibility-weighted weight sum exceeds 1e-6
     (`deferred_lighting_ps.hlsl:874-878`).
   - **Missing.** There is no backface or normal-bias weight.
2. **Radiance cache** (`deferred_lighting_ps.hlsl:882-970`).
   - **When it is bound.** When `r.RadianceCache` is on and the cache has produced a result
     (`frame_constants.cpp:796-819`).
   - **What it reads.** Eight probes, each weighted by its trilinear weight times the squared
     facing weight against the geometric normal (`deferred_lighting_ps.hlsl:912-934`). Invalid
     probes are skipped and the surviving weights renormalised (`deferred_lighting_ps.hlsl:937-968`).
     The L2 SH is evaluated at the shading normal and multiplied by `r.GI.Intensity`.
   - **When it answers.** Every pixel DDGI did not answer, even with zero.
3. **Light probe volume** (`deferred_lighting_ps.hlsl:973-1028`). Inert: `lpvIntensity` is always
   0 (`frame_constants.cpp:967-972`). See "Light Probe Volume" in
   `docs/specs/rendering/ibl-pipeline.md`.
4. **Sky SH** (`deferred_lighting_ps.hlsl:1030-1060`). The global sky SH, evaluated with
   `WorldToCubemap(N)`, floored at `minOcclusion` (`deferred_lighting_ps.hlsl:1066-1068`) and tinted
   by the sky light colour (`deferred_lighting_ps.hlsl:714`). Every pixel uses it when
   `r.RadianceCache` is 0.

After the chain:

- **SDF final gather.** `r.GI.FinalGather 1` needs the radiance cache and the distance field
  (`frame_constants.cpp:826-861`). When it is on, the gathered irradiance replaces the chosen
  source in every pixel (`deferred_lighting_ps.hlsl:1148-1155`), so no pixel reads DDGI or the
  probe interpolation directly. The gather itself reads the radiance-cache probes where its rays
  hit (`runtime/shaders/runtime/ssgi/ssgi_world_gather.hlsli:13`).
- **Diffuse term.** `(1 - metallic) x E x baseColor x (1 - dielectric scatter)`
  (`deferred_lighting_ps.hlsl:1246-1247`), times `ao ^ r.IndirectAO.Exponent` and the sky tint
  (`deferred_lighting_ps.hlsl:1337-1338`). The legacy SSGI (`r.SSGI`) is added only while the gather
  is off (`deferred_lighting_ps.hlsl:1277-1289`). The dormant Brixelizer branch is described in
  Tier 4.
- **SkyLight settings.** Section 2 of `docs/guides/indoor-lighting-guide.md` lists which of
  `intensity`, `indirectLightingIntensity`, the lower hemisphere and `minOcclusion` reaches each
  source. In short, the sky of the GI writers takes `skyLightColor x skyLightIntensity`
  (`runtime/engine/renderer/gi/radiance_cache.cpp:839-841`,
  `runtime/engine/renderer/gi/ddgi_system.cpp:253-255`, `ssgi_world_gather.hlsli:22`), while
  `indirectLightingIntensity` and the lower hemisphere reach only the sky-SH fallback.

### Probe Interpolation

The deferred lighting pixel shader samples the radiance cache at the current pixel's world position via trilinear probe interpolation:

```hlsl
/// @ref Majercik et al. 2019 "DDGI" — trilinear probe interpolation
float3 SampleRadianceCache(
    float3 worldPos,
    float3 normal,
    StructuredBuffer<float4> probeSHBuffer,
    float3 gridOrigin,
    float  gridExtent,
    uint   probesPerAxis)
{
    // Compute fractional grid coordinates
    float probeSpacing = (gridExtent * 2.0f) / float(probesPerAxis);
    float3 localPos = (worldPos - gridOrigin) / probeSpacing;

    // Clamp to grid bounds
    localPos = clamp(localPos - 0.5f, 0.0f, float3(probesPerAxis - 1));

    // Get integer cell and fractional weights
    uint3 cell = uint3(floor(localPos));
    float3 frac = localPos - float3(cell);
    uint3 cellMax = min(cell + 1, uint3(probesPerAxis - 1));

    // Evaluate SH at 8 corner probes and blend
    float3 result = (float3)0;
    for (uint dz = 0; dz < 2; dz++)
    {
        for (uint dy = 0; dy < 2; dy++)
        {
            for (uint dx = 0; dx < 2; dx++)
            {
                float weight = (dx == 0 ? (1 - frac.x) : frac.x)
                             * (dy == 0 ? (1 - frac.y) : frac.y)
                             * (dz == 0 ? (1 - frac.z) : frac.z);

                uint probeIdx = (dz == 0 ? cell.z : cellMax.z) * 256
                              + (dy == 0 ? cell.y : cellMax.y) * 16
                              + (dx == 0 ? cell.x : cellMax.x);

                float3 probeIrrad = EvaluateProbeSH(probeSHBuffer, probeIdx * 7, normal);
                result += probeIrrad * weight;
            }
        }
    }

    return result;
}
```

### SH Evaluation

L2 spherical harmonics are evaluated for a given normal direction to produce per-channel irradiance:

```hlsl
/// @ref Ramamoorthi & Hanrahan 2001 "Irradiance Environment Maps"
float3 EvaluateProbeSH(StructuredBuffer<float4> shBuffer, uint baseIndex, float3 normal)
{
    float4 rSH0 = shBuffer[baseIndex + 0];
    float4 gSH0 = shBuffer[baseIndex + 1];
    float4 bSH0 = shBuffer[baseIndex + 2];
    float4 rSH1 = shBuffer[baseIndex + 3];
    float4 gSH1 = shBuffer[baseIndex + 4];
    float4 bSH1 = shBuffer[baseIndex + 5];
    float4 sh6  = shBuffer[baseIndex + 6];

    // L0 + L1: dot(coeffs, (1, nx, nz, ny))
    float4 nDir = float4(1.0f, normal.x, normal.z, normal.y);
    float3 result;
    result.r = dot(rSH0, nDir);
    result.g = dot(gSH0, nDir);
    result.b = dot(bSH0, nDir);

    // L2: 5 basis functions + xy term
    float3z2_1 = 3.0f * normal.z * normal.z - 1.0f;
    float xz = normal.x * normal.z;
    float x2_y2 = normal.x * normal.x - normal.y * normal.y;
    float yz = normal.y * normal.z;

    result.r += dot(rSH1, float4(z2_1, xz, x2_y2, yz)) + sh6.x * normal.x * normal.y;
    result.g += dot(gSH1, float4(z2_1, xz, x2_y2, yz)) + sh6.y * normal.x * normal.y;
    result.b += dot(bSH1, float4(z2_1, xz, x2_y2, yz)) + sh6.z * normal.x * normal.y;

    return max(result, 0.0f);
}
```

### Deferred Lighting Application

In the main deferred lighting pass:

```hlsl
// deferred_lighting_ps.hlsl (pseudo-code)
float3 albedo = SampleGBuffer(texAlbedo, uv);
float3 normal = SampleGBuffer(texNormal, uv);
float3 worldPos = ReconstructWorldPosFromDepth(texDepth, uv);

// Sample radiance cache
float3 giIrradiance = SampleRadianceCache(worldPos, normal,
                                          probeSHBuffer, gridOrigin, gridExtent, 16);

// Apply: diffuse = irradiance × baseColor × π (Lambertian BRDF)
float3 diffuseGI = giIrradiance * albedo * (1.0f / 3.14159f);

// Clamp at grid boundary (fade out over last half probe spacing)
float distToBoundary = DistanceToGridBoundary(worldPos, gridOrigin, gridExtent);
float fadeFactor = saturate(distToBoundary / (probeSpacing * 0.5f));
diffuseGI *= fadeFactor;

// Combine with other lighting (direct light, ambient, etc.)
float3 finalColor = directLighting + ambientLighting + diffuseGI;
```

---

## GPU Resources Summary

### Textures (3D)

| Resource | Format | Dimensions | Memory | Purpose |
|----------|--------|-----------|--------|---------|
| GDF Clipmap 0 | R16F | 64×64×64 | 512 KB | Cascade 0 (25 m) |
| GDF Clipmap 1 | R16F | 64×64×64 | 512 KB | Cascade 1 (100 m) |
| GDF Clipmap 2 | R16F | 64×64×64 | 512 KB | Cascade 2 (400 m) |
| GDF Clipmap 3 | R16F | 64×64×64 | 512 KB | Cascade 3 (1600 m) |
| **Per-Mesh SDFs** | R16F | 32×32×32 | 64 KB each | Mesh surfaces for GDF composite |

**Total dedicated GI memory:** ~2.5 MB (4 clipmaps + typical 10-15 mesh SDFs)

### Buffers

| Resource | Type | Size | Usage |
|----------|------|------|-------|
| Probe SH Buffer | StructuredBuffer<float4> | 4096 × 7 × 16 = 458 KB | Radiance cache probe coefficients |
| Mesh SDF Array (cbuffer) | cbuffer | 256 × 96 = 24 KB | Per-mesh SDF metadata for composite |
| Per-mesh Tri Buffer (transient) | StructuredBuffer<GPUTriangle> | 64 bytes × triangle count | Temporary during SDF bake (destroyed after dispatch) |

### Descriptors (Bindless)

| Descriptor | Type | Binding | Purpose |
|-----------|------|---------|---------|
| GDF Clipmap SRVs | Texture3D SRV | table[global_sdf_slot...] | Sampling GDF in probe trace shader |
| GDF Clipmap UAVs | Texture3D UAV | table[global_sdf_composite_slot...] | Writing composite results |
| Mesh SDF SRVs | Texture3D SRV | per-mesh (MeshSDF::srvIndex) | Sampling mesh SDFs in composite |
| Probe SH Buffer SRV | StructuredBuffer SRV | global shader table | Reading SH coefficients in deferred lighting |
| Probe SH Buffer UAV | StructuredBuffer UAV | global shader table | Writing SH coefficients in trace shader |

---

## Constant Buffer Layouts

### GDF Composite Root Constants (32 bytes)

```cpp
cbuffer RootConstants : register(b0)
{
    uint32_t outputUav;        // [0] UAV index for target clipmap
    uint32_t meshCount;        // [1] Number of mesh SDFs to composite
    uint32_t clipmapIndex;     // [2] Cascade index (0-3)
    uint32_t resolution;       // [3] 64
    float    clipmapCenterX;   // [4..6] World-space clipmap center (cm)
    float    clipmapCenterY;
    float    clipmapCenterZ;
    float    clipmapExtent;    // [7] Half-extent (cm)
};
// Total: 32 bytes (8 × uint32)
```

### Radiance Cache Trace Root Constants (176 bytes)

Bound at b2 (cbuffer). See [Root Constants for Trace](#root-constants-for-trace) section above.

---

## Performance Characteristics

### Step Counts (Per Frame, 1080p)

| Stage | Threads | Complexity | Est. Time |
|-------|---------|-----------|-----------|
| **Mesh SDF Bake** (1×32³ mesh) | 32,768 | O(V×T) where T=triangles | ~20 ms |
| **GDF Composite** (cascade 0) | 262,144 | O(V×M) where M=mesh SDFs | ~3 ms |
| **GDF Composite** (all 4 cascades) | 1M+ | — | ~15 ms (1-2 cascades/frame) |
| **Radiance Cache Trace** (4096 probes) | 131,072 rays | O(P×R×S) P=probes, R=rays, S=sphere march steps | ~8 ms |
| **Deferred Lighting** (with GI sampling) | 2M pixels | Trilinear probe interpolation per pixel | +2-3 ms vs non-GI |

**Total GI overhead: ~25-30 ms per frame** (cascaded composites amortized, probe trace incremental).

### Ray Marching Budget

Each probe ray marches through cascaded GDF with up to 128 steps per cascade (512 steps per probe in worst case). Typical case: 64-100 steps total due to early termination.

```
Rays per frame: 4096 probes × 32 rays/probe = 131,072 rays
Steps per ray (avg): ~80 sphere march steps
Total SDF samples per frame: ~10.5M samples
GPU BW: 10.5M × 4 bytes × 0.4 (cache hit rate) = ~17 GB/s read
```

---

## Data Flow: Per-Frame Update

```
Frame N:
  1. Update camera position → PerFrameCB
  2. GlobalDistanceField::Update(cameraPos)
     ├─ Snap clipmap centers to voxel grids
     └─ Mark dirty cascades
  3. For each dirty GDF cascade:
     ├─ Dispatch global_sdf_composite_cs.hlsl
     ├─ Iterate all mesh SDFs
     ├─ Sample mesh SDF in local space
     └─ Write global minimum to clipmap texture
  4. RadianceCache::UpdateProbes(cmdList, gdf, ...)   (as built, 2026-10-08)
     ├─ Dispatch radiance_cache_trace_cs.hlsl (64 threads per group,
     │  1024 of the 4096 probes per frame)
     ├─ Per-probe: 32 Hammersley rays over the sphere, jittered per frame
     ├─ Sphere-march each ray through GDF cascades 0→3
     ├─ Accumulate radiance into SH coefficients
     ├─ EMA blend: SH_new = 0.88×SH_old + 0.12×SH_trace
     └─ (DDGI writer in the same pass when r.RayTracing + r.RayTracing.GI)
  5. Deferred lighting pass
     ├─ Per-pixel: reconstruct world position from depth
     ├─ One irradiance source by priority: DDGI > radiance cache >
     │  light probe volume (inert) > sky SH; r.GI.FinalGather replaces it
     ├─ Apply: diffuse = (1-metallic) × E × baseColor × (1 - scatter)
     └─ Output combined lighting
```

---

## Future Work and Extensions

### Phase C (Current): Core 3-Tier System
- [x] Mesh SDF baking (GPU compute)
- [x] Global distance field (cascaded clipmaps)
- [x] Radiance cache (world-space probes + L2 SH)
- [x] Deferred lighting integration

### Phase D: Visibility and Quality Enhancements
- [ ] **Probe visibility occlusion** — Chebyshev variance shadow maps per probe for backface rejection
- [ ] **Surface expansion and thickness** — Parameterized SDF distance bias to prevent light leaks
- [ ] **Screen-space probe gather** — Lumen-style per-pixel screen probes for high-frequency detail
- [ ] **Coverage atlas** — Coverage information per voxel to handle thin geometry

### Phase E: Disk Persistence and Optimization
- [ ] **SDF disk caching** — Save baked mesh SDFs to `.uzsdf` format; reload on level load
- [x] **Partial clipmap scrolling** — done 2026-10-05: recentres recompose the entering slabs (toroidal storage, `r.GI.GDF.Incremental`) and instance changes recompose their neighbourhood (per-entity diff, band `r.GI.GDF.BandVoxels`) with region-scoped history resets in the radiance cache and the SSGI accumulation; see `docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-02-gdf-incremental.md`
- [ ] **Probe LOD** — Lower ray count for distant probes; higher rate for near-camera probes
- [ ] **Cone tracing for soft shadows** — 3D cone march for contact shadow refinement

### Phase F (Long-term): Advanced Techniques
- [ ] **Hierarchical SH probes** — Multiple SH grids at different resolutions (octree)
- [ ] **Irradiance caching** — Cache irradiance queries from expensive lighting evaluation
- [ ] **Probe relocation** — Move probes away from geometry to avoid dark corners
- [ ] **GPU-driven rendering** — Bindless mesh instancing + GI lookup in deferred pass
- [ ] **Temporal reuse** — Reproject probe history from previous frames

---

## References

### Published Works
- **@ref Wright 2015** — "Dynamic Occlusion with Signed Distance Fields" (SIGGRAPH 2015). UE4 Lumen GI foundation.
- **@ref Hart 1996** — "Sphere Tracing" (SIGGRAPH 1996). Foundational SDF ray marching algorithm.
- **@ref Ericson 2005** — "Real-Time Collision Detection" (2005), Chapter 5.1.5. Closest point on triangle via Voronoi regions.
- **@ref Keinert et al. 2015** — "Spherical Fibonacci Point Sets" (2015). Uniform sphere sampling for quasi-random ray distribution.
- **@ref Majercik et al. 2019** — "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields" (SIGGRAPH 2019). Reference DDGI probe architecture.
- **@ref Ramamoorthi & Hanrahan 2001** — "An Efficient Representation for Irradiance Environment Maps" (SIGGRAPH 2001). Spherical harmonics for irradiance.

### UzEngine Source
- `runtime/engine/renderer/gi/mesh_sdf_baker.h` — CPU/GPU mesh SDF interface
- `runtime/engine/renderer/gi/global_distance_field.h` — Cascaded clipmap manager
- `runtime/engine/renderer/gi/radiance_cache.h` — World-space probe grid
- `runtime/shaders/runtime/gi/mesh_sdf_bake_cs.hlsl` — Per-voxel GPU SDF computation
- `runtime/shaders/runtime/gi/global_sdf_composite_cs.hlsl` — Clipmap compositing
- `runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl` — Probe ray tracing
- `runtime/shaders/runtime/include/sdf_common.hlsli` — SDF sampling and sphere marching utilities
- `runtime/shaders/runtime/include/radiance_cache_common.hlsli` — Probe lookup and SH evaluation

---

## Implementation Notes

### Thread Safety
- **Mesh SDF baking** is thread-safe; can be called from worker threads.
- **GlobalDistanceField** and **RadianceCache** are NOT thread-safe; must be called from render thread.

### Memory Overhead Estimate (Typical Scene)
- **4 GDF clipmaps:** 2 MB
- **Probe SH buffer:** 449 KB
- **15 mesh SDFs:** 960 KB
- **Descriptors:** ~100 KB
- **Constant buffers:** ~200 KB
- **Total:** ~3.7 MB GPU VRAM (negligible for modern GPUs)

### Temporal Stability
EMA blending provides temporal coherence while remaining responsive. The design value was α = 0.05 (a 20-frame window); as built it is 0.12 with each probe retraced every fourth frame, about 33 frames per e-fold, and a freshly loaded scene converges over about 200 frames (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-02-gdf-incremental.md:112-117`, `:152-154`).

### Cascading Strategy
Cascades are updated on a round-robin schedule:
- **Every frame:** Cascade 0 (fine detail, high temporal requirement)
- **Every 4 frames:** Cascade 1
- **Every 16 frames:** Cascade 2
- **Every 64 frames:** Cascade 3

This keeps per-frame composite cost bounded while ensuring GI updates complete in reasonable time scales.

---

## Tier 4: AMD FidelityFX Brixelizer GI (dormant)

> **Status (2026-10-08): dormant. Not compiled, never constructed, never dispatched.** This section
> records the integration as it stands in source; none of it runs. `phase0_brixelizer-gi-integration`
> was closed as superseded on 2026-10-08; its `tasks.md` lists which of the original ticks hold and
> the defects found in the dormant code. Removing that code is a separate decision.
>
> - **Not compiled.** `runtime/build.zig:903-911` defines `UZ_BRIXELIZER_ENABLED` (`:929-931`) only
>   when the MSVC-built wrapper `runtime/thirdparty/amd-fidelityfx-1.1/wrapper/build/bin/uz_brixelizer.dll`
>   exists. Nothing in the repo builds it (`scripts/setup.ps1:543-549` only clones SDK v1.1.4), so
>   `brixelizer_gi_pass.cpp` compiles its no-op stub (`:1166-1202`) and none of the CVars below is
>   registered.
> - **Never constructed.** The construction is commented out at
>   `runtime/engine/renderer/scene/scene_renderer_init.cpp:666-674` since `c84a8bce` (2026-04-10),
>   because `ffxBrixelizerContextCreate` failed with `0x8000000D` (`FFX_ERROR_BACKEND_API_ERROR`) on
>   non-AMD GPUs. The cause was never diagnosed.
> - **Never dispatched.** `SceneRenderer::RegisterBrixelizerGIPasses` returns at its first statement
>   (`runtime/engine/renderer/frame/frame_gi_passes.cpp:387`) since `29c83ba5` (2026-04-07).
> - **Composite never taken.** `frame_constants.cpp:868-872` publishes `UINT32_MAX` as
>   `brixelizerGIDiffuseSrv` unless a pass is initialised, so the branch at
>   `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:1306-1324` never runs.
> - **What runs instead:** Tiers 1-3 with the SDF final gather and SDF reflections, and DDGI as the
>   DXR option (see the Overview).

### Design intent

AMD FidelityFX Brixelizer GI builds a sparse, cascaded SDF of the scene and traces screen-space probes
through it, producing diffuse and specular indirect light with its own denoiser, on compute shaders and
without DXR. None of this was validated in UzEngine: the only timing on record is the unverified
"~26 ms/frame on RTX 4090" in the task's item 4.6, written by the commit that disabled the dispatch
because it "was crashing render"; the task's other run claims (items 2.6, 3.5 and 5.6) left no
log, capture or screenshot in the repo.

- **SDK:** AMD FidelityFX SDK 1.1.4 (MIT license), vendored at `runtime/thirdparty/amd-fidelityfx-1.1/`
- **Location:** `runtime/engine/renderer/passes/gi/brixelizer_gi_pass.h/.cpp` and `brixelizer_loader.h/.cpp`
- **Gates:** `UZ_BRIXELIZER_ENABLED` at build time. At run time `r.BrixelizerGI` (default: false) gates
  only `BrixelizerGIPass::Execute` (`brixelizer_gi_pass.cpp:904`); Init, geometry registration and the
  published SRV ignore it.

### Architecture (as written, never run)

```text
Geometry Registration (DrawList → FfxBrixelizerInstanceDescription)
         │
         ▼
Brixelizer SDF Bake (ffxBrixelizerBakeUpdate + ffxBrixelizerUpdate)
  ├─ 2-8 cascades, voxel sizes 2/4/8/16/32/64/128/256 cm
  ├─ Static + dynamic geometry per cascade
  └─ GPU compute: 25+ HLSL shaders (SDF build, eikonal, compress)
         │
         ▼
BrixelizerGI Dispatch (ffxBrixelizerGIContextDispatch)
  ├─ Screen-space probe spawning + projection
  ├─ SDF ray march through Brixelizer cascades
  ├─ SH propagation + irradiance cache emission
  ├─ Specular trace + pre-trace
  ├─ Temporal reprojection + denoising (blur X/Y)
  └─ Output: diffuseGI (RGBA16F) + specularGI (RGBA16F)
         │
         ▼
Deferred Lighting Composite (deferred_lighting_ps.hlsl:1306-1324, never taken)
  ├─ if the SRV is valid and luma(brixDiffuse) > 0.001:
  │    diffuseAmbient = brixDiffuse × baseColor × (1 - metallic)   (replace, not add)
  └─ no specular read (removed in 29c83ba5)
```

### CVars

Declared only when `UZ_BRIXELIZER_ENABLED` is defined (`brixelizer_gi_pass.cpp:50-106`), so no current build registers them. Also declared there: `r.BrixelizerGI.TriangleSwapSize` and the diagnostic gates `r.BrixelizerGI.Dispatch`, `.AllocSdf`, `.RealInit`, `.GiDispatch` and `.BakeUpdate` (`:89-106`).

| CVar | Type | Default | Description |
|------|------|---------|-------------|
| `r.BrixelizerGI` | bool | false | Master toggle |
| `r.BrixelizerGI.Cascades` | int | 4 | Number of SDF cascades (2-8) |
| `r.BrixelizerGI.Intensity` | float | 1.0 | GI intensity multiplier. **No reader**: nothing applies it |
| `r.BrixelizerGI.VoxelSize` | float | 2.0 | Finest cascade voxel size (cm) |
| `r.BrixelizerGI.RayPushoff` | float | 0.5 | Ray origin offset along normal (cm) |
| `r.BrixelizerGI.SdfSolveEps` | float | 0.5 | SDF ray march epsilon (cm) |
| `r.BrixelizerGI.MaxReferences` | int | 32768 | Max triangle refs per update |
| `r.BrixelizerGI.MaxBricksPerBake` | int | 16384 | Max bricks per SDF bake |
| `r.BrixelizerGI.Debug` | int | 0 | Debug mode (0=off, 1=radiance, 2=irradiance). **No reader**: nothing applies it |

### Relationship to Tiers 1-3

Designed as an alternative to the radiance cache (Tier 3), never validated as one. If the dormant
composite ran, it would not add to Tiers 1-3: when the Brixelizer sample's luma exceeds 0.001 it
**replaces** `diffuseAmbient` (`deferred_lighting_ps.hlsl:1314-1321`), discarding the world final
gather carried in it (`:1148-1150`, `:1246-1247`), any legacy SSGI added at `:1277-1295` and the
`(1 - totalScatterDielectric)` energy factor. It has no intensity factor (`r.BrixelizerGI.Intensity`
is never read) and no specular term: the `diffuseSrv + 1` specular read was removed in `29c83ba5`,
and the "+1" SRV convention does not hold, because SRVs and UAVs come from one allocator
(`dx12_texture_creation.cpp:195`, `:232`). The figures this section used to give ("~0.5 ms" for the
radiance cache, "~2-4 ms" for Brixelizer) were estimates. The measured radiance-cache trace is
0.72 ms at 1080p on Sponza (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-01-pass-timings.md:76`), and no
measured Brixelizer timing exists.

### Build gate and what a revival would need

The Zig build never links the FFX libraries. It adds the SDK include paths when
`sdk/include/FidelityFX/host/ffx_brixelizer.h` exists (`runtime/build.zig:879-888`, `:913-922`) and
defines `UZ_BRIXELIZER_ENABLED` only when the wrapper DLL exists (`:903-911`, `:929-931`). At run
time `brixelizer_loader.cpp:93-126` loads `uz_brixelizer.dll` from next to the executable (falling
back to the default DLL search) and resolves its 18 `uz_brix_*` exports. The gate this section used
to give (`ffx_backend_dx12_x64.lib`) was wrong when written: commit `490e9be1`, which wrote it, also
removed the `.lib`-gated define from `build.zig` (its message says the define "stays 0 in Zig build").

Reviving the integration would be a new task:

1. Build the SDK 1.1.4 static libraries with MSVC/CMake (in `runtime/thirdparty/amd-fidelityfx-1.1/sdk`:
   `cmake -B build -G "Visual Studio 17 2022" -A x64 -DFFX_API_BACKEND=DX12_X64 -DFFX_ALL=ON`, then
   `cmake --build build --config Release`). They land in `sdk/bin/ffx_sdk/` (`sdk/CMakeLists.txt:122-124`),
   where the wrapper expects them (`wrapper/CMakeLists.txt:34-43`, `:58-60`).
2. Build the wrapper (in `wrapper/`: `cmake -B build -G "Visual Studio 17 2022" -A x64`, then
   `cmake --build build --config Release`). It writes `wrapper/build/bin/uz_brixelizer.dll`
   (`wrapper/CMakeLists.txt:82-85`); copy that next to `uzengine.exe`, since no build step does.
3. Restore the construction (`scene_renderer_init.cpp:666-674`) and the pass registration
   (`frame_gi_passes.cpp:387`), then re-run `zig build`; with the DLL in place it defines
   `UZ_BRIXELIZER_ENABLED`.
4. Diagnose the `FFX_ERROR_BACKEND_API_ERROR` from `ffxBrixelizerContextCreate` with the D3D12 debug
   layer. Then fix the wiring defects recorded in the task's `tasks.md`, restore a specular composite
   with its own SRV, size the cascades for the scene (the defaults cover at most 10.24 m per axis), and
   gate the result with region dumps and per-pass `gpu_ms` against the current GI.

---

**Document Version:** 1.2
**Last Updated:** 2026-10-08
**Status:** Tiers 1-3 implemented (current pipeline: `docs/engine/12-rendering/33-sdf-gi-final-gather.md`); Tier 4 (Brixelizer GI) dormant, its task closed as superseded on 2026-10-08. Overview, Tier 3 constants and the deferred-lighting priority chain corrected to the code on 2026-10-08 (`phase0_indoor-lighting-system`); the Tier 1-2 design figures (32³ mesh SDFs, 64³ clipmaps, memory and cost estimates) were not re-checked
