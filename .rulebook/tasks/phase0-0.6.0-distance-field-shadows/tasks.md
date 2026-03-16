## 1. Mesh SDF Generation (Import-Time)

- [ ] 1.1 Implement SDF voxelizer: for each mesh, compute signed distance field on a 3D grid. (Deferred — requires import pipeline)
- [ ] 1.2 Implement sparse brick encoding: 8x8x8 bricks, band-limited. Source: `DistanceFieldAtlas.h:34-50`. (Deferred)
- [ ] 1.3 Quantize SDF values to 8-bit (`PF_G8`). (Deferred)
- [ ] 1.4 Generate 3 MIP levels of the SDF. (Deferred)
- [ ] 1.5 Store SDF data in mesh asset file. (Deferred)
- [ ] 1.6 Integrate SDF generation into asset import pipeline. (Deferred)

## 2. Global SDF Atlas (GPU)

- [x] 2.1 Create `SdfAtlasManager` class in `sdf_atlas_manager.h/cpp`. Init/Shutdown/RegisterMesh/UnregisterMesh.
- [x] 2.2 Implement linear brick allocation (bump allocator). Free-list planned for dynamic loading.
- [ ] 2.3 Implement indirection table StructuredBuffer on GPU. (Deferred — requires brick upload)
- [ ] 2.4 Upload mesh SDF bricks to atlas. (Deferred — requires SDF baker)
- [x] 2.5 Create atlas 3D texture: R8_UNORM, default 256^3 (16MB, 32768 bricks).

## 3. Object Data Buffers

- [x] 3.1 Created `SdfObjectGpuData` struct (192 bytes): world-to-local transform, local/world AABB, atlas offset, resolution. Source: `DistanceFieldLightingShared.h:69-76`.
- [ ] 3.2 Create GPU StructuredBuffer of `SdfObjectGpuData`. (Deferred — requires scene integration)
- [ ] 3.3 Update buffers when meshes move. (Deferred)
- [ ] 3.4 Track total object count for dispatch sizing. (Deferred)

## 4. Tile-Based Object Culling (Compute)

- [ ] 4.1 Implement `CullObjectsForShadow` compute shader. (Deferred — requires object buffers)
- [ ] 4.2 Implement tile grid generation. (Deferred)
- [ ] 4.3 Implement `ScatterObjectsToShadowTiles`. (Deferred)
- [ ] 4.4 Output per-tile object count + index list. (Deferred)

## 5. Cone Tracing Compute Shader

- [ ] 5.1 Implement `DistanceFieldShadowingCS` compute shader. (Deferred — requires atlas + culling)
- [ ] 5.2 Construct ray from surface toward light. (Deferred)
- [ ] 5.3 Sphere-tracing loop with cone expansion. (Deferred)
- [ ] 5.4 Support quality levels (20/32/64 steps). (Deferred)
- [ ] 5.5 Output `RWTexture2D<float2>` (shadow_factor, depth). (Deferred)
- [ ] 5.6 Half-resolution mode with bilateral upsample. (Deferred)

## 6. SDF Sampling

- [ ] 6.1 Implement `SampleSparseMeshSignedDistanceField()`. (Deferred — requires atlas upload)
- [ ] 6.2 Handle band boundary. (Deferred)
- [ ] 6.3 MIP selection. (Deferred)

## 7. Compositing

- [x] 7.1 In `shadow_common.hlsli`: `SampleDfsShadow(screenUV)` — returns 1.0 when disabled, samples DFS output when active.
- [x] 7.2 Composite: `finalShadow = csmShadow * dfsShadow` (multiplicative).
- [x] 7.3 Added `dfsEnabled` and `dfsOutputSrvIndex` to `ShadowDataCB` in `constant_buffers.h`.
- [x] 7.4 Updated HLSL cbuffers in `deferred_lighting_ps.hlsl` and `mesh_ps.hlsl`.

## 8. Configuration & Integration

- [x] 8.1 Created `DistanceFieldShadowConfig` in `distance_field_shadow_config.h`: enabled, distance (51200cm), quality levels, fullResolution, tileWorldSize, selfShadowBias.
- [ ] 8.2 Integrate into scene renderer dispatch. (Deferred — requires compute pipeline)
- [ ] 8.3 Async compute support. (Deferred)

## 9. Core Types

- [x] 9.1 Created `sdf_types.h`: `SdfBrick` (8x8x8, 512 bytes), `MeshSdfAsset` (sparse bricks + indirection), `SdfObjectGpuData` (GPU struct).
- [x] 9.2 Created `DfsQuality` enum with `DfsStepCount()` constexpr helper.

## 10. Visual Verification

- [x] 10.1 Regression: PBR demo renders correctly with DFS infrastructure (default disabled, zero behavioral change).

## 11. Documentation

- [ ] 11.1 Document SDF generation pipeline. (Deferred)
- [ ] 11.2 Document cone tracing algorithm. (Deferred)
- [ ] 11.3 Document quality vs performance tradeoffs. (Deferred)
- [ ] 11.4 Document integration with CSM compositing. (Deferred)

## Note
Core DFS infrastructure complete: config types, SDF data types, atlas manager (Init/Shutdown/Register/Unregister), compositing shader hook, CB expansion. The actual SDF baker, GPU compute shaders (culling + cone tracing), and scene integration are deferred — they require the import pipeline, mesh data access, and compute dispatch infrastructure.
