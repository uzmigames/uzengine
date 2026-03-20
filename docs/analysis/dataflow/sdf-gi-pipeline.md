<!-- doc-meta
title: Software Ray-Traced GI Pipeline — UE5 Lumen Data Flow
task: phase0-3.0.1
created: 2026-03-19
status: analysis
-->

# Software Ray-Traced GI Pipeline — UE5 Lumen Data Flow

Complete data flow analysis for implementing Lumen-style software ray-traced
global illumination in UzEngine. Every algorithm detail cites UE5 5.6 source
at `W:\UE_5.6_Source`.

---

## 1. UE5 Lumen Pipeline Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        UE5 LUMEN GI PIPELINE                            │
│                                                                         │
│  ┌──────────────┐    ┌──────────────────┐    ┌───────────────────────┐  │
│  │  Per-Mesh     │    │  Global Distance  │    │  Lumen Surface Cache │  │
│  │  SDF Bake     │───▶│  Field (GDF)     │    │  (card captures)     │  │
│  │  (offline)    │    │  (clipmaps)      │    │                       │  │
│  └──────────────┘    └────────┬─────────┘    └──────────┬────────────┘  │
│                               │                          │               │
│                               ▼                          ▼               │
│                    ┌──────────────────────────────────────────┐          │
│                    │  SDF Sphere March Ray Tracing            │          │
│                    │  (per Mesh SDF → then per Global SDF)    │          │
│                    └────────────────┬─────────────────────────┘          │
│                                     │                                    │
│                    ┌────────────────┼────────────────┐                   │
│                    ▼                ▼                ▼                   │
│             ┌────────────┐  ┌────────────┐  ┌────────────────┐          │
│             │ Screen      │  │ Radiance   │  │ Lumen          │          │
│             │ Probes      │  │ Cache      │  │ Reflections    │          │
│             │ (per-pixel  │  │ (world     │  │ (specular      │          │
│             │  GI gather) │  │  probes)   │  │  reflections)  │          │
│             └──────┬──────┘  └─────┬──────┘  └───────┬────────┘          │
│                    │               │                  │                   │
│                    ▼               ▼                  ▼                   │
│             ┌──────────────────────────────────────────────┐             │
│             │  DiffuseIndirect Composite Pass              │             │
│             │  (combines GI + AO into scene color)         │             │
│             └──────────────────────────────────────────────┘             │
└─────────────────────────────────────────────────────────────────────────┘
```

**Method selection CVar:**
`r.DynamicGlobalIlluminationMethod` = 0 (None), 1 (Lumen), 2 (SSGI), 3 (Plugin)
@source UE5 IndirectLightRendering.cpp:39-44

---

## 2. Mesh SDF Generation

### 2.1 Data Structure — `FDistanceFieldVolumeData`

@source UE5 DistanceFieldAtlas.h:240-303

Each static mesh has a baked SDF stored as a **sparse brick volume**:

| Field | Type | Description |
|-------|------|-------------|
| `Mips[3]` | `FSparseDistanceFieldMip` | 3 mip levels of the sparse SDF |
| `AlwaysLoadedMip` | `TArray<uint8>` | Lowest mip always in memory |
| `StreamableMips` | `FByteBulkData` | Higher mips streamed on demand |
| `LocalSpaceMeshBounds` | `FBox3f` | Object-space AABB |
| `bMostlyTwoSided` | `bool` | Affects surface expansion |

Each mip (`FSparseDistanceFieldMip`) contains:
@source UE5 DistanceFieldAtlas.h:198-237

| Field | Type | Description |
|-------|------|-------------|
| `IndirectionDimensions` | `FInt32Vector` | Size of indirection table |
| `NumDistanceFieldBricks` | `int32` | Number of non-empty bricks |
| `VolumeToVirtualUVScale` | `FVector3f` | Volume → UV transform |
| `VolumeToVirtualUVAdd` | `FVector3f` | Volume → UV transform |
| `DistanceFieldToVolumeScaleBias` | `FVector2f` | Decode SDF distance |

### 2.2 Constants

@source UE5 DistanceFieldAtlas.h:36-50

```cpp
constexpr int32 MeshDistanceFieldObjectBorder = 1;    // 1 voxel border for gradient
constexpr int32 UniqueDataBrickSize = 7;               // Unique data region per brick
constexpr int32 BrickSize = 8;                         // Full brick (with trilinear border)
constexpr int32 BandSizeInVoxels = 4;                  // Narrow band width
constexpr int32 NumMips = 3;                           // Mip chain depth
constexpr uint32 InvalidBrickIndex = 0xFFFFFFFF;
constexpr EPixelFormat DistanceFieldFormat = PF_G8;    // 8-bit unsigned per voxel
constexpr uint32 MaxIndirectionDimension = 1024;       // Max indirection table size
```

### 2.3 Per-Mesh Resolution

@source UE5 DistanceFieldAtlas.cpp:67-79

```
r.DistanceFields.MaxPerMeshResolution = 256  (max voxels per axis per mesh)
r.DistanceFields.DefaultVoxelDensity  = 0.2  (how mesh scale → voxel dims)
```

### 2.4 SDF Sampling (Shader Side)

@source UE5 LumenSoftwareRayTracing.ush:124-259 — `RayTraceSingleMeshSDF`

The per-mesh SDF trace performs sphere marching in **volume space**:

1. Transform ray to volume space via `WorldToVolume` matrix
2. Clip ray against volume AABB (`LineBoxIntersect`)
3. March up to **64 steps** with `MinStepSize = 1/(16*MaxSteps)`
4. At each step, sample `SampleSparseMeshSignedDistanceField()`
5. Surface expansion: `ExpandSurfaceDistance * saturate(t / (2*ExpandSurfaceDistance)) * ExpandSurfaceScale`
6. Hit when `DistanceField < ExpandSurfaceAmount`
7. On hit, convert distance back to world space: `length(VolumeRayDir * t * VolumeToWorldScale)`

---

## 3. Global Distance Field (GDF)

### 3.1 Clipmap Structure

@source UE5 GlobalDistanceField.cpp:23-46, 120-126, 383-403

```
r.AOGlobalDistanceField.NumClipmaps = 4          (base, Lumen adds 1-2 more)
r.AOGlobalDFResolution = 128                      (voxels per clipmap axis)
r.AOGlobalDFClipmapDistanceExponent = 2           (cascade size growth factor)
r.AOGlobalDFStartDistance = 100                   (cm, switch from mesh→global SDF)
```

Lumen uses **4-6 clipmaps** (up to `MAX_GLOBAL_DF_CLIPMAPS = 6`).
@source UE5 GlobalDistanceFieldShared.ush:19 — `#define MAX_GLOBAL_DF_CLIPMAPS 6`

