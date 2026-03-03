## 1. Cluster Grid Construction

- [ ] 1.1 Define cluster grid constants in `src/renderer/passes/ClusteredLightPass.h`: `CLUSTER_X = 64`, `CLUSTER_Y = 36`, `CLUSTER_Z = 32`, `MAX_LIGHTS = 1024`, `MAX_LIGHTS_PER_CLUSTER = 128`
- [ ] 1.2 Implement `ClusterAABB` struct (min float3 + max float3) and allocate a `StructuredBuffer<ClusterAABB>` of size `64 * 36 * 32` in `ClusteredLightPass.cpp` on device init
- [ ] 1.3 Implement `ClusterBuildAABBs.hlsl` compute shader: for each cluster cell (dispatched 64x36x32), compute the frustum-aligned AABB using inverse projection of the cell's NDC min/max corners; distribute Z-slices logarithmically: `z_near * (z_far / z_near)^(slice / CLUSTER_Z)`
- [ ] 1.4 Trigger AABB recomputation in `ClusteredLightPass` whenever the camera projection matrix changes (FOV, aspect ratio, near/far plane change); dirty-flag set from `CameraSystem` on projection update
- [ ] 1.5 Bind the `StructuredBuffer<ClusterAABB>` to a UAV slot during the AABB build dispatch and then rebind as SRV for subsequent light assignment and shading passes

## 2. Light Assignment Compute Pass

- [ ] 2.1 Create `shaders/lighting/ClusterBuildDispatch.hlsl`: first dispatch clears per-cluster counter buffer (64x36x32 uint, zero-initialized); second dispatch runs `ClusterLightAssign.hlsl`; both dispatched via `ClusteredLightPass::dispatch()`
- [ ] 2.2 Create `shaders/lighting/ClusterLightAssign.hlsl`: dispatch group (64, 36, 32), one thread per cluster cell; for each of up to 1024 lights, test the light's bounding sphere against the cell AABB using separating-axis overlap test; on overlap, use `InterlockedAdd` to append the light index to the per-cluster `RWStructuredBuffer<uint>` light index list
- [ ] 2.3 Implement the per-cluster light count + offset buffer: a `StructuredBuffer<uint2>` of size `64*36*32` where `x = count`, `y = offset` into the flat light index list; compute offsets via a prefix-sum pass on the counter buffer after the assignment dispatch
- [ ] 2.4 Size the flat light index buffer as `1024 * 64 * 36 * 32` `uint` elements (approximately 150 MB); allocate as a `RWStructuredBuffer<uint>` with a commit-on-demand GPU heap if the D3D12 device supports resource heaps tier 2
- [ ] 2.5 Add `LightGrid3D` constant buffer struct to `GlobalParams` at register `b15` (as specified in `shader-library.md`): contains `cluster_count_x`, `cluster_count_y`, `cluster_count_z`, `z_near`, `z_far`, `light_count`

## 3. PBR Pixel Shader Cluster Lookup

- [ ] 3.1 In `shaders/pbr/PBR.hlsl`, replace the 2D tile lookup with the 3D cluster lookup: compute `z_slice = floor(CLUSTER_Z * log(view_depth / z_near) / log(z_far / z_near))`, compute `cluster_index = tile_x + tile_y * CLUSTER_X + z_slice * CLUSTER_X * CLUSTER_Y`
- [ ] 3.2 Fetch `uint2 cluster_info = ClusterCountOffset[cluster_index]`; iterate `cluster_info.x` lights starting at offset `cluster_info.y` in the light index list buffer
- [ ] 3.3 Clamp `z_slice` to [0, CLUSTER_Z - 1] and `cluster_index` to [0, CLUSTER_X * CLUSTER_Y * CLUSTER_Z - 1] before buffer access to prevent out-of-bounds reads on edge pixels

## 4. Remove Old 2D Tile Pass

- [ ] 4.1 Delete `shaders/lighting/TileLightCull.hlsl` from the repository
- [ ] 4.2 Remove `TileLightCull` dispatch call and all associated UAV/SRV bindings from `src/renderer/RenderGraph.cpp`
- [ ] 4.3 Remove the 2D tile grid cbuffer binding slot and `b14` register alias from `shader-library.md` and `GlobalParams`; update register slot documentation accordingly
- [ ] 4.4 Search all custom shader includes for references to the old tile grid sampler and update them to use the 3D cluster lookup pattern; document the breaking change in `CHANGELOG.md` with a migration code example

## 5. Debug Cluster Occupancy Overlay

- [ ] 5.1 Implement `r.ClusterDebug` console variable (int, default 0) in `src/renderer/passes/ClusteredLightPass.cpp`
- [ ] 5.2 When `r.ClusterDebug = 1`, render a full-screen debug overlay pass `ClusterDebugOverlay.hlsl` that reads the cluster count buffer and colors each pixel based on its cluster's light count: 0 lights = green, 1–4 = yellow, 5–8 = orange, 9+ = red (additive blend over scene color at 50% alpha)
- [ ] 5.3 Register `r.ClusterDebug` in the console command registry and document it in `docs/specs/console-commands.md`

## 6. Testing

- [ ] 6.1 Write `tests/renderer/test_clustered_shading.cpp`: test `ClusterBuildAABBs` output for a known camera frustum with specific near/far and FOV; verify the AABB at slice 0 matches expected near-plane cell dimensions, and the AABB at slice 31 matches the far-plane dimensions
- [ ] 6.2 Test logarithmic Z-slice distribution: verify that the midpoint slice index corresponds to the geometric mean of near and far distances (`sqrt(z_near * z_far)`) within 1% tolerance
- [ ] 6.3 Test `ClusterLightAssign` overlap detection: create a point light whose bounding sphere overlaps exactly 4 known cluster cells; verify those 4 cells have `count >= 1` and the others have `count = 0`
- [ ] 6.4 Test the 1024-light maximum: fill the scene with 1025 lights; verify only 1024 are assigned and the 1025th is silently dropped with a warning log
- [ ] 6.5 Test `z_slice` clamping: compute cluster index for a pixel at exactly `view_depth = z_near - 0.001` (behind near plane) and verify `z_slice = 0` (not negative)
- [ ] 6.6 Test `r.ClusterDebug` console command: set to 1 and verify the overlay pass is inserted into the render graph; set to 0 and verify it is absent
- [ ] 6.7 Run existing point-light and spot-light rendering tests to confirm no regressions after removal of `TileLightCull.hlsl`
- [ ] 6.8 Benchmark test: scene with 512 point lights, verify GPU frame time for the cluster assignment dispatch is under 0.5 ms on a simulated D3D12 reference device (use timing query)
- [ ] 6.9 Verify ≥95% line and branch coverage on `ClusteredLightPass.cpp` and the cluster lookup code path in the PBR pixel shader CPU-side reference test

## 7. Documentation

- [ ] 7.1 Update `docs/specs/renderer.md`: document the 64x36x32 cluster grid, logarithmic Z distribution formula, AABB precomputation trigger conditions, and `LightGrid3D` cbuffer layout
- [ ] 7.2 Update `docs/specs/shader-library.md`: remove `b14` tile grid entry, add `b15 LightGrid3D` with field descriptions and the `ClusterCountOffset` and `LightIndexList` buffer SRV slots
- [ ] 7.3 Update `docs/specs/console-commands.md`: document `r.ClusterDebug` command, accepted values, and overlay color legend
- [ ] 7.4 Add migration note to `docs/specs/point-spot-light-shadows.md` warning about the 2D tile grid removal and linking to the 3D cluster lookup pattern example
