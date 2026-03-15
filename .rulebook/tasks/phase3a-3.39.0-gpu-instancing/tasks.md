## 1. Implementation

- [x] 1.1 Define InstanceData struct — float4x4 world, float4x4 normalMatrix (128 bytes). Created in `instance_batcher.h`.
- [x] 1.2 Implement InstanceBatcher — iterates sorted draw list, groups consecutive entries with same (meshId, material SRV indices) into instance batches. Created `instance_batcher.cpp`.
- [x] 1.3 Allocate per-frame instance buffer — Persistent StructuredBuffer with power-of-two capacity, resized on demand. Upload heap with WaitIdle sync.
- [x] 1.4 Upload instance data — Map/memcpy/unmap pattern on persistent Upload buffer. WaitIdle before write to prevent race with in-flight frames.
- [x] 1.5 Create INSTANCED vertex shader variant — `#ifdef UZ_INSTANCED` in mesh_vs.hlsl and shadow_depth_vs.hlsl. Reads InstanceData[SV_InstanceID] via ResourceDescriptorHeap (SM6.6 bindless only).
- [x] 1.6 Emit DrawIndexedInstanced — GBufferPass and ShadowPass both have instanced code paths. Per-batch PerObjectCB for shared material, DrawIndexedInstanced(indexCount, batchInstanceCount, 0, 0, firstInstance).
- [x] 1.7 Fallback path — non-instanced path preserved in both passes. SM5 (non-bindless) always uses per-object CB path. Instancing only activates when bindless=true.
- [x] 1.8 Integrate with GBuffer and shadow passes — both passes support instanced drawing via `instanced` flag + `batchData`/`batchCount` in pass desc.
- [ ] 1.9 Create instancing demo — 1000 identical cubes in a grid, showing single draw call in stats overlay. Compare draw call count: instanced vs non-instanced.

## 2. Visual Verification

- [ ] 2.1 Screenshot: 1000 instanced cubes — each must have correct world transform, no visual artifacts
- [x] 2.2 Screenshot: instanced objects with shadows — pbr_test scene (5 cubes + ground) renders correctly with instancing, CSM shadows work per-instance
- [ ] 2.3 Performance: measure draw calls and frame time — 1000 objects must collapse to ~1 draw call per pass
- [x] 2.4 Regression: non-instanced scenes (pbr_test, deferred_test) unchanged — pbr_test verified visually, no artifacts

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/07-sort-key-architecture.md` with instancing batch logic
- [ ] 3.2 Document instance buffer layout and shader access pattern
