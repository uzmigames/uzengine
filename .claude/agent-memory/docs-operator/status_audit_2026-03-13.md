---
name: Render Graph & RHI Status Audit (2026-03-13)
description: Documentation status corrections for completed render graph implementation
type: project
---

## Audit Completed: Documentation Status Updates

**Date**: 2026-03-13
**Scope**: Corrected outdated status markers in rendering and RHI documentation

### Changes Made

1. **docs/engine/12-rendering/04-render-graph.md** (L3)
   - Changed: `Status: Planned (Phase 3a-3.1.0)` → `Status: Done (Phase 3a-3.1.0)`
   - Rationale: Render graph fully implemented (RgBuilder, RgPass, RgResourcePool, topological sort, barrier scheduling, pass culling, resource pooling all working)

2. **docs/engine/12-rendering/05-render-graph-guide.md** (L3)
   - Changed: `Status: Planned (Phase 3a-3.1.0)` → `Status: Done (Phase 3a-3.1.0)`
   - Rationale: Guide documents completed implementation

3. **docs/engine/03-rhi/06-barriers.md** (L139, L144)
   - Updated Engine Comparison table: Changed UzEngine column from "Manual ResourceBarrier() calls (render graph planned)" → "Automatic via render graph (RgBuilder, section 12.4)"
   - Removed "Future" paragraph that said "When UzEngine implements its Render Graph"
   - Rationale: Render graph now handles automatic barrier insertion between passes

4. **docs/engine/03-rhi/12-gpu-memory.md** (L3)
   - Changed: `Status: Planned (Phase 1.5 basic D3D12MA, Phase 2 descriptors + streaming, Phase 3 residency)` → `Status: Partial (D3D12MA done, Descriptors/Bindless done, Streaming/Residency planned)`
   - Rationale: D3D12MA v3.1.0 and bindless descriptor heaps are implemented; streaming/residency still planned

5. **docs/engine/README.md** (L35)
   - Changed rendering pipeline status from: `**Planned (3a-3.1.0)**` → `**Partial (3a-3.1.0)**`
   - Bolded "Render world, render graph" in description (now completed)
   - Rationale: Render graph core and render world documented and implemented

### Verified Not Needed

- DX12 SM5.0 backend: Still Planned (correct)
- DX11 backend: Still Planned (correct)
- PSO disk cache: Still Planned (correct)
- Async compute queue: Still Planned (correct)
- GPU memory streaming/residency: Still Planned (correct)
- HDR swap chain: Still Planned (correct)
- Metal backend: Partial (correct)

### Key Architectural Notes

**Automatic Barrier Insertion Now Works**:
- Render graph passes declare resource reads/writes via `pb.Read()` / `pb.Write()`
- RgBuilder::Compile() performs topological sort and schedules barriers automatically
- No manual ResourceBarrier() calls needed in pass execution callbacks
- Engine comparison updated to reflect parity with UE5 FRDGBuilder approach

**GPU Memory Status**:
- Phase 1.5 (D3D12MA integration): ✅ Done
- Phase 2 (Descriptor heaps + bindless SM6.6): ✅ Done
- Phase 3 (Streaming/residency/defragmentation): ⏱️ Planned

### Documentation Quality

All status tags now accurately reflect implementation state:
- **Done**: Core render graph API, DX12 backend, Vulkan backend, RHI types
- **Partial**: GPU memory management, rendering pipeline (material system + lighting still planned)
- **Planned**: Advanced features (streaming, async compute, PSO cache, etc.)
