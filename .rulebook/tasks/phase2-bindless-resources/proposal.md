# Proposal: phase2-bindless-resources

## Why

The current root signature uses fixed descriptor table slots (t0–t7 for SRVs, b0–b3 for CBVs). Every draw call with a different material must rebind descriptor tables. A scene with 1000 unique materials = 1000 CPU-side descriptor table updates per frame — this is a major CPU bottleneck and prevents GPU-driven rendering where the GPU generates its own draw calls.

Additionally, DX12 root signatures are compiled into PSO objects — changing the signature in Phase 3 to support bindless would require recompiling every PSO, breaking every existing shader. This must be designed before Phase 2 PBR renderer ships.

## What Changes

- `DescriptorHeap` class with thread-safe alloc/free (atomic CAS), deferred GPU-safe free
- `BindlessManager` singleton: `register_texture()`, `register_buffer()`, `register_uav()` returning `uint32_t` heap indices
- New bindless root signature: 4 root parameters (DrawConstants, SceneCBV, SRV heap table, Sampler heap table)
- `DrawConstants` root constants: material_idx, transform_idx, instance_offset, flags
- `StructuredBuffer<Material>` holding all per-material texture indices and PBR factors
- HLSL `ResourceDescriptorHeap[index]` usage replacing all `Texture2D : register(tN)`
- Migration of all Phase 1 shaders to bindless pattern

## Impact

- Affected specs: `docs/specs/rendering/bindless-resources.md`, `docs/specs/rendering/rhi.md`
- Affected code: `src/engine/renderer/rhi/`, all HLSL shaders in `shaders/`
- Breaking change: YES — all existing shaders must be updated; root signature changes all PSOs
- User benefit: 10–100× draw call reduction via batching; enables GPU-driven rendering in Phase 4; cleaner shader code