Clipmap extent formula:
@source UE5 GlobalDistanceField.cpp:446-457

```
InnerClipmapDistance = ViewDistance / pow(Exponent, 3)
Extent[i] = InnerClipmapDistance * pow(Exponent, i)
```

### 3.2 Page-Based Sparse Storage

@source UE5 GlobalDistanceField.cpp:406-418
@source UE5 GlobalDistanceFieldShared.ush:88-101

```
PageResolutionInAtlas = 8    (includes 0.5 texel trilinear margin)
PageResolution = 7           (actual data resolution per page)
InfluenceRangeInVoxels = 4   (narrow band)
PageAtlasSizeInPagesX = 128
PageAtlasSizeInPagesY = 128
PageAtlasSizeInPagesZ = dynamic (based on occupancy ratio 0.3)
InvalidPageId = 0xFFFFFFFF
CoveragePageResolutionInAtlas = 4
CoverageDownsampleFactor = 2 (= 8/4)
```

GPU textures:
| Texture | Format | Purpose |
|---------|--------|---------|
| `PageAtlasTexture` | `R8_UNORM` 3D | Encoded SDF distances |
| `CoverageAtlasTexture` | `R8_UNORM` 3D | Surface coverage (two-sided detection) |
| `PageTableTexture` | `R32_UINT` 3D | Page index lookup (per clipmap) |
| `MipTexture` | `R8_UNORM` 3D | Low-res mip for early-out |

### 3.3 Shader Parameters

@source UE5 GlobalDistanceField.cpp:312-349 — `SetupGlobalDistanceFieldParameters`

