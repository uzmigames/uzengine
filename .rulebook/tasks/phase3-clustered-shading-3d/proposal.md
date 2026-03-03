# Proposal: phase3-clustered-shading-3d

## Why
The current forward+ pipeline uses flat 2D tile-based light culling, which is efficient only when lights are spread uniformly across the screen. In tall environments such as skyscrapers, underground caves, or multi-story interiors, many lights occupy the same screen tile but at very different depths, forcing every pixel in the tile to evaluate all of them regardless of whether they are near or far. 3D clustered shading divides the frustum into Z-slices, turning each screen tile into a column of depth cells so that only lights actually overlapping a pixel's depth range are evaluated. This enables hundreds of dynamic lights per scene without per-pixel overdraw scaling linearly with light count.

## What Changes
- Build cluster grid: 64x36x32 frustum-aligned AABB cells, Z-slices distributed logarithmically from near to far plane; cell AABBs precomputed once per resolution/FOV change into a `StructuredBuffer<ClusterAABB>`
- `ClusterLightAssign.hlsl` (compute): for each light, determine which cluster cells its bounding sphere overlaps and append the light index to a per-cluster `RWStructuredBuffer<uint>` light index list; uses thread-group atomic append
- `ClusterBuildDispatch.hlsl`: first pass clears cluster counters, second pass fills the light-index buffer; dispatches (64, 36, 32) groups each frame on the compute queue
- Update `PBR.hlsl` pixel shader: replace 2D tile lookup with 3D cluster lookup — compute `cluster_index = xy_tile + z_slice * 64 * 36`, fetch light count + offset from cluster buffer, iterate only those lights
- Add `LightGrid3D` cbuffer slot to `GlobalParams` (b15 in `shader-library.md`) alongside existing light list buffer bindings
- `CClusterShadingDebug` toggle: debug overlay draws cluster occupancy as a color-coded overlay (green = 0 lights, yellow = moderate, red = max), toggled via console command `r.ClusterDebug 1`
- Maximum lights per scene raised from 256 to 1024 (light index list buffer sized accordingly at 1024 * 64 * 36 * 32 uint indices)
- Remove the old 2D tile compute pass `TileLightCull.hlsl`; replace entirely with the new 3D cluster pass

## Impact
- Affected specs: `renderer.md`, `shader-library.md`, `point-spot-light-shadows.md`, `gpu-instancing.md`, `console-commands.md`
- Affected code: `src/renderer/passes/ClusteredLightPass.cpp`, `shaders/lighting/ClusterLightAssign.hlsl`, `shaders/lighting/ClusterBuildDispatch.hlsl`, `shaders/pbr/PBR.hlsl`, `src/renderer/RenderGraph.cpp`
- Breaking change: YES — removes `TileLightCull.hlsl` and the 2D tile light grid cbuffer binding; any custom shaders that sample the old tile grid must be updated to the cluster lookup
- User benefit: Scenes with tall geometry and dense dynamic lighting (city streets at night, dungeon corridors, particle-lit interiors) render at full speed with hundreds of lights instead of suffering quadratic per-pixel light evaluation from overlapping 2D tiles.
