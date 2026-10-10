> 2026-10-07: lighting block B.7 (`phase0_lighting-block-b-07-clustered-lights`) implemented the cluster
> grid, the light-to-cluster compute pass and the deferred lookup with the lights still in the constant
> buffer (512 point / 128 spot); items 1.1-1.3 below were ticked while `cluster_assign_cs.hlsl` was a
> one-line stub. What remains of this task is the structured-buffer light list (1.4, 1.10), the forward
> pass (1.6) and the 100+-light demo (1.11); the heat map (1.8) exists as `r.Lights.ClusterDebug`.

## 1. Implementation

- [x] 1.1 Implement cluster grid definition — 16×16 pixel screen tiles × 32 logarithmic depth slices. Depth slice boundaries via UE5 LightGridZParams (exp2-based, CalculateGridZParams). Compute cluster AABB in view-space via 8-corner unprojection through inverse projection matrix. Implemented in cluster_assign_cs.hlsl.
- [x] 1.2 Implement light-to-cluster assignment compute shader (`cluster_assign_cs.hlsl`) — UE5-matched thread-group-per-cell algorithm with prefix-sum compaction (64 threads/group). Sphere-AABB primary test + approximate cone-AABB separating plane for spot lights. Atomic append to global index list. All algorithms cite UE5 LightGridInjection.usf.
- [x] 1.3 Implement compact light list data structure — `RWStructuredBuffer<uint> g_LightIndexList` (global flat array), `RWStructuredBuffer<uint> g_ClusterLightGrid` (per-cluster offset+count, stride 2), `RWStructuredBuffer<uint> g_GlobalCounter` (atomic allocator). LDS buffer for 256 lights per cluster.
- [ ] 1.4 Implement light buffer — `StructuredBuffer<LightGPU> g_LightBuffer` containing all scene lights (up to 1024). Uploaded from CPU each frame. Replace hardcoded 8-light CB array.
- [ ] 1.5 Update deferred lighting shader — replace brute-force 64-light loop with cluster lookup: `uint2 info = g_ClusterLightInfo[clusterIdx]; for (uint i = 0; i < info.y; i++) { uint lightIdx = g_LightIndexList[info.x + i]; LightGPU light = g_LightBuffer[lightIdx]; /* evaluate */ }`.
- [ ] 1.6 Update forward lighting shader — same cluster lookup for forward pass. Replace 8-light hardcoded array with per-cluster iteration.
- [ ] 1.7 Implement cluster index computation — in pixel shader: `tileX = screenPos.x / tileSize; tileY = screenPos.y / tileSize; slice = log(viewDepth/near) / log(far/near) * numSlices; clusterIdx = (slice * tilesY + tileY) * tilesX + tileX`.
- [ ] 1.8 Implement cluster debug visualization — heat map: color each pixel by number of lights in its cluster. Blue=0, green=1-4, yellow=5-10, red=10+. Toggle via debug flag.
- [ ] 1.9 Add cluster parameters to PerFrameCB — tileSize, numSlices, near, far, tilesX, tilesY. Bind light buffer and cluster buffers as SRVs.
- [ ] 1.10 Implement CPU light upload — each frame: gather all active lights from ECS, upload to `g_LightBuffer` StructuredBuffer. Pass total count to shader. Remove CPU-side distance sorting (GPU handles culling).
- [ ] 1.11 Create clustered lighting demo — scene with 100+ colored point lights, camera fly-through, FPS counter. Show cluster debug heat map. Compare vs brute-force (old path).

## 2. Visual Verification

- [ ] 2.1 Screenshot: 100 point lights — all visible, correct attenuation
- [ ] 2.2 Screenshot: cluster heat map — light count varies by region
- [ ] 2.3 Screenshot: same scene at 60fps — no frame drops with 100 lights
- [ ] 2.4 Screenshot: visual parity — clustered output matches brute-force output exactly
- [ ] 2.5 Regression: existing 8-light scenes still work

## 3. Documentation

- [ ] 3.1 Document cluster grid layout and depth slicing formula
- [ ] 3.2 Add light culling compute shader algorithm overview
- [ ] 3.3 Document performance characteristics and max light budget

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