Per-clipmap data:
- `GlobalVolumeTranslatedCenterAndExtent[6]` — center.xyz + extent.w
- `GlobalVolumeTranslatedWorldToUVAddAndMul[6]` — UV transform (add.xyz + mul.w)
- `GlobalDistanceFieldMipTranslatedWorldToUVScale[6]` — mip UV scale
- `GlobalDistanceFieldMipTranslatedWorldToUVBias[6]` — mip UV bias

Scalars:
- `GlobalVolumeDimension` = resolution (128)
- `GlobalVolumeTexelSize` = 1/128
- `GlobalDistanceFieldClipmapSizeInPages` = resolution / 7
- `NumGlobalSDFClipmaps` = 4-6
- `CoveredExpandSurfaceScale` = 1.0 (default)
- `NotCoveredExpandSurfaceScale` = 0.6
- `NotCoveredMinStepScale` = 4.0

### 3.4 Update Strategy

@source UE5 GlobalDistanceField.cpp:56-95

- **Partial updates**: Only re-composite pages where objects changed
- **Staggered updates**: Larger clipmaps updated less frequently
- **2 clipmaps per frame** (default), reduced to 1 in fast-camera mode
- **Static/Movable separation**: Static objects cached separately (+12MB)
- **Streaming**: Re-cache clipmaps when mesh SDFs finish streaming

### 3.5 Distance Sampling

@source UE5 GlobalDistanceFieldShared.ush:214-247 — `GetDistanceToNearestSurfaceGlobal`

```hlsl
for each clipmap (inner→outer):
    if (point inside clipmap margin):
        VolumeUV = ComputeGlobalUV(position, clipmapIndex)
        Page = PageTableTexture.Load(VolumeUV * ClipmapSizeInPages)
        if (Page.bValid):
            PageUV = PageAtlasOffset + frac(VolumeUV * ClipmapSizeInPages) * 7 + 0.5
            distance = DecodePageDistance(PageAtlas.Sample(PageUV), influenceRange)
            return distance
```

Distance encoding:
@source UE5 GlobalDistanceFieldShared.ush:183-191

```hlsl
Encode: saturate(distance / (2 * influenceRange) + 0.5)
Decode: (encoded * 2 - 1) * influenceRange
```

---

## 4. SDF Ray Tracing — Sphere March Algorithm

### 4.1 Global SDF Trace

@source UE5 GlobalDistanceFieldUtils.ush:90-200 — `RayTraceGlobalDistanceField`

```hlsl
FGlobalSDFTraceResult RayTraceGlobalDistanceField(FGlobalSDFTraceInput Input)
{
    // For each clipmap (inner → outer):
    for (clipmap = minClipmap; clipmap < NumClipmaps; clipmap++)
    {
        VoxelExtent = Extent[clipmap] * TexelSize;
        InfluenceRange = 4 * 2 * VoxelExtent;

        // Clip ray to clipmap box
        IntersectionTimes = LineBoxIntersect(rayStart, rayEnd, center ± extent);

        // Sphere march up to 256 steps
        for (step = 0; step < 256; step++)
        {
            samplePos = rayStart + dir * t;

            // Sample mip first for early-out
            mipValue = MipTexture.Sample(ComputeMipUV(samplePos));
            distance = DecodeMip(mipValue, MipFactor * influenceRange);
            coverage = 1.0;

            // If mip says close, read high-res page
            Page = PageTable.Load(ComputeGlobalUV(samplePos));
            if (Page.bValid && mipValue < MipTransition)
            {
                pageUV = ComputePageUV(volumeUV, page);
                distance = Decode(PageAtlas.Sample(pageUV), influenceRange);

                if (Page.bCoverage)
                    coverage = CoverageAtlas.Sample(coverageUV);
            }

            // Surface expansion (anti-leak for thin surfaces)
            expandScale = lerp(0.6, 1.0, coverage);     // NotCovered vs Covered
            expandAmount = VoxelExtent * saturate(t / (2 * VoxelExtent)) * expandScale;

            if (distance < expandAmount)
            {
                HitTime = max(t + distance - expandAmount, 0);
                break;
            }

            // Step forward
            minStep = MinStepFactor * VoxelExtent * lerp(4.0, 1.0, coverage);
            t += max(distance * StepFactor, minStep);
        }
    }
}
```

