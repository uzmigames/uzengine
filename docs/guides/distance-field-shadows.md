# Distance Field Shadows

<!-- doc-meta
title: Distance Field Shadows
category: guide
engine: uzengine
theme: rendering
created: 2026-03-17
updated: 2026-03-17
status: active
author: docs-operator
-->

## Overview

Distance field shadows (DFS) provide fast dynamic shadow coverage for all geometry by pre-baking a sparse signed distance field (SDF) atlas at import time. At runtime, a cone-tracing compute shader ray-marches through the SDF to determine shadow coverage, operating independently of traditional shadow maps. This enables global shadow updates without multi-cascading and efficiently handles distant occluders.

## SDF Generation Pipeline

### Mesh Baking

When a static mesh is imported, the `SdfBaker` generates a signed distance field:

1. **Spatial Hash Triangle Lookup** — Accelerates nearest-triangle queries for large meshes. The baker partitions the mesh AABB into a grid, hashing triangles to grid cells.

2. **Point-to-Triangle Distance** — For each voxel center, compute the unsigned distance to the closest triangle's surface. Uses Cartesian distance formula in local mesh space (centimeters).

3. **Sign Determination (Pseudo-Normal Method)** — The sign is determined by the dot product of the (voxel center − closest surface point) displacement vector with the triangle normal at the closest point. Positive dot = outside, negative = inside.

4. **Quantization to 8-bit** — Voxel values are stored as unsigned char (0–255). The range `[−64cm, 64cm]` is remapped to `[0, 255]`, with 128 representing the zero-crossing (surface).

### Sparse Brick Encoding

To save VRAM, only voxel bricks that contain surface detail are stored:

- **Brick Size**: 8×8×8 voxels per brick (`SdfBrick::kSize = 8`).
- **Sparse Storage**: The indirection table maps logical brick coordinates to physical storage offsets. Empty bricks (all voxels far from surface) are marked as `UINT32_MAX` and skipped.
- **Band Width**: Only bricks with at least one voxel within `bandWidthMultiplier × voxelSize × 8` of the surface are retained. Default multiplier is 4.0.

**Source**: UE5 `DistanceFieldAtlas.h:34-50`.

### MIP Levels

Three MIP levels are generated from the base level via downsampling:

- **MIP 0** (base): 8×8×8 bricks
- **MIP 1**: 4×4×4 bricks (parent voxels reduce via min absolute distance)
- **MIP 2**: 2×2×2 bricks
- **MIP 3**: 1×1×1 brick (optionally skipped if too coarse)

Each MIP halves the resolution per axis. The shader can sample coarser MIPs for distant shadows.

**Source**: UE5 `DistanceFieldAtlas.h` — MIP chain for LOD cone tracing.

## Cone Tracing Algorithm

### Ray Marching with Sphere Expansion

The DFS shadow computation uses cone tracing: a ray steps from the pixel surface toward the light, expanding a cone at each step to account for the distance field's uncertainty:

```
For each ray step:
  sampleDist = SampleSdf(rayPos)      // Distance to nearest surface
  coneRadius = rayDist * coneAngle    // Cone angle from light viewpoint
  shadowFactor *= Clamp(sampleDist / coneRadius, 0, 1)  // Penumbra blend
  rayPos += stepDist * lightDir
```

The cone angle is derived from the light's angular size or a fixed soft-shadow radius. As the cone expands, the marcher becomes less strict: a negative SDF value (inside geometry) at a far distance still contributes to penumbra rather than hard shadow.

### Adaptive Step Size

The ray-marching loop uses the SDF value itself to determine the next step size:

```
stepDist = max(sampleDist, minStepSize)
```

If `sampleDist` is large, the ray safely steps far. If close to the surface, smaller steps refine the contact boundary.

### Early Termination

Marching stops when:
- The shadow factor reaches near-zero (fully shadowed).
- The ray exceeds the max shadow distance (51200 cm = 512 m by default).
- Step count limit is reached.

**Source**: UE5 `DistanceFieldShadowing.cpp` — ray march loop and termination.

## Quality Levels and Performance

DFS quality is controlled via the `DfsQuality` enum. Higher quality increases ray-march steps, improving shadow accuracy:

| Quality | Step Count | Approximate Time (1080p Half-Res) | Use Case |
|---------|------------|-----------------------------------|----------|
| Off | 0 | 0 ms | Disabled |
| Low | 20 | ~0.7 ms | Mobile, low-end GPU |
| Medium | 32 | ~1.2 ms | Mainstream GPU |
| High | 64 | ~2.5 ms | High-end GPU, demanding scenes |

