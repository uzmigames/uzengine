<!-- doc-meta
title: Root Signature Layout Audit
category: analysis/architecture
created: 2026-03-24
updated: 2026-03-24
-->

# Root Signature Layout Audit

## Summary

- **Total Budget**: 64 DWORDs (DX12 hard limit)
- **Current Usage**: 52 DWORDs (5 root params)
- **Available Headroom**: 12 DWORDs
- **Static Samplers**: 4 (s0 aniso wrap, s1 PCF, s2 bilinear clamp, s3 bilinear wrap)

## Graphics Root Signature Layout

| Slot | Type | Register | DWORDs | Change Freq | Purpose |
|------|------|----------|--------|-------------|---------|
| 0 | Root Constants | b0 | 16 | Per-draw | Descriptor indices (slots 0-3 active, 4-15 reserved) |
| 1 | Root CBV | b1 | 16 | Per-frame | PerFrameCB (768 bytes) — camera, time, lighting, IBL, post-process |
| 2 | Root CBV | b2 | 16 | Per-draw/frame | PerObjectCB (256B) OR LightingCB (5632B) — shared slot |
| 3 | Root CBV | b4 | 4 | Per-frame | ShadowDataCB (2304B) — cascades, local shadows |
| 4 | UAV Table | u0-u3 | 0 | Per-compute | 4 UAVs for compute shaders |
| 5 (SM5) | SRV Table | t0+ | 0 | Per-draw | Unbounded SRV table (SM5 fallback) |

## Constant Buffer Sizes

| CB | Size | Slot | Binding |
|----|------|------|---------|
| PerFrameCB | 768B (3×256) | b1 | Root CBV, once per frame |
| PerObjectCB | 256B (1×256) | b2 | Root CBV, per draw call |
| LightingCB | 5632B (22×256) | b2 | Root CBV, per fullscreen pass (reuses slot 2) |
| ShadowDataCB | 2304B (9×256) | b4 | Root CBV, once per frame |

## Root Slot 2 Contention

Slot 2 is dual-purpose:
- **Geometry passes**: binds PerObjectCB (256B per draw)
- **Fullscreen passes**: binds LightingCB (5632B per frame, 64 point + 16 spot lights)

This works because geometry and fullscreen passes don't overlap, but limits future flexibility.

## Optimization Opportunities

### Immediate (no functionality loss)
1. **Root Constants 16→8**: Only indices 0-3 actively used. Saves **8 DWORDs**.
2. **Move ShadowDataCB to descriptor table**: Saves **4 DWORDs** (root CBV cost).

### Medium term (requires shader changes)
3. **MaterialData StructuredBuffer**: Replace 10 individual texture indices in PerObjectCB with a single materialID → StructuredBuffer<MaterialData> lookup.
4. **Merge PerFrameCB + ShadowDataCB**: Single combined CB at b1. Saves 1 root slot.

### Long term (major refactor)
5. **Bindless descriptor buffers (SM6.6+)**: Eliminate UAV/SRV tables entirely.

## Source Files

- `runtime/engine/rhi/dx12/dx12_pso_creation.cpp:109-292` — root signature creation
- `runtime/engine/renderer/core/pipeline/root_slots.h` — slot definitions
- `runtime/engine/renderer/core/params/constant_buffers.h` — CB layouts