Key parameters:
- `StepFactor` = 1.0 (conservative, can increase for performance)
- `MinStepFactor` = 1.0
- `MaxSteps` = 256 (global SDF) or 64 (mesh SDF)
- `ExpandSurfaceFalloff` = 2.0 * VoxelExtent
- `NotCoveredMinStepScale` = 4.0 (faster march in two-sided regions)

### 4.2 Combined Mesh SDF + Global SDF Trace

@source UE5 LumenSoftwareRayTracing.ush:27-107 — `FConeTraceInput`

Lumen traces are **two-phase**:
1. **Near field**: Trace against individual Mesh SDFs (higher detail)
   - `VoxelTraceStartDistance` controls handoff distance
   - Uses `RayTraceSingleMeshSDF` per culled object (max 64 steps each)
2. **Far field**: Switch to Global SDF (lower detail, larger range)
   - Uses `RayTraceGlobalDistanceField` (max 256 steps)

---

## 5. Radiance Cache

### 5.1 Probe Layout — Clipmap-Based World Probes

@source UE5 LumenRadianceCacheInterpolation.ush:1-95

The Radiance Cache uses **up to 6 clipmaps** of world-space probes:

```
#define RADIANCE_PROBE_MAX_CLIPMAPS 6
#define INVALID_PROBE_INDEX 0xFFFFFFFF
```

Per-clipmap data:
- `RadianceProbeSettings[6]` — .x = TMin (minimum trace distance)
- `ClipmapCornerTWSAndCellSize[6]` — .xyz = corner position, .w = cell size
- `RadianceProbeClipmapResolution` — probes per axis per clipmap

Probe coordinate computation:
@source UE5 LumenRadianceCacheInterpolation.ush:114-128

```hlsl
float3 GetRadianceProbeCoordFloat(float3 WorldPos, uint ClipmapIndex)
{
    float3 CornerTWS = ClipmapCornerTWSAndCellSize[ClipmapIndex].xyz;
    float CellSize   = ClipmapCornerTWSAndCellSize[ClipmapIndex].w;
    return (WorldPos + PreViewTranslation - CornerTWS) / CellSize;
}
```

Clipmap selection with dithered edge fade:
@source UE5 LumenRadianceCacheInterpolation.ush:154-172

```hlsl
uint GetRadianceProbeClipmap(float3 WorldPos, float DitherRandom)
{
    for (clipmap = 0; clipmap < NumClipmaps; clipmap++)
    {
        probeCoord = GetRadianceProbeCoordFloat(WorldPos, clipmap);
        edgeFade = min(bottom edges, top edges) * InvClipmapFadeSize;
        if (edgeFade > DitherRandom) return clipmap;
    }
    return NumClipmaps; // invalid
}
```

### 5.2 Probe Storage — Octahedral Atlas

@source UE5 LumenRadianceCacheInterpolation.ush:55-93

Probes store radiance in an **octahedral atlas texture**:

| Texture | Content |
|---------|---------|
| `RadianceCacheFinalRadianceAtlas` | `float3` radiance per probe texel |
| `RadianceCacheFinalIrradianceAtlas` | `float3` irradiance (optional) |
| `RadianceCacheFinalSkyVisibilityAtlas` | `float` sky visibility |
| `RadianceCacheDepthAtlas` | `float` hit distance per texel |
| `RadianceCacheProbeOcclusionAtlas` | `float2` occlusion data |
| `RadianceProbeIndirectionTexture` | `uint` 3D texture: coord → probe index |

Key resolution parameters:
- `RadianceProbeResolution` — octahedral tracing resolution (e.g., 16 or 32)
- `FinalProbeResolution` — sampling resolution (with border for mips)
- `IrradianceProbeResolution` = 6 (with 2-texel border = 8)

### 5.3 Probe Update — SDF Tracing from Probes

@source UE5 LumenRadianceCache.usf:1-17 — includes `LumenSoftwareRayTracing.ush`
@source UE5 LumenRadianceCacheTracingCommon.ush:1-72

Each frame, a **budget** of probes is traced:

```hlsl
#define PROBE_TRACE_COST_DOWNSAMPLED  1
#define PROBE_TRACE_COST_NORMAL       4
#define PROBE_TRACE_COST_SUPERSAMPLED 16
```

@source UE5 LumenRadianceCacheUpdate.ush:14-38

