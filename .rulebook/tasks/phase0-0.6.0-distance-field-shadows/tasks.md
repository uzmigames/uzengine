## 1. Mesh SDF Generation (Import-Time)

- [ ] 1.1 Implement SDF voxelizer: for each mesh, compute signed distance field on a 3D grid. Approach: for each voxel, find closest triangle and compute signed distance (inside/outside via normal direction).
- [ ] 1.2 Implement sparse brick encoding: divide SDF grid into 8x8x8 bricks. Only store bricks within 4 voxels of the surface (band-limited). Discard fully-inside and fully-outside bricks. Source: `DistanceFieldAtlas.h:34-50`.
- [ ] 1.3 Quantize SDF values to 8-bit (`PF_G8`): map signed distance range to [0, 255]. Source: UE5 uses unsigned byte encoding.
- [ ] 1.4 Generate 3 MIP levels of the SDF for adaptive step sizing during ray march.
- [ ] 1.5 Store SDF data in mesh asset file (`.uzmesh` sidecar or embedded). Include: brick data, indirection table, bounds, resolution.
- [ ] 1.6 Integrate SDF generation into asset import pipeline (glTF importer triggers SDF bake after mesh load).

## 2. Global SDF Atlas (GPU)

- [ ] 2.1 Create `SdfAtlasManager` class: manages a global GPU 3D texture atlas that holds all active mesh SDF bricks. Source: `DistanceFieldAtlas.h`, `DistanceFieldLightingShared.h:78-94`.
- [ ] 2.2 Implement sparse brick allocation: allocate/free 8x8x8 regions in the 3D atlas texture. Track free list.
- [ ] 2.3 Implement indirection table: `StructuredBuffer<uint>` mapping virtual brick index → physical atlas position. Source: `DistanceFieldLightingShared.h:82`.
- [ ] 2.4 Upload mesh SDF bricks to atlas when meshes enter the scene. Free bricks when meshes leave.
- [ ] 2.5 Create atlas 3D texture: `Texture3D<float>` (R8_UNORM), initial size 512x512x512 (expandable).

## 3. Object Data Buffers

- [ ] 3.1 Create `StructuredBuffer<SdfObjectBounds>`: per-mesh AABB in world space. Updated when meshes move. Source: `DistanceFieldLightingShared.h:69-76`.
- [ ] 3.2 Create `StructuredBuffer<SdfObjectData>`: per-mesh transform (world → local), SDF atlas brick offset, SDF resolution, local bounds. Source: `DistanceFieldLightingShared.h:70`.
- [ ] 3.3 Update both buffers every frame (or only when meshes move for static meshes).
- [ ] 3.4 Track total object count for dispatch sizing.

## 4. Tile-Based Object Culling (Compute)

- [ ] 4.1 Implement `CullObjectsForShadow` compute shader: frustum-cull mesh SDFs against shadow cone (convex hull + bounding sphere). Output: culled object index buffer. Source: `DistanceFieldShadowing.usf:83`.
- [ ] 4.2 Implement tile grid generation: divide screen into tiles (tile size ~200 world units). Source: `DistanceFieldShadowing.cpp:69`.
- [ ] 4.3 Implement `ScatterObjectsToShadowTiles`: rasterize culled object bounding boxes onto tile grid. Build per-tile object lists using atomics. Source: `DistanceFieldShadowing.usf:266-282`.
- [ ] 4.4 Output: per-tile object count + object index list (compact).

## 5. Cone Tracing Compute Shader

- [ ] 5.1 Implement `DistanceFieldShadowingCS` compute shader (8x8 thread groups). Source: `DistanceFieldShadowing.cpp:288`, `DistanceFieldShadowingShared.ush:66-221`.
- [ ] 5.2 Per-pixel: construct ray from surface position toward light. Ray length = `DistanceFieldShadowDistance`.
- [ ] 5.3 Implement sphere-tracing loop: sample SDF at current position, compute cone radius (`TanLightAngle * rayTime`), compute visibility (`saturate(distance / coneRadius)`), advance by `max(abs(distance), minStepSize)`.
- [ ] 5.4 Support quality levels: 20/32/64 steps via `#define DFS_QUALITY`.
- [ ] 5.5 Output: `RWTexture2D<float2>` — (shadow_factor, scene_depth) per pixel.
- [ ] 5.6 Implement half-resolution mode: trace at half-res, bilateral upsample to full-res. Source: `DistanceFieldShadowing.cpp:150`.

## 6. SDF Sampling

- [ ] 6.1 Implement `SampleSparseMeshSignedDistanceField(float3 worldPos, SdfObjectData obj)`: transform world pos to local SDF space, look up brick in indirection table, sample 3D atlas texture with trilinear filtering. Source: `DistanceFieldShadowingShared.ush`.
- [ ] 6.2 Handle band boundary: if position is outside the SDF band (>4 voxels from surface), return a large distance (skip quickly).
- [ ] 6.3 MIP selection: use `rayTime` to select coarser MIP for distant samples (larger steps, less precision needed).

## 7. Compositing

- [ ] 7.1 In deferred lighting pass: sample DFS output texture alongside CSM shadow.
- [ ] 7.2 Composite: `finalShadow = csmShadow * dfsShadow`. Both must be lit for pixel to be lit.
- [ ] 7.3 Update `ShadowDataCB` with `dfsOutputIndex` (SRV index for DFS result texture).
- [ ] 7.4 Update `shadow_common.hlsli` to sample DFS result and multiply with CSM.

## 8. Configuration & Integration

- [ ] 8.1 Add `DistanceFieldShadowConfig`: `enabled` (bool), `distance` (float, 51200cm), `quality` (0-3), `fullResolution` (bool), `tileWorldSize` (float, 200cm). Source: `DistanceFieldShadowing.cpp`.
- [ ] 8.2 Integrate into scene renderer: if DFS enabled and mesh SDFs available, dispatch culling + tracing before lighting pass.
- [ ] 8.3 Async compute support: DFS compute dispatches can run on async compute queue overlapping with other GPU work. Source: `DistanceFieldShadowing.cpp:93`.

## 9. Visual Verification

- [ ] 9.1 Screenshot: DFS disabled — standard CSM shadows (baseline)
- [ ] 9.2 Screenshot: DFS enabled — soft area shadows with variable penumbra
- [ ] 9.3 Screenshot: DFS quality comparison (low/medium/high) — step count effect on quality
- [ ] 9.4 Screenshot: half-res vs full-res DFS — bilateral upsample quality
- [ ] 9.5 Screenshot: DFS + CSM composited — smooth transition between systems
- [ ] 9.6 Performance capture: DFS cost at each quality level
- [ ] 9.7 Regression: all existing demos render correctly

## 10. Documentation

- [ ] 10.1 Document SDF generation pipeline: mesh → voxelizer → sparse bricks → atlas
- [ ] 10.2 Document cone tracing algorithm with diagrams
- [ ] 10.3 Document quality vs performance tradeoffs
- [ ] 10.4 Document integration with CSM and compositing
