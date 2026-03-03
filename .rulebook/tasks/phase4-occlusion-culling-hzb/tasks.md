## 1. HZB Build Pass (Mip Chain Generation)

- [ ] 1.1 Create `src/engine/renderer/passes/hzb_pass.h` and `hzb_pass.cpp` implementing `HzbPass::Build(cmd_list, depth_srv)` that generates a full HZB mip chain from the resolved depth buffer; allocate a `DXGI_FORMAT_R32_FLOAT` texture with `log2(max(width, height))` mip levels at HZB pass initialization
- [ ] 1.2 Integrate `FidelityFX SPD` (Single Pass Downsampler, AMD GPUOpen) from vcpkg to generate all HZB mip levels in a single compute dispatch with 8x8 thread groups; add `ffx-spd` to `vcpkg.json`
- [ ] 1.3 Add a fallback naive multi-pass downsample path in `hzb_pass.cpp` (one compute dispatch per mip level, reading min depth from a 2x2 footprint) controlled by CVar `r.hzb_spd` (int, default `1`); when `0`, use the naive path for debugging
- [ ] 1.4 Register `HzbPass::Execute()` in the render graph in `src/engine/renderer/render_graph.cpp` immediately after the depth prepass and before the GBuffer pass; add a `UAV` barrier on the HZB texture between the SPD dispatch and the occlusion test pass

## 2. Occlusion Test Compute Shader

- [ ] 2.1 Write `src/shaders/hzb_occlusion_test.hlsl`: a compute shader with one thread per entity; read `StructuredBuffer<HzbInstance> g_Instances` containing `world_aabb_min`, `world_aabb_max`, and `entity_draw_id`; reproject all 8 AABB corners into NDC using `g_PrevViewProj` from the scene CBV
- [ ] 2.2 In `hzb_occlusion_test.hlsl`, compute the screen-space footprint of the projected AABB, select the appropriate HZB mip level as `ceil(log2(max(footprint_x, footprint_y)))`, sample the HZB at the computed mip with bilinear filtering, and compare the minimum projected Z against the sampled HZB value; write `1` (visible) or `0` (occluded) to `RWStructuredBuffer<uint> g_OcclusionResults`
- [ ] 2.3 Handle the first-frame edge case in `hzb_occlusion_test.hlsl`: if an entity's `entity_draw_id` is not yet in `g_Instances` (detected via a validity flag), write `1` (conservatively visible) to avoid one-frame pop-in

## 3. Occlusion Result Buffers

- [ ] 3.1 Allocate a persistent double-buffered `StructuredBuffer<uint> g_OcclusionResults[2]` in `src/engine/renderer/culling.cpp` indexed by `frame_index % 2`; write current frame's test results to buffer `N`, read previous frame's results from buffer `N-1` when building draw arguments
- [ ] 3.2 Implement `HzbInstance` struct population in `src/engine/renderer/culling.cpp` during the existing frustum cull pass: for each frustum-visible entity, copy its world-space AABB (already computed for frustum rejection) into a staging `HzbInstance` array; upload to `g_Instances` GPU buffer via `CopyBufferRegion` before the occlusion test dispatch

## 4. DrawSubmitter Integration

- [ ] 4.1 Modify `DrawSubmitter` in `src/engine/renderer/draw_submitter.cpp` to read the previous frame's `g_OcclusionResults` buffer when building `DrawIndexedIndirect` argument buffers; for each entity with `g_OcclusionResults[entity_id] == 0`, set `instance_count = 0` in the draw arguments to suppress the draw without removing it from the buffer (preserving index stability)
- [ ] 4.2 Add a `r.hzb_enabled` CVar (int, default `1`); when `0`, skip the occlusion result read-back in `DrawSubmitter` and treat all entities as visible (disables HZB culling without changing the render graph structure)

## 5. Two-Phase Occlusion (Optional Second Pass)

- [ ] 5.1 Add CVar `r.hzb_two_phase` (int, default `0`); when `1`, after the GBuffer pass, re-run the occlusion test using the current frame's newly built HZB and collect entities that become visible in phase 2 (i.e., were occluded in phase 1 but are now visible in the updated HZB)
- [ ] 5.2 Dispatch a second `DrawIndexedIndirect` for the phase-2 newly-visible entities, appending their draw calls to the GBuffer pass output; this eliminates one-frame pop-in on fast camera cuts at the cost of one additional GPU dispatch per frame

## 6. Tracy Profiling and Debug Overlay

- [ ] 6.1 Add Tracy GPU zone markers in `HzbPass::Build()` and the occlusion test dispatch using `TracyD3D12Zone(ctx, cmdlist, "HZB Build")` and `TracyD3D12Zone(ctx, cmdlist, "HZB Occlusion Test")`
- [ ] 6.2 Add CPU-side Tracy plots in `draw_submitter.cpp`: `TracyPlot("hzb_visible_objects", visible_count)`, `TracyPlot("hzb_culled_objects", culled_count)`, and `TracyPlot("hzb_cull_ratio", culled_count / total_count)` updated each frame
- [ ] 6.3 Add CVar `r.hzb_debug` (int, default `0`); when `1`, bind the HZB mip 0 texture as an SRV to a fullscreen debug blit pass and overlay it on screen using `src/shaders/debug_texture_blit.hlsl` to visualize the HZB for shader debugging

## 7. Testing

- [ ] 7.1 Create `tests/renderer/test_hzb_pass.cpp` using a mock command list recorder; verify `HzbPass::Build()` dispatches the SPD compute shader once (not N times for N mips) when `r.hzb_spd == 1`
- [ ] 7.2 Test naive fallback path (`r.hzb_spd == 0`): verify `log2(max(width, height))` separate compute dispatches are recorded for a 512x256 depth buffer
- [ ] 7.3 Test the UAV barrier is inserted between the SPD dispatch and the next pass that reads the HZB as an SRV
- [ ] 7.4 Create `tests/renderer/test_hzb_occlusion.cpp` testing the mip level selection formula: an AABB with screen-space footprint `32x32` pixels selects mip level `5` (`ceil(log2(32))`)
- [ ] 7.5 Test that an entity fully behind a wall (max AABB Z greater than the HZB sample at that mip) is marked `0` (occluded) in the output results buffer
- [ ] 7.6 Test that an entity in front of a wall (min AABB Z less than the HZB sample) is marked `1` (visible) in the output results buffer
- [ ] 7.7 Test first-frame edge case: entity with no prior-frame AABB data defaults to `1` (visible) in the occlusion results buffer
- [ ] 7.8 Test `DrawSubmitter`: verify an entity with `g_OcclusionResults[id] == 0` produces a `DrawIndexedIndirect` argument with `instance_count == 0`; verify an entity with `1` produces `instance_count == 1`
- [ ] 7.9 Test `r.hzb_enabled 0` causes `DrawSubmitter` to produce `instance_count == 1` for all entities regardless of the occlusion results buffer content
- [ ] 7.10 Test Tracy counters: simulate 100 entities with 40 occluded and verify `hzb_culled_objects == 40`, `hzb_visible_objects == 60`, `hzb_cull_ratio` approximately `0.4`
- [ ] 7.11 Run coverage -- all new files in `src/engine/renderer/passes/` and `src/engine/renderer/` must reach >= 95% line and branch coverage before marking complete