Trace cost varies by distance from camera:
- `DistanceFromCameraSq >= DownsampleDistanceSq` → cost 1 (downsampled)
- `DistanceFromCameraSq < SupersampleDistanceSq` → cost 16 (supersampled)
- Otherwise → cost 4 (normal)

Probe world offset (anti-leak):
@source UE5 LumenRadianceCache.usf:132-200 — `ComputeProbeWorldOffsetsCS`

When a probe center is inside geometry (SDF distance < threshold), a 4x4x4
grid of candidate offsets is evaluated. The offset with the maximum SDF
distance is chosen, moving the probe away from surfaces.

Trace tile grouping:
@source UE5 LumenRadianceCacheTracingCommon.ush:4-5

```
#define RADIANCE_CACHE_TRACE_TILE_SIZE_2D 8
#define TRACE_TILE_GROUP_STRIDE 128
```

### 5.4 Probe Interpolation

@source UE5 LumenRadianceCacheInterpolation.ush:372-395 — `SampleRadianceCacheInterpolated`

Interpolation is **trilinear** across the 8 corner probes of the cell
containing the query point, with optional stochastic interpolation for
performance.

Each probe sample uses **sphere parallax correction**:
@source UE5 LumenRadianceCacheInterpolation.ush:277-347

```hlsl
// Sphere parallax: intersect ray with probe's TMin sphere
T = RayIntersectSphere(WorldPos, Dir, float4(ProbePos, ReprojectionRadius));
IntersectionPos = WorldPos + Dir * T;
ReprojectedDir = IntersectionPos - ProbePos;
CorrectionFactor = T^2 / (R * dot(ReprojectedDir, Dir));
```

Sampling a single probe:
@source UE5 LumenRadianceCacheInterpolation.ush:221-269

```hlsl
FRadianceCacheSample SampleRadianceCacheProbe(uint ProbeIndex, float3 Dir, float MipLevel)
{
    float2 ProbeUV = InverseEquiAreaSphericalMapping(Dir);
    uint2 ProbeAtlasCoord = FinalProbeResolution * uint2(
        ProbeIndex & ModuloMask, ProbeIndex >> DivideShift);
    float2 AtlasUV = (ProbeAtlasCoord + ProbeUV * RadianceProbeRes + Border) * InvAtlasRes;
    Sample.Radiance = RadianceCacheFinalRadianceAtlas.SampleLevel(bilinear, AtlasUV, MipLevel);
}
```

---

## 6. Screen Probe Gather (Final Per-Pixel GI)

### 6.1 Screen Probe Placement

@source UE5 LumenScreenProbeCommon.ush:21-44

Screen probes are placed on a **downsampled grid** of the viewport:

```
ScreenProbeDownsampleFactor = 16     (one probe per 16x16 pixel tile)
ScreenProbeViewSize = viewport / 16  (e.g., 120x68 for 1920x1080)
```

Probe placement uses **temporal jitter** via Hammersley sequence:
@source UE5 LumenScreenProbeCommon.ush:72-76

```hlsl
uint2 GetScreenTileJitter(uint TemporalIndex)
{
    return Hammersley16(TemporalIndex, 8, 0) * ScreenProbeDownsampleFactor;
}
```

**Adaptive probes** are placed in high-detail regions where uniform probes
have poor depth/normal coverage.

### 6.2 Per-Pixel Interpolation and Upsampling

@source UE5 LumenScreenProbeGather.usf:136-195

Interpolation uses **bilinear weights modulated by plane distance**:

```hlsl
// Plane-weighted depth rejection
float4 ScenePlane = float4(WorldNormal, dot(WorldPosition, WorldNormal));
PlaneDistance = abs(dot(float4(ProbePosition, -1), ScenePlane));
RelativeDepthDiff = PlaneDistance / SceneDepth;
DepthWeight = exp2(ScreenProbeInterpolationDepthWeight * RelativeDepthDiff);
```

The final weight per corner probe = `BilinearWeight * DepthWeight`.

### 6.3 Screen Probe Tracing Resolution

@source UE5 LumenScreenProbeCommon.ush:21-23

```
ScreenProbeTracingOctahedronResolution = 8   (8x8 traces per probe)
ScreenProbeGatherOctahedronResolution  = 8   (resampled for filtering)
```

