## 1. Implementation

- [ ] 1.1 Define InstanceData struct — float4x4 world, float4x4 prevWorld, uint materialBufferIndex. Packed as StructuredBuffer<InstanceData> in bindless heap.
- [ ] 1.2 Implement InstanceBatcher — iterates sorted draw list, groups consecutive entries with same (meshHandle, materialHandle) into instance batches. Outputs batch list with (mesh, material, instanceCount, instanceBufferOffset).
- [ ] 1.3 Allocate per-frame instance buffer — StructuredBuffer large enough for all instances (max 65536). Ring-allocated per frame to avoid GPU stalls.
- [ ] 1.4 Upload instance data — fill StructuredBuffer with InstanceData for each batch. Map, memcpy, unmap pattern using upload ring.
- [ ] 1.5 Create INSTANCED vertex shader variant — #ifdef INSTANCED: read InstanceData[SV_InstanceID] from StructuredBuffer via bindless index. Transform position/normal using instance world matrix.
- [ ] 1.6 Emit DrawIndexedInstanced — for each batch: bind mesh VB/IB, set instance buffer SRV root constant, call DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0).
- [ ] 1.7 Fallback path — objects that cannot be instanced (unique mesh+material) continue using per-object CB path unchanged.
- [ ] 1.8 Integrate with GBuffer and shadow passes — both passes must support instanced drawing. Shadow pass uses same instance buffer.
- [ ] 1.9 Create instancing demo — 1000 identical cubes in a grid, showing single draw call in stats overlay. Compare draw call count: instanced vs non-instanced.

## 2. Visual Verification

- [ ] 2.1 Screenshot: 1000 instanced cubes — each must have correct world transform, no visual artifacts
- [ ] 2.2 Screenshot: instanced objects with shadows — CSM shadows must be correct per-instance
- [ ] 2.3 Performance: measure draw calls and frame time — 1000 objects must collapse to ~1 draw call per pass
- [ ] 2.4 Regression: non-instanced scenes (pbr_test, deferred_test) unchanged

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/07-sort-key-architecture.md` with instancing batch logic
- [ ] 3.2 Document instance buffer layout and shader access pattern
