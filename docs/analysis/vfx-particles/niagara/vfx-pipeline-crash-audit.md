<!-- doc-meta
title: VFX Pipeline Crash Audit — Complete Bug Report
author: Claude Opus 4.6 (4-agent team)
date: 2026-04-02
tags: [vfx, crash, audit, dx12, root-signature, render-graph]
-->

# VFX Pipeline Crash Audit — 4-Agent Team Report

**Date**: 2026-04-02
**Agents**: compute-auditor, render-auditor, rootsig-auditor, rendergraph-auditor
**Symptom**: Engine crashes (segfault/device removed) after SkyAtmospherePass when
VFX emitters are present in scene. PBR scenes without VFX work perfectly.

---

## Root Causes (ordered by execution order)

### RC1. Constant buffer size mismatch — GPU reads uninitialized memory [FATAL]

| Shader | Expected CB Size | CPU Sends | Missing Data |
|--------|-----------------|-----------|--------------|
| particle_spawn_cs.hlsl (b1) | 192 bytes | 16 bytes | emitterPos, spawnVelocity, lifetime, color, ALL bindless indices |
| particle_update_cs.hlsl (b1) | 320 bytes | 16 bytes | gravity, damping, ALL read/write SRV/UAV indices, noise params |
| particle_indirect_args_cs.hlsl (b0) | 16 bytes | 16 bytes | Wrong fields (sends verticesPerParticle, shader expects aliveCountSrvIndex) |

**Effect**: Shaders read garbage bindless descriptor indices → `ResourceDescriptorHeap[garbage]` → device removed.

**Files**: `particle_simulation_pass.h:63-94`, `frame_particles.cpp:245-283`

### RC2. SetDescriptorTable(5) on nonexistent root parameter [FATAL]

Bindless root signature has 5 params (0-4). Slot 5 does NOT exist.

| File | Line | Call |
|------|------|------|
| particle_render_pass.cpp | 108 | `SetDescriptorTable(5, readSetSrvIndex)` |
| trail_render_pass.cpp | 80 | `SetDescriptorTable(5, ...)` |
| particle_mesh_render_pass.cpp | 114 | `SetDescriptorTable(5, ...)` |
| decal_pass.cpp | 112 | `SetDescriptorTable(5, ...)` |
| distortion_pass.cpp | 91 | `SetDescriptorTable(5, ...)` |

### RC3. Missing BeginRenderPass — draw calls execute with no render targets [FATAL]

None of the 3 VFX render passes call `cmd->BeginRenderPass()`. They declare
render graph color/depth targets but never resolve them via `res.GetTexture()`
and never bind RTVs. Drawing to null RTVs → device removed.

| File | Line |
|------|------|
| particle_render_pass.cpp | 176-204 |
| trail_render_pass.cpp | 120-157 |
| particle_mesh_render_pass.cpp | 172-207 |

### RC4. DrawIndexedIndirect without index buffer [FATAL]

`particle_render_pass.cpp:112` calls `DrawIndexedIndirect` but no index buffer
is bound. Billboard VS uses SV_VertexID (no IA). DX12 reads null index buffer → crash.

### RC5. SetRootCBV(0) on root constants parameter [HIGH]

Root param 0 is `D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS`, not CBV.
`SetRootCBV` calls `SetGraphicsRootConstantBufferView` which is wrong for this type.

| File | Line |
|------|------|
| particle_render_pass.cpp | 92 |
| particle_mesh_render_pass.cpp | 105 |
| distortion_pass.cpp | 76 |

**Fix**: Use `SetRootConstants(0, data, count)` instead.

### RC6. Manual barriers conflict with render graph state tracking [HIGH]

`EmitIndirectArgsBarriers()` (particle_simulation_pass.cpp:520) manually transitions
indirectDrawArgs from UAV→IndirectArgument. But the render graph tracked state is
still UAV. When the render pass declares `pb.Read(rgIndirectDrawArgs, IndirectArgument)`,
the render graph emits a redundant UAV→IndirectArgument barrier with wrong stateBefore.

### RC7. Indirect args CS register mismatch [HIGH]

Shader declares `cbuffer : register(b0)` but CPU binds to slot 1 (`SetRootCBV(1, ...)`).
The shader reads from b0 but data is at b1.

`particle_simulation_pass.cpp:310` + `particle_indirect_args_cs.hlsl:89`

### RC8. Null barrier resource handle [MEDIUM]

`frame_particles.cpp:826-829` emits a barrier with no buffer/texture handle set.

### RC9. Per-emitter CB overwrites PerFrameCB at slot 1 [MEDIUM]

Billboard render pass binds per-emitter data at slot 1, overwriting PerFrameCB.
Should use slot 2 (per-object).

### RC10. hasCompletedCycle set even on empty dispatch [LOW]

`frame_particles.cpp:848` sets the flag even if all emitters were skipped.

### RC11. Dead code — TransitionWriteSetToUAV / TransitionIndirectArgsToUAV [LOW]

`particle_simulation_pass.cpp:147-214` — never called from anywhere.

---

## Fix Plan (priority order)

### Phase 1: Stop the crash (RC1-RC5)

1. **RC1**: Create full CPU-side mirror structs for VFXSpawnConstants (192 bytes)
   and VFXUpdateConstants (320 bytes) matching HLSL layouts. Fill ALL bindless
   descriptor indices from the emitter's GPUParticleBuffer.

2. **RC2**: Remove ALL `SetDescriptorTable(5, ...)` calls from VFX render/decal/
   distortion passes. In bindless mode, resources are accessed via
   ResourceDescriptorHeap[index] — no descriptor tables needed.

3. **RC3**: Add `BeginRenderPass`/`EndRenderPass` to all 3 VFX render pass
   execution callbacks, resolving targets from render graph resources.

4. **RC4**: Change `DrawIndexedIndirect` to `DrawIndirect` (add command signature
   to RHI if needed), OR create an identity index buffer and bind it before draw.

5. **RC5**: Change `SetRootCBV(0, ...)` to `SetRootConstants(0, data, count)`
   in particle_render_pass, particle_mesh_render_pass, distortion_pass.

### Phase 2: Correctness (RC6-RC9)

6. **RC6**: Remove manual `EmitIndirectArgsBarriers` from sim pass. Declare
   indirect args as `pb.Write(rgIndirectArgs, IndirectArgument)` so the render
   graph tracks the exit state correctly.

7. **RC7**: Fix indirect args CS to use `register(b1)` matching `SetRootCBV(1)`,
   OR change dispatch to use slot 0 with `SetRootConstants`.

8. **RC8**: Add resource handle to the null barrier or remove it.

9. **RC9**: Move per-emitter CB to slot 2 (per-object) instead of slot 1 (per-frame).

### Phase 3: Cleanup (RC10-RC11)

10. **RC10**: Guard `hasCompletedCycle` behind a "had successful dispatches" check.
11. **RC11**: Delete dead barrier helper functions.