Each screen probe generates 64 rays (8x8 octahedral), traces them via
the combined Mesh SDF + Global SDF pipeline, and stores radiance in
an atlas texture.

---

## 7. Compositing — DiffuseIndirect Composite Pass

### 7.1 UE5 Compositing

@source UE5 IndirectLightRendering.cpp:118-221 — `FDiffuseIndirectCompositePS`

The composite pass has 3 modes (`DIM_APPLY_DIFFUSE_INDIRECT`):
- 0 = None (AO only)
- 1 = SSGI (upscale + apply)
- 2 = Lumen ScreenProbeGather (with screen-space bent normal)

It reads:
- `DiffuseIndirect_Lumen_0..3` — radiance atlas layers
- `AmbientOcclusionTexture` — GTAO/DFAO
- GBuffer (normals, depth, material properties)

And writes the composited diffuse indirect + specular reflections to
the scene color buffer via additive blending or dual-source blending.

### 7.2 Current UzEngine Compositing

@source UzEngine deferred_lighting_ps.hlsl:970-1069

UzEngine currently composites in the deferred lighting pass:
- SH-based diffuse IBL (`GetSkySHDiffuse`)
- Prefiltered specular IBL (cubemap sampling)
- SSR blending (lerp by confidence)
- SSGI additive (`diffuseIBL += ssgiIndirect * kD`)
- Multi-bounce AO (`AOMultiBounce`)
- Horizon occlusion, specular occlusion

The SDF GI system will replace/augment the SH-based diffuse IBL path
when Lumen-style probes are available.

---

## 8. UzEngine Implementation Plan

### 8.1 Architecture

```
runtime/engine/renderer/gi/
├── ssgi_renderer.h/.cpp          (EXISTS — screen-space GI, becomes near-field tier)
├── mesh_sdf_baker.h/.cpp         (DONE — GPU compute bake via mesh_sdf_bake_cs.hlsl, CPU fallback)
├── mesh_sdf_atlas.h/.cpp         (NEW — GPU brick atlas for mesh SDFs)
├── global_distance_field.h/.cpp  (NEW — clipmap GDF management)
├── radiance_cache.h/.cpp         (NEW — world-space probe cache)
├── screen_probe_gather.h/.cpp    (NEW — per-pixel GI gather + upsampling)
└── gi_compositor.h/.cpp          (NEW — combines all GI tiers)

runtime/shaders/runtime/gi/
├── mesh_sdf_trace.hlsl           (NEW — sphere march per-mesh SDF)
├── global_sdf_trace.hlsl         (NEW — sphere march global SDF)
├── mesh_sdf_bake_cs.hlsl         (DONE — GPU SDF bake, brute-force O(V*T), task 3.0.2)
├── global_sdf_composite_cs.hlsl  (DONE — composite mesh SDFs into GDF clipmaps, bindless)
├── radiance_cache_trace_cs.hlsl  (DONE — probe trace through GDF, bindless SH + GDF textures)
├── radiance_cache_filter_cs.hlsl (NEW — spatial + octahedral filter)
├── screen_probe_trace_cs.hlsl    (NEW — trace screen probes via SDF)
├── screen_probe_gather_cs.hlsl   (NEW — interpolate probes per-pixel)
└── gi_composite_cs.hlsl          (NEW — final composite into scene color)

runtime/shaders/runtime/include/
├── sdf_common.hlsli              (NEW — SDF sampling, distance decode)
└── radiance_cache_common.hlsli   (NEW — probe lookup, interpolation)
```

### 8.2 Dependency Order

