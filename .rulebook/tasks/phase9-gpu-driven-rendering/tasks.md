## 1. Prerequisites

- [ ] 1.1 Confirm `phase3-bindless-resources` task is complete: all textures and vertex/index buffers are registered in a bindless descriptor heap accessible from GPU compute shaders
- [ ] 1.2 Confirm `phase3-gpu-instancing` task is complete; the `InstanceDataBuffer` layout from that task will be extended here

## 2. InstanceDataBuffer Population

- [ ] 2.1 Define `struct InstanceData` in `src/renderer/renderer.h` with fields: `world_transform` (float4x4), `mesh_id` (uint32_t), `material_id` (uint32_t), `bounding_sphere` (float4: center xyz + radius w)
- [ ] 2.2 In `src/renderer/renderer.cpp`, at the start of each frame, upload all active entity world transforms, mesh IDs, and material IDs into a `StructuredBuffer<InstanceData>` `InstanceDataBuffer`
- [ ] 2.3 Size the buffer for a maximum of 100,000 instances; use a ring-buffer upload heap to avoid per-frame GPU synchronization

## 3. Hierarchical Z-Buffer Generation

- [ ] 3.1 Create `src/renderer/hzb_pass.cpp` implementing an HZB (Hierarchical Z-Buffer) generation pass: downsample the previous frame's depth buffer into a mip chain using `max()` reduction
- [ ] 3.2 Generate mip levels from full-resolution down to 1x1; store each mip level as a separate SRV in the bindless heap
- [ ] 3.3 Run the HZB generation pass immediately after the depth pre-pass, before the cull compute shader

## 4. GPU Occlusion Culling Compute Shader

- [ ] 4.1 Create `src/renderer/cull_pass.comp.hlsl` reading `InstanceDataBuffer` and the HZB mip chain; for each instance, test the bounding sphere against the camera frustum and HZB occlusion
- [ ] 4.2 Implement frustum test: cull instances whose bounding sphere does not intersect any of the 6 camera frustum planes
- [ ] 4.3 Implement HZB occlusion test: project the bounding sphere to screen space; sample the HZB mip level whose texel size matches the projected screen extent; cull if the sphere's closest depth > the sampled HZB depth
- [ ] 4.4 Write surviving instance draw arguments as `D3D12_DRAW_INDEXED_ARGUMENTS` into `DrawIndirectBuffer` using `AppendStructuredBuffer`
- [ ] 4.5 Write surviving instance count to a `DrawCountBuffer` (`uint`); used by `ExecuteIndirect` as the count buffer

## 5. ExecuteIndirect Draw Loop

- [ ] 5.1 In `src/renderer/renderer.cpp`, replace the per-entity draw loop with a single `ID3D12GraphicsCommandList::ExecuteIndirect()` call consuming `DrawIndirectBuffer` and `DrawCountBuffer`
- [ ] 5.2 Configure the `ID3D12CommandSignature` with `D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED` for the `ExecuteIndirect` call
- [ ] 5.3 Bind `InstanceDataBuffer` as a root SRV so each draw can index per-instance data in the vertex shader

## 6. Debug Counter Readback

- [ ] 6.1 After each frame, read back the value of `DrawCountBuffer` to the CPU via a readback heap copy
- [ ] 6.2 Display `drawn = N / total = M` in the profiler debug overlay row for the GPU-driven rendering pass
- [ ] 6.3 Store the drawn count in the ring buffer stats for profiler history graphing

## 7. Bindless Resource Heap Integration

- [ ] 7.1 Register all vertex buffers and index buffers in the bindless descriptor heap at load time with a stable `mesh_id` that matches the `InstanceData::mesh_id` field
- [ ] 7.2 In `cull_pass.comp.hlsl` and the indirect draw vertex shader, index the bindless heap by `mesh_id` to fetch the correct vertex/index buffer SRV

## 8. Testing

- [ ] 8.1 Create `tests/renderer/test_gpu_driven.cpp` testing `InstanceData` upload: verify a scene with 1,000 entities produces a `InstanceDataBuffer` with 1,000 non-zero entries
- [ ] 8.2 Test frustum culling: an entity outside the camera frustum has its `bounding_sphere` fail the frustum test; verify it does not appear in `DrawIndirectBuffer`
- [ ] 8.3 Test HZB generation: verify mip level count equals `ceil(log2(max(width, height))) + 1`; verify each mip has correct dimensions
- [ ] 8.4 Test draw count readback: after a frame with N visible entities, the readback value matches N (within 5% tolerance for async readback timing)
- [ ] 8.5 Test regression: all existing renderer unit tests continue to pass after the per-entity draw loop replacement with `ExecuteIndirect`
- [ ] 8.6 Test maximum capacity: a scene with 100,000 entities does not overflow `DrawIndirectBuffer`; verify no GPU validation errors
- [ ] 8.7 Test `ExecuteIndirect` call: verify the DX12 debug layer reports zero errors for the indirect draw call with a valid command signature
- [ ] 8.8 Achieve >= 95% line and branch coverage across `renderer.cpp` instance buffer upload, `hzb_pass.cpp`, and the cull pass dispatch code

## 9. Documentation

- [ ] 9.1 Update `docs/specs/renderer.md` documenting the GPU-driven rendering pipeline: `InstanceDataBuffer` layout, HZB pass, cull compute shader, `ExecuteIndirect`, and debug counter readback
- [ ] 9.2 Update `docs/specs/gpu-instancing.md` documenting the extended `InstanceData` struct with bounding sphere and draw argument packing
- [ ] 9.3 Update `docs/specs/debug-profiling.md` documenting the drawn vs. total entity count display in the profiler overlay
