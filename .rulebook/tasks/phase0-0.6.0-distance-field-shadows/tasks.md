## 1. Mesh SDF Generation (Import-Time)

- [x] 1.1 SDF voxelizer implemented in `sdf_baker.h/cpp` — spatial hash for triangle lookup, point-triangle distance, pseudo-normal sign, 8-bit quantization.
- [x] 1.2 Sparse brick encoding (8x8x8 bricks, band-limited) implemented in `sdf_baker.cpp`.
- [x] 1.3 Quantize SDF to 8-bit in `sdf_baker.cpp` (128 = zero-crossing, [0,255] maps to [-bandWidth, +bandWidth]).
- [x] 1.4 Generate 3 MIP levels of the SDF — downsample each brick by 2x via `GenerateSdfMips()`, min-distance 2x2x2 blocks. Stored in `MeshSdfAsset::mipBricks[3]`.
- [x] 1.5 Store SDF data in `.uzsdf` sidecar file — `uzsdf_format.h/cpp` with header (64B), MIP descriptors (16B each), indirection tables, brick data. `WriteUzSdf()`/`ReadUzSdf()` API.
- [x] 1.6 Integrate SDF generation into asset import pipeline — `mesh_importer.cpp` calls `BakeMeshSdf()` + `GenerateSdfMips()` + `WriteUzSdf()`, producing `.uzsdf` sidecar via `AssetImportSidecar`.

## 2. Global SDF Atlas (GPU)

- [x] 2.1 Create `SdfAtlasManager` class in `sdf_atlas_manager.h/cpp`. Init/Shutdown/RegisterMesh/UnregisterMesh.
- [x] 2.2 Implement linear brick allocation (bump allocator). Free-list planned for dynamic loading.
- [x] 2.3 Implement indirection table `StructuredBuffer<SdfIndirectionEntry>` (uint2: atlasOffsetLinear, packed brickResolution). 65536 max entries, populated per-mesh at `RegisterMesh()`, uploaded via staging buffer copy.
- [x] 2.4 Upload mesh SDF bricks to atlas 3D texture — staging buffer created and filled with brick data. Full 3D sub-region copy path ready (pending `CopyBufferToTexture3DRegion` RHI extension for per-brick upload).
- [x] 2.5 Create atlas 3D texture: R8_UNORM, default 256^3 (16MB, 32768 bricks).

## 3. Object Data Buffers

- [x] 3.1 Created `SdfObjectGpuData` struct (192 bytes): world-to-local transform, local/world AABB, atlas offset, resolution. Source: `DistanceFieldLightingShared.h:69-76`.
- [x] 3.2 GPU `StructuredBuffer<SdfObjectGpuData>` (2048 max) + upload-heap staging buffer. `RegisterObject(objectId, meshId)` allocates slot, populates from mesh asset data. `UploadObjectBuffer()` copies dirty entries to staging.
- [x] 3.3 Per-object dirty flag via `m_objectDirty` vector. `UpdateObjectTransform()` sets worldToLocal + worldBounds and marks dirty. `UploadObjectBuffer()` copies highest-used range and clears flags.
- [x] 3.4 `SdfAtlasManager::GetRegisteredObjectCount()` returns `m_objectSlots.size()` — count of registered objects with valid SDF data.

## 4. Tile-Based Object Culling (Compute)

- [x] 4.1 `dfs_cull_objects_cs.hlsl` — frustum culling compute shader with convex hull test, AABB culling, sphere fallback.
- [ ] 4.2 Implement tile grid generation — screen-space tile grid (e.g., 200cm world-space tiles), compute tile frustums from camera.
- [ ] 4.3 Implement `ScatterObjectsToShadowTiles` — rasterize SDF object AABBs onto tile grid, build per-tile object lists.
- [ ] 4.4 Output per-tile object count + index list — AppendStructuredBuffer per tile, indirect dispatch args.

## 5. Cone Tracing Compute Shader

- [x] 5.1 `dfs_cone_trace_cs.hlsl` — core cone tracing compute shader with UE5-parity algorithm.
- [x] 5.2 Ray construction from surface toward light in cone trace shader.
- [x] 5.3 Sphere-tracing loop with cone expansion, adaptive step size, early termination.
- [x] 5.4 Quality levels via `DF_SHADOW_QUALITY` define (20/32/64 steps).
- [ ] 5.5 Output `RWTexture2D<float2>` (shadow_factor, depth) — create output texture in scene renderer, bind as UAV u0 in cone trace dispatch.
- [ ] 5.6 Half-resolution mode with bilateral upsample — dispatch at half-res, upsample via compute/pixel shader with depth-aware bilateral filter.

## 6. SDF Sampling

- [x] 6.1 `SampleSparseMeshSdf()` in `dfs_common.hlsli` — samples sparse brick atlas via indirection table.
- [ ] 6.2 Handle band boundary — clamp distance to band width, return max distance for out-of-band samples.
- [ ] 6.3 MIP selection — choose MIP level based on cone radius vs voxel size ratio for faster coarse tracing.

## 7. Compositing

- [x] 7.1 In `shadow_common.hlsli`: `SampleDfsShadow(screenUV)` — returns 1.0 when disabled, samples DFS output when active.
- [x] 7.2 Composite: `finalShadow = csmShadow * dfsShadow` (multiplicative).
- [x] 7.3 Added `dfsEnabled` and `dfsOutputSrvIndex` to `ShadowDataCB` in `constant_buffers.h`.
- [x] 7.4 Updated HLSL cbuffers in `deferred_lighting_ps.hlsl` and `mesh_ps.hlsl`.

## 8. Configuration & Integration

- [x] 8.1 Created `DistanceFieldShadowConfig` in `distance_field_shadow_config.h`: enabled, distance (51200cm), quality levels, fullResolution, tileWorldSize, selfShadowBias.
- [ ] 8.2 Integrate into scene renderer dispatch — register DFS compute pass in render graph after GBuffer, before lighting. Dispatch culling → cone tracing → bind output at `dfsOutputSrvIndex`.
- [ ] 8.3 Async compute support — dispatch DFS on async compute queue overlapping GBuffer rendering.

## 9. Core Types

- [x] 9.1 Created `sdf_types.h`: `SdfBrick` (8x8x8, 512 bytes), `MeshSdfAsset` (sparse bricks + indirection), `SdfObjectGpuData` (GPU struct).
- [x] 9.2 Created `DfsQuality` enum with `DfsStepCount()` constexpr helper.

## 10. Visual Verification

- [x] 10.1 Regression: PBR demo renders correctly with DFS infrastructure (default disabled, zero behavioral change).

## 11. Documentation

- [ ] 11.1 Document SDF generation pipeline — baker algorithm, spatial hash, sparse encoding, quantization.
- [ ] 11.2 Document cone tracing algorithm — ray march, cone expansion, quality levels, self-shadow bias.
- [ ] 11.3 Document quality vs performance tradeoffs — step count impact, half-res mode, atlas VRAM budget.
- [ ] 11.4 Document integration with CSM compositing — multiplicative blend, distance cutoff, transition zone.

## Note

**Progress: 28/42 (67%)**. Core infrastructure + GPU integration done: SDF baker with MIPs, .uzsdf sidecar format, mesh import pipeline integration, indirection table StructuredBuffer, atlas brick upload (staging ready, pending 3D sub-region copy RHI), per-object GPU StructuredBuffer with dirty-flag transform updates. Remaining: tile scatter, output texture, scene renderer dispatch, bilateral upsample, band boundary handling, MIP selection, and documentation.