```
Phase A: Mesh SDF Foundation
  A1: mesh_sdf_baker — CPU voxelization of mesh triangles into sparse brick SDF
  A2: mesh_sdf_atlas — GPU 3D texture atlas (brick allocator, indirection table)
  A3: sdf_common.hlsli — SDF sampling functions

Phase B: Global Distance Field
  B1: global_distance_field — Clipmap management (4 cascades, page table)
  B2: global_sdf_composite_cs — GPU compute: composite mesh SDFs into GDF pages
  B3: global_sdf_trace.hlsl — Sphere march through GDF (RayTraceGlobalDistanceField)

Phase C: Radiance Cache
  C1: radiance_cache — Probe allocation, clipmap probe grid, indirection texture
  C2: radiance_cache_trace_cs — SDF trace from probe centers, store octahedral radiance
  C3: radiance_cache_filter_cs — Spatial filter + octahedral border fixup
  C4: radiance_cache_common.hlsli — Probe lookup, trilinear interpolation

Phase D: Screen Probe Gather
  D1: screen_probe_gather — Screen-space probe placement (uniform + adaptive)
  D2: screen_probe_trace_cs — Per-probe SDF tracing (8x8 octahedral rays)
  D3: screen_probe_gather_cs — Per-pixel interpolation with depth-aware weighting

Phase E: Integration
  E1: gi_compositor — Combines radiance cache + screen probes + existing SSGI
  E2: Modify deferred_lighting_ps.hlsl — Route SDF GI into ambient section
  E3: Lua API bindings for GI quality settings
```

### 8.3 Files to Modify

| File | Changes |
|------|---------|
| `scene_renderer.h` | Add `GlobalDistanceField`, `RadianceCache`, `ScreenProbeGather` members |
| `frame_lighting.cpp` | Register GDF update, radiance cache trace, screen probe passes |
| `constant_buffers.h` | Add GDF shader parameters to PerFrameCB or new dedicated CB |
| `deferred_lighting_ps.hlsl` | Replace SH IBL path with radiance cache interpolation |
| `pipeline_library.h/.cpp` | Register new compute PSOs |
| `scene_lifecycle.cpp` | Init/Shutdown GDF and radiance cache resources |

---

## 9. Sub-Tasks (1-2 File Units)

### Phase A: Mesh SDF Foundation

**A1: Mesh SDF Baker** (CPU, 2 files)
- `mesh_sdf_baker.h` — `BakeMeshSDF(const Mesh& mesh) -> SparseSDFVolume`
- `mesh_sdf_baker.cpp` — Triangle-to-voxel distance computation, brick compression
- Constants: BrickSize=8, UniqueData=7, Band=4 voxels, Format=R8_UNORM
- @source UE5 DistanceFieldAtlas.h:36-50

**A2: Mesh SDF Atlas** (GPU, 2 files)
- `mesh_sdf_atlas.h` — `MeshSdfAtlas` class: 3D texture atlas, indirection buffer
- `mesh_sdf_atlas.cpp` — Brick allocator, upload, per-object transform buffer
- @source UE5 DistanceFieldAtlas.h:240-303, LumenSoftwareRayTracing.ush:140

**A3: SDF Common Shader** (1 file)
- `sdf_common.hlsli` — `SampleSparseMeshSDF()`, `DecodeSdfDistance()`, `LineBoxIntersect()`
- @source UE5 MeshDistanceFieldCommon.ush:9-38

### Phase B: Global Distance Field

**B1: GDF Manager** (2 files)
- `global_distance_field.h` — `GlobalDistanceField` class: 4-6 clipmaps, page table
- `global_distance_field.cpp` — Clipmap extent computation, page allocation, dirty tracking
- Page atlas: 128x128xN pages, 8^3 texels each (R8_UNORM 3D)
- Page table: R32_UINT 3D texture (resolution/7 per axis per clipmap)
- @source UE5 GlobalDistanceField.cpp:406-513

**B2: GDF Composite Shader** (1 file)
- `global_sdf_composite_cs.hlsl` — Per-page: cull mesh SDFs, sample and composite
- @source UE5 GlobalDistanceField.cpp (dispatch logic)

**B3: GDF Trace Shader** (1 file)
- `global_sdf_trace.hlsl` — `RayTraceGlobalDistanceField()` sphere march
- Max 256 steps, page lookup, mip early-out, surface expansion
- @source UE5 GlobalDistanceFieldUtils.ush:90-200

### Phase C: Radiance Cache

**C1: Radiance Cache Manager** (2 files)
- `radiance_cache.h` — `RadianceCache` class: clipmap probes, indirection 3D texture
- `radiance_cache.cpp` — Probe allocation, free list, dirty marking, budget
- Indirection: `R32_UINT` 3D texture (coord → probe atlas index)
- Atlas: `RGBA16F` 2D texture (octahedral layout, FinalProbeResolution per probe)
- @source UE5 LumenRadianceCacheInterpolation.ush:55-93

