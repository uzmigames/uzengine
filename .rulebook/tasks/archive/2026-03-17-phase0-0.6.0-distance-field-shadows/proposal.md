# Distance Field Shadows — UE5 Parity

## Priority: P3 — Advanced feature requiring SDF infrastructure (long-term)

## Summary

Distance Field Shadows (DFS) uses cone-tracing through mesh signed distance fields (SDFs) to compute soft area shadows with physically-accurate variable penumbra. Unlike PCSS (which adapts filter kernel size), DFS traces actual geometry distance fields for true area light shadows. UE5 uses DFS as a medium-range shadow system between CSM (close) and far cascades (distant), covering up to 512m with high-quality soft shadows.

This is a **large infrastructure feature** — it requires mesh SDF generation at import time, a global sparse brick atlas on GPU, tile-based culling, and a compute shader ray marcher. It is NOT a drop-in replacement for PCF/PCSS.

## UE5 Reference

### Architecture
**Source**: `DistanceFieldShadowing.usf`, `DistanceFieldShadowingShared.ush`, `DistanceFieldShadowing.cpp`

**Three-stage compute pipeline**:
1. **Object Culling** (`FCullObjectsForShadowCS`): frustum-cull mesh SDFs against shadow cone
2. **Tile-Based Culling** (`ScatterObjectsToShadowTiles`): rasterize bounding boxes onto 2D tile grid, build per-tile object lists
3. **Cone Tracing** (`FDistanceFieldShadowingCS`): ray march through SDFs with cone expansion based on light source angle

### Parameters
| Parameter | Default | Source |
|-----------|---------|--------|
| `DistanceFieldShadowDistance` | 51200 cm (512m) | `DLC.cpp:1018` |
| `r.DistanceFieldShadowing` | 1 (enabled) | `DistanceFieldShadowing.cpp:36` |
| `r.DFShadowQuality` | 3 (high: 64 steps) | `DistanceFieldShadowing.cpp` |
| `r.DFFullResolution` | 0 (half-res) | `DistanceFieldShadowing.cpp` |
| `r.DFShadowCullTileWorldSize` | 200 cm | `DistanceFieldShadowing.cpp` |
| `LightSourceAngle` | 0.5357° | `DirectionalLightComponent.h` |

### SDF Generation (Import-Time)
**Source**: `DistanceFieldAtlas.h:34-50`
- **Format**: 8-bit quantized (`PF_G8`), sparse brick atlas
- **Brick size**: 8x8x8 voxels (7x7x7 unique + 1 border for gradients)
- **Band**: Only store SDF within 4 voxels of surface (sparse)
- **MIP levels**: 3 (coarse-to-fine hierarchy)
- **Generated at**: Asset cook time, stored in mesh DDC (derived data cache)
- **NO runtime generation**

### Ray Marching Kernel
**Source**: `DistanceFieldShadowingShared.ush:66-221`
```
for each step:
  distance = SampleSparseMeshSignedDistanceField(position)
  coneRadius = clamp(TanLightAngle * rayTime, minRadius, maxRadius)
  visibility = max(saturate(distance / coneRadius), selfShadowVisibility)
  minVisibility = min(minVisibility, visibility)
  stepSize = max(abs(distance), minStepSize)  // Adaptive step (sphere tracing)
  rayTime += stepSize
```

### Quality Levels
| Level | Steps | SSS | Cost |
|-------|-------|-----|------|
| 0 (off) | 0 | No | 0 |
| 1 (low) | 20 | No | ~0.7ms |
| 2 (medium) | 32 | No | ~1.2ms |
| 3 (high) | 64 | Yes | ~2.5ms |

### Compositing
- DFS output: separate `RWTexture2D<float2>` (shadow_factor, scene_depth)
- Final shading: `finalShadow = csmShadow * dfShadow` (multiplicative)

## Prerequisites

This feature requires significant infrastructure that does NOT exist in UzEngine yet:

1. **Mesh SDF Generation** — Import-time SDF baker (sparse brick atlas from mesh triangles)
2. **Global SDF Atlas** — GPU 3D texture atlas with virtual addressing/indirection
3. **Sparse Brick Allocation** — Memory manager for 8x8x8 SDF bricks
4. **Object Data Buffers** — StructuredBuffer of mesh bounds, transforms, SDF asset indices
5. **Tile-Based Culling System** — GPU tile grid with per-tile object lists

### Dependency Chain
```
Mesh SDF Baker → SDF Atlas Manager → Object Data Buffers → Tile Culling → Cone Tracing → Compositing
```

## Impact

- **New files**: ~15 files (SDF baker, atlas manager, culling, cone trace shader, compositing)
- **VRAM**: 50-200MB for SDF atlas (depending on scene complexity)
- **GPU cost**: 1-3ms at 1080p high quality
- **Build time**: SDF generation adds seconds per mesh at import
- **Visual**: Physically accurate soft shadows with variable penumbra from actual geometry
- **Complexity**: HIGH — this is one of the most complex rendering features in UE5

## Acceptance Criteria

1. Mesh SDF generated at import time, stored as sparse brick atlas
2. Global SDF atlas on GPU with virtual addressing
3. Tile-based object culling for shadow cone
4. Cone tracing compute shader with adaptive sphere-tracing steps
5. Quality levels: low (20 steps), medium (32), high (64)
6. Half-resolution option with bilateral upsample
7. Compositing: DFS * CSM for final shadow
8. Light source angle drives penumbra softness
9. `DistanceFieldShadowDistance` configurable (default 512m)
10. Async compute support (runs on async compute queue)
11. Demo: scene with varied geometry — soft shadows with accurate penumbra
12. Performance within 3ms at 1080p high quality