Time is GPU frame time at half resolution. Full-resolution tracing (no bilateral upsample) costs ~2× the half-res time.

### Half-Resolution Optimization

By default, DFS traces at half-resolution and upsamples via bilateral filtering, which preserves edges while smoothing. This trades shadow precision for 4× speedup on fill-rate-bound GPUs.

**Configuration**:
```cpp
DistanceFieldShadowConfig cfg;
cfg.quality = DfsQuality::High;
cfg.fullResolution = false;  // Use half-res + bilateral upsample
```

### Atlas VRAM Budget

Each mesh's SDF bricks occupy storage in the global atlas:

| Resolution | Sparse Factor | Per-Mesh (Typical) | Budget Capacity |
|------------|---------------|-------------------|-----------------|
| 64³ | 0.3–0.5 | 0.5–2 MB | 256 MB |
| 128³ | 0.3–0.5 | 2–8 MB | 256 MB |

Sparse encoding is crucial: fully dense 128³ would be 2 MB per mesh. Sparse storage with 40% density factor reduces it to ~0.8 MB.

## CSM Compositing

The final shadow value composites cascade shadow maps (CSM) with DFS:

```hlsl
float csmShadow = SampleCascadeShadow(pixelPos, depth);
float dfsShadow = ConeTraceShadow(pixelPos, surfaceNormal, lightDir);
float finalShadow = csmShadow * dfsShadow;  // Multiplicative blend
```

**Reasoning**: A pixel is fully lit only if both CSM and DFS agree it is unoccluded. CSM provides precise nearby shadows; DFS handles distant occluders.

### Distance Cutoff

DFS is used only for distances beyond the last CSM cascade. Beyond 51200 cm (512 m), shadows are fully determined by DFS; CSM is disabled.

```cpp
cfg.distance = 51200.0f;  // UE5 default (DirectionalLightComponent.cpp:1018)
```

### Self-Shadow Bias

A self-shadow prevention bias is applied to surface samples to avoid shadow acne from SDF quantization noise:

```cpp
cfg.selfShadowBias = 2.0f;  // cm, UE5 default
```

The baker quantizes to 8-bit with ~0.5 cm precision; a 2 cm bias prevents the surface from self-shadowing due to bake-time noise.

## Runtime Integration

### Enable/Disable

DFS is disabled by default. Enable via:

```cpp
// C++
DistanceFieldShadowConfig cfg;
cfg.enabled = true;
cfg.quality = DfsQuality::High;
shadowRenderer.SetDfsConfig(cfg);
```

Or from Lua:
```lua
uz.renderer.set_dfs_enabled(true)
uz.renderer.set_dfs_quality("High")
```

### Mesh Registration

When a static mesh is imported, if the asset has a baked SDF (stored as a `.uzsdf` sidecar file), the SDF atlas manager automatically registers it:

```cpp
// In MeshImporter::Import()
if (sdfAsset.IsValid()) {
    uint32_t atlasOffset = sdfAtlasManager.RegisterMesh(meshId, sdfAsset);
    // Store atlasOffset in mesh metadata for runtime lookups
}
```

At runtime, the deferred lighting shader queries the atlas by brick offset and samples the SDF at the surface point.

## Key Takeaways

- **SDF Baking**: Offline generation from mesh triangles; sparse brick encoding saves 60–70% VRAM.
- **Cone Tracing**: Ray-march with sphere expansion; cone angle provides soft shadows.
- **Quality vs Cost**: 20–64 steps depending on GPU tier; half-res mode trades accuracy for 4× speedup.
- **CSM Compositing**: Multiplicative blend of cascade + DFS shadows for complementary coverage.
- **Self-Shadow Bias**: 2 cm default prevents acne from 8-bit SDF quantization.
- **Atlas Budget**: Typical meshes cost 0.5–2 MB; 256 MB total budget capacity.

## References

- UE5 `DistanceFieldAtlas.h:34-50` — Sparse brick layout and MIP chain
- UE5 `DistanceFieldShadowing.cpp` — Cone tracing algorithm and tile culling
- UE5 `DirectionalLightComponent.cpp:1018` — Max DFS distance (51200 cm)
- UE5 `MeshUtilities.cpp` — Mesh SDF baking (Embree-accelerated)
- [Distance Field Shadow Config](../../runtime/engine/renderer/shadows/distance_field_shadow_config.h)
- [SDF Baker Header](../../runtime/engine/asset/importers/sdf_baker.h)
- [SDF Atlas Manager](../../runtime/engine/renderer/shadows/sdf_atlas_manager.h)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-17 | Initial creation — SDF baking, cone tracing, quality levels, CSM compositing |