**C2: Radiance Cache Trace Shader** (1 file)
- `radiance_cache_trace_cs.hlsl` — Per-probe: trace octahedral rays via SDF
- Tile size 8x8, trace budget system (downsample/normal/supersample)
- @source UE5 LumenRadianceCacheTracingCommon.ush:1-72

**C3: Radiance Cache Filter Shader** (1 file)
- `radiance_cache_filter_cs.hlsl` — Spatial filter, octahedral border fixup
- @source UE5 LumenRadianceCache.usf:84-128

**C4: Radiance Cache Common Shader** (1 file)
- `radiance_cache_common.hlsli` — `GetRadianceProbeClipmap()`, `SampleRadianceCacheInterpolated()`
- Trilinear interpolation with sphere parallax correction
- @source UE5 LumenRadianceCacheInterpolation.ush:154-395

### Phase D: Screen Probe Gather

**D1: Screen Probe Manager** (2 files)
- `screen_probe_gather.h` — `ScreenProbeGather` class: probe placement, atlas management
- `screen_probe_gather.cpp` — Downsample factor=16, Hammersley jitter, adaptive probes
- @source UE5 LumenScreenProbeCommon.ush:21-48

**D2: Screen Probe Trace Shader** (1 file)
- `screen_probe_trace_cs.hlsl` — Per-probe 8x8 octahedral SDF traces
- @source UE5 LumenScreenProbeGather.usf:322-354

**D3: Screen Probe Gather Shader** (1 file)
- `screen_probe_gather_cs.hlsl` — Per-pixel: bilinear interpolation with plane weighting
- @source UE5 LumenScreenProbeGather.usf:136-195, 231-289

### Phase E: Integration

**E1: GI Compositor** (2 files)
- `gi_compositor.h` — `GICompositor` class: route between SSGI/radiance cache/screen probes
- `gi_compositor.cpp` — Per-frame: choose GI method, dispatch passes, output SRV indices

**E2: Deferred Lighting Integration** (1 file modified)
- `deferred_lighting_ps.hlsl` — Add radiance cache sampling path alongside existing SH IBL
- When radiance cache available: `diffuseGI = InterpolateRadianceCache(worldPos, normal)`
- Fallback chain: Screen Probes → Radiance Cache → SH IBL → Constant ambient

**E3: Lua API** (1 file modified)
- `lua_camera_api.cpp` or new `lua_gi_api.cpp` — `uz.gi.set_method()`, `uz.gi.set_quality()`

---

## 10. Memory Budget Estimates

| Resource | Size (1080p) | Notes |
|----------|-------------|-------|
| Per-mesh SDF atlas | 64 MB | 256^3 max per mesh, R8, ~50 meshes |
| GDF Page Atlas | 128 MB | 128x128xN pages, 8^3 R8 each |
| GDF Page Table | 2 MB | ~18^3 per clipmap x 4 clipmaps, R32 |
| GDF Mip Texture | 1 MB | 32^3 per clipmap x 4, R8 |
| GDF Coverage Atlas | 32 MB | 128x128xN pages, 4^3 R8 each |
| Radiance Probe Atlas | 32 MB | ~4096 probes, 32x32 RGBA16F each |
| Radiance Depth Atlas | 8 MB | ~4096 probes, 16x16 R16F each |
| Radiance Indirection | 4 MB | 32^3 x 6 clipmaps, R32 |
| Screen Probe Atlas | 8 MB | 120x68 probes, 8x8 RGBA16F each |
| **Total** | **~279 MB** | Comparable to UE5 Lumen SW |

---

## 11. Performance Budget (Target: 1080p, RTX 2070)

| Pass | Target GPU Time | Notes |
|------|----------------|-------|
| GDF Update (2 clipmaps) | 0.5 ms | Partial page update only |
| Radiance Cache Trace | 1.0 ms | ~200 probes/frame budget |
| Screen Probe Trace | 1.5 ms | 120x68 probes, 64 rays each |
| Screen Probe Gather | 0.3 ms | Full-res bilinear interpolation |
| GI Composite | 0.1 ms | Add to scene color |
| **Total** | **~3.4 ms** | ~5.5 ms/frame budget at 60fps |
