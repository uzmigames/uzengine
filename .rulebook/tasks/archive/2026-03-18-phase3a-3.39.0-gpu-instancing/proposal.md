# GPU Instancing

## Why

Each identical mesh is currently a separate draw call with its own per-object CB upload. A scene with 500 trees = 500 draw calls. GPU instancing batches identical (mesh, material) pairs into a single DrawIndexedInstanced call, reducing CPU overhead by 10-50x for foliage/props. P2 gap — required for any scene with repeated geometry. No dependencies.

## What Changes

### New Systems
1. **InstanceBatcher** — groups draw list entries by (mesh, material) key, allocates StructuredBuffer<InstanceData> per batch
2. **InstanceData** — per-instance: float4x4 world matrix, float4x4 prevWorld (for velocity), uint materialIndex
3. **INSTANCED shader variant** — vertex shader reads InstanceData[SV_InstanceID] instead of per-object CB

### Refactoring
- Draw list builder detects instancable groups and emits batched DrawIndexedInstanced
- Per-object CB path remains for unique objects
- mesh_vs.hlsl gains #ifdef INSTANCED path reading from StructuredBuffer

## Impact
- Affected specs: `docs/engine/12-rendering/07-sort-key-architecture.md`
- Affected code: `runtime/engine/renderer/core/draw_list.*`, `runtime/shaders/mesh_vs.hlsl`
- Breaking change: NO
- User benefit: 10-50x draw call reduction for repeated geometry
- Enables: GPU-driven rendering (Phase 4), foliage, particle rendering
