<!-- doc-meta
title: VFX Fix Plan v2 — Crash Root Cause + Priority Fixes
author: engine-architect (synthesis)
date: 2026-04-02
tags: [vfx, crash, dx12, device-removed, fix-plan, synthesis]
status: binding-directive
subsystem: vfx
-->

# VFX Fix Plan v2 — Crash Root Cause + Priority Fixes

**Synthesized from**: vfx-pipeline-crash-audit.md (11 bugs) + vfx-architectural-gaps.md + current
code state (frame_particles.cpp, particle_simulation_pass.cpp, particle_render_pass.cpp,
particle_render_pass.h, gpu_particle_buffer.cpp, vfx_cb_structs.h, particle_billboard_vs.hlsl,
particle_indirect_args_cs.hlsl).

**Current crash**: DEVICE_REMOVED on Frame 2 during compute dispatch. Frame 1 completes;
Frame 2 dies inside VFX_ParticleSimulation or VFX_ParticleRender.

---

## 1. Crash Root Cause Analysis

### Frame 0 (emitter created)

- `needsInitialUpload = true` → emitter skipped from render graph import AND sim/render.
- `VFX_InitFreeList` pass runs: uploads free list (Common→CopyDst→UAV), freeListHead, aliveCount.
- All buffers are in UAV at frame end.
- `hasCompletedCycle = false`.

### Frame 1 (first actual simulation)

- Swap is skipped (`needsInitialUpload` was reset, `hasCompletedCycle` still false).
- `writeSetState = UAV` (correct for firstCycle), `readSetState = UAV`, `aliveState = UAV`,
  `indirectState = UAV`.
- Sim pass runs: DispatchSpawn → DispatchUpdate → DispatchIndirectArgs. All UAV. No barriers
  needed between them. **This succeeds.**
- Render pass runs: uses `cpuAliveCount = 0` → `Draw(0, ...)` → no-op. **Frame 1 completes.**
- CB ring marks `hasCompletedCycle = true`.

### Frame 2 (DEVICE_REMOVED)

This is where the crash occurs. Here is the exact execution path:

**Step 1 — Swap executes** (`hasCompletedCycle = true`, `needsInitialUpload = false`):
  - WriteSet becomes the old ReadSet (was UAV after sim wrote to it). ✅
  - ReadSet becomes the old WriteSet (was UAV from frame 1's sim output). ✅
  - This is correct.

**Step 2 — Import with wrong initial states:**
  - `writeSetState = SRV` (firstCycle = false). **BUG**: the write set (new WriteSet = old
    ReadSet) was left in UAV by frame 1's sim, not SRV. The render graph imports it as SRV,
    but the actual D3D12 resource state is UAV.
  - `indirectState = IndirectArgument` (firstCycle = false). **BUG**: After frame 1, the
    indirect args CS wrote indirectDrawArgs and left it in UAV. `EmitIndirectArgsBarriers()`
    was removed from the code (RC6 fix), so indirectDrawArgs is still in UAV state. The
    render graph imports it as `IndirectArgument` — mismatched.
  - When the render graph emits barriers for `VFX_ParticleSimulation` (wants UAV), the
    stateBefore it supplies is SRV (the imported state). D3D12 executes a UAV barrier with
    `StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` but the resource is
    **actually in UAV state**. **This is a validation error → DEVICE_REMOVED.**

**Step 3 — VFXBillboardIndices struct mismatch (Frame 2 render):**
  - The HLSL `cbuffer VFXBillboardConstants : register(b0)` has 12 fields = 48 bytes:
    8 VS indices + `spriteTextureIdx` + `depthBufferIdx` + `dynamicParamsBufferIdx` + `_vfxPad1`.
  - The CPU-side `VFXBillboardIndices` struct has only 8 fields = 32 bytes. Missing the last
    4 fields (PS indices).
  - `SetRootConstants(kSlotBindlessIndices, data, numDwords=8, 0)` pushes 8 DWORDs = 32 bytes.
    DX12 root constant count must match the root signature declaration. If the PSO root
    signature declared 48 bytes (12 DWORDs) for b0, setting only 8 DWORDs corrupts the
    remaining 4 (PS indices will be garbage → crash if bindless path reads them).

**Summary of crash chain on Frame 2:**
1. Import states don't match actual D3D12 resource states (UAV imported as SRV).
2. Render graph emits barrier with wrong `StateBefore` → D3D12 Debug Layer detects state
   mismatch → DEVICE_REMOVED.
3. Even if barrier passes, root constants are incomplete (32 bytes sent vs 48 bytes expected
   by PSO) → PS reads garbage `spriteTextureIdx`/`depthBufferIdx` → bindless access to invalid
   descriptor → second DEVICE_REMOVED path.

---

## 2. Priority Fix List

Ordered by crash severity. Each fix is independently buildable and testable.

---

### FIX 1 (CRITICAL): Correct frame-2+ import states for write set and indirect args

**Files**: `e:/UzEngine/runtime/engine/renderer/frame/frame_particles.cpp`
**Lines**: 135-155 (writeSetState and indirectState computation)

**Problem**: After frame 1's sim pass writes the write set as UAV and leaves indirect args as
UAV (no longer calling `EmitIndirectArgsBarriers`), frame 2 imports those resources with wrong
states (`SRV` and `IndirectArgument`), causing barrier validation errors.

**Root cause**: `writeSetState = SRV` assumes the write set was last read by the render pass in
frame N-1. But the write set is the ping-pong partner that the RENDER pass read as SRV — that is
the **read set** after the swap, not the write set. After the swap:

- `WriteSet()` = what the previous frame's render pass read as SRV → correct, this WAS left in
  SRV by the previous render pass. **The existing logic is actually correct for the write set.**
- `indirectDrawArgs` is left in UAV by the sim pass (since `EmitIndirectArgsBarriers` was
  removed). Import must be `UAV`, not `IndirectArgument`.

**Specific fix**: Change `indirectState` for frames after the first cycle:

```cpp
// WRONG (current):
const RHIResourceState indirectState = firstCycle
    ? RHIResourceState::UnorderedAccess
    : RHIResourceState::IndirectArgument;  // ← WRONG: sim no longer transitions to IndirectArgument

// CORRECT (after RC6 fix removed EmitIndirectArgsBarriers):
const RHIResourceState indirectState = RHIResourceState::UnorderedAccess;
// Always UAV — the sim pass leaves indirect args in UAV; the render graph
// will transition to IndirectArgument when VFX_ParticleRender declares
// pb.Read(rgIndirectDrawArgs, IndirectArgument).
```

**Risk**: Low. Aligns import state with actual D3D12 state.

**Verification**: GPU validation (`--gpu-validation`) should show zero state mismatch errors
on frame 2.

---

### FIX 2 (CRITICAL): Extend VFXBillboardIndices to match full 48-byte HLSL b0 cbuffer

**Files**:
- `e:/UzEngine/runtime/engine/renderer/frame/frame_particles.cpp`, lines 580-608 (struct definition + fill)
- `e:/UzEngine/runtime/engine/renderer/passes/vfx/particle_render_pass.h` (EmitterRenderData.indicesCB comment)

**Problem**: HLSL `cbuffer VFXBillboardConstants : register(b0)` declares 12 uint fields = 48 bytes.
CPU-side `VFXBillboardIndices` struct has only 8 fields = 32 bytes. The root signature's b0
root-constants slot was created to hold 12 DWORDs. Passing 8 DWORDs sets only the first 8;
the remaining 4 DWORDs (spriteTextureIdx, depthBufferIdx, dynamicParamsBufferIdx, _vfxPad1)
are left at whatever was in the root signature scratch space — garbage values. PS reads
`spriteTextureIdx` as garbage → `ResourceDescriptorHeap[garbage]` → device removed.

**Specific fix** in `frame_particles.cpp` at the `VFXBillboardIndices` struct (line ~580):

```cpp
// WRONG (current — 8 fields, 32 bytes):
struct VFXBillboardIndices {
    uint32_t positionBufferIdx;
    uint32_t sizeBufferIdx;
    uint32_t colorBufferIdx;
    uint32_t rotationBufferIdx;
    uint32_t sortedIndicesSrvIndex;
    uint32_t velocityBufferIdx;
    uint32_t agesBufferIdx;
    uint32_t lifespansBufferIdx;
};

// CORRECT (12 fields, 48 bytes — matches VFXBillboardConstants in particle_billboard_vs.hlsl):
struct VFXBillboardIndices {
    // VS indices (bytes 0-31)
    uint32_t positionBufferIdx;        // b0 DWORD 0
    uint32_t sizeBufferIdx;            // b0 DWORD 1
    uint32_t colorBufferIdx;           // b0 DWORD 2
    uint32_t rotationBufferIdx;        // b0 DWORD 3
    uint32_t sortedIndicesSrvIndex;    // b0 DWORD 4
    uint32_t velocityBufferIdx;        // b0 DWORD 5
    uint32_t agesBufferIdx;            // b0 DWORD 6
    uint32_t lifespansBufferIdx;       // b0 DWORD 7
    // PS indices (bytes 32-47)
    uint32_t spriteTextureIdx;         // b0 DWORD 8 — MISSING, must be UINT32_MAX if no texture
    uint32_t depthBufferIdx;           // b0 DWORD 9 — scene depth SRV
    uint32_t dynamicParamsBufferIdx;   // b0 DWORD 10 — dynamic material params
    uint32_t _vfxPad1;                 // b0 DWORD 11 — padding to 48 bytes
};
```

Also update the fill code to set the new fields:

```cpp
indicesData.spriteTextureIdx       = UINT32_MAX;  // no sprite texture yet
indicesData.depthBufferIdx         = UINT32_MAX;  // no soft-particle depth yet
indicesData.dynamicParamsBufferIdx = UINT32_MAX;  // no dynamic params
indicesData._vfxPad1               = 0;

// Update SetRootConstants call:
const uint32_t numDwords = sizeof(indicesData) / sizeof(uint32_t); // = 12
cmd->SetRootConstants(kSlotBindlessIndices,
                      reinterpret_cast<const uint32_t*>(emitter.indicesCB.cpuPtr),
                      numDwords, 0);
```

**Risk**: Low. Fix is a struct extension; no logic changes.

---

### FIX 3 (HIGH): Verify cbuffer register alignment in particle_indirect_args_cs.hlsl

**Files**: `e:/UzEngine/runtime/shaders/runtime/vfx/particle_indirect_args_cs.hlsl` (line 89)
**Also**: `e:/UzEngine/runtime/engine/renderer/frame/frame_particles.cpp` (line ~302)

**Problem (RC7)**: The crash audit identified a register mismatch — shader uses `register(b0)` but
CPU binds at slot 1 (`kSlotPerDispatch = 1`). However, after reading the current code:

- `particle_indirect_args_cs.hlsl` line 89: `cbuffer VFXIndirectArgsConstants : register(b0)`
- `DispatchIndirectArgs()` in particle_simulation_pass.cpp line 302: `SetRootCBV(kSlotPerDispatch, ...)`
  where `kSlotPerDispatch = 1`.

The shader declares b0 but the root binding sends to slot 1. DX12 maps root parameter slots
to shader registers by HLSL visibility and the root signature layout. If the root signature's
slot 1 is declared as `CBV(b0)` (which is the typical global root signature where slot 1 = b1),
then the shader register `b0` sees root parameter slot 0, not slot 1. This depends entirely on
the root signature declaration.

**Required action**: Read the global compute root signature declaration (likely in
`pipeline_library.cpp` or `rhi_device_dx12.cpp`) and confirm whether slot 1 is `b0` or `b1`.

If slot 1 = `b1`, then:
- Fix the shader: change `register(b0)` to `register(b1)`.
- OR fix the dispatch: change `kSlotPerDispatch` to 0 (if slot 0 = CBV b0).

The safest fix without reading the root sig is to match the VFXSpawnCB and VFXUpdateCB pattern:
both use `register(b1)` and both bind at `kSlotPerDispatch = 1`. Change the indirect args
shader to `register(b1)` for consistency.

**Specific fix in particle_indirect_args_cs.hlsl**:

```hlsl
// WRONG (current):
cbuffer VFXIndirectArgsConstants : register(b0) {

// CORRECT (matches kSlotPerDispatch = 1 binding):
cbuffer VFXIndirectArgsConstants : register(b1) {
```

**Risk**: Medium. Depends on root signature slot mapping. Must verify before applying.

---

### FIX 4 (HIGH): Ensure DispatchSpawn dispatch count is non-zero

**Files**: `e:/UzEngine/runtime/engine/renderer/passes/vfx/particle_simulation_pass.cpp`, line 229

**Problem**: `DispatchSpawn` early-returns if `emitter.spawnCount == 0`. On frame 2, if the
CPU-side `spawnCount` is 0 (spawn rate = 0 on that frame), spawn is skipped. This is correct.

However, `DispatchUpdate` dispatches `max(aliveCount + spawnCount, 1)` groups. On frame 2,
`emitter.aliveCount = cpuAliveCount` which reads the **previous frame's CPU-estimated alive
count**. If `cpuAliveCount` was never updated by readback (the readback path may be absent or
1-frame delayed), this could be 0, causing `Dispatch(1, 1, 1)` — which is fine.

The real issue: `updateCBData.aliveCount = emitter->cpuAliveCount` at frame 2 is 0 (no readback
yet). The update shader runs 1 group = 64 threads, but all threads check `threadID < aliveCount`
which is 0 — no work done. This is not a crash, but means particles never move. Investigate
whether the readback pipeline is correctly populating `cpuAliveCount` with a 1-frame delay.

**This is not a crash fix but a correctness fix for the update dispatch count.**

**Specific fix**: Use the GPU indirect dispatch args buffer (already generated by the indirect
args CS) to drive the update dispatch via `DispatchIndirect`, rather than a CPU-estimated count:

```cpp
// Instead of:
const uint32_t groups = (totalCount > 0) ? DivRoundUp(totalCount, kParticleThreadGroupSize) : 1;
cmd->Dispatch(groups, 1, 1);

// Use the GPU-generated dispatch args from the previous frame:
// cmd->DispatchIndirect(emitter.buffer->indirectDispatchArgs, 0);
// This requires the buffer to be in IndirectArgument state before this call.
```

**Risk**: Medium. Requires ordering change. Defer until after FIX 1-3 stop the crash.

---

### FIX 5 (MEDIUM): Guard hasCompletedCycle behind successful dispatch check

**Files**: `e:/UzEngine/runtime/engine/renderer/frame/frame_particles.cpp`, line ~848

**Problem (RC10)**: `hasCompletedCycle` is set to true even if all emitters were skipped
(e.g., all had `cpuAliveCount == 0` or pipeline was invalid). On the next frame, the import
logic uses `firstCycle = false` and applies wrong initial states.

**Specific fix**: Only set `hasCompletedCycle = true` when at least one emitter successfully
ran both sim and render passes:

```cpp
// Current (wrong — always sets flag):
gpuBuf.hasCompletedCycle = true;

// Correct — only set when emitters were actually processed:
if (!simDesc.emitters.empty() && !renderDesc.emitters.empty()) {
    for (auto& data : simDesc.emitters) {
        data.buffer->hasCompletedCycle = true;
    }
}
```

**Risk**: Low.

---

### FIX 6 (MEDIUM): Remove dead TransitionWriteSetToUAV / TransitionIndirectArgsToUAV helpers

**Files**: `e:/UzEngine/runtime/engine/renderer/passes/vfx/particle_simulation_pass.cpp`,
lines 147-213

**Problem (RC11)**: Two static functions are never called from anywhere. They reference state
transitions that the render graph now handles. Their presence is misleading and risks being
called accidentally.

**Specific fix**: Delete `TransitionWriteSetToUAV()` (lines 147-180) and
`TransitionIndirectArgsToUAV()` (lines 182-213) entirely.

**Risk**: None. Dead code removal.

---

### FIX 7 (LOW): Fix VFXSpawnCB field assignment ordering

**Files**: `e:/UzEngine/runtime/engine/renderer/frame/frame_particles.cpp`, lines 265-296

**Problem**: `VFXSpawnCB` is filled with data but some fields are set in wrong order vs the
struct definition. Specifically, `spawnCBData.spawnCount` is assigned at line 266 but the
struct has `spawnCount` at offset 72 (byte 4 of the third float4). This is not a bug since
named field assignment is used, but a style hazard when copy-pasting.

**Action**: Verify the fill matches the struct field order as a maintenance audit. No functional
change needed since named initializers are used.

---

## 3. Architecture Improvements (Post-Crash Fixes)

These are structural improvements identified from the combined analyses. Implement AFTER
the Frame 2 crash is resolved.

### A. Introduce emitter lifecycle state machine

**Current problem**: Emitters use ad-hoc boolean flags (`needsInitialUpload`, `hasCompletedCycle`,
`initialized`) in various combinations. Frame 2 bugs stem from incorrect state tracking.

**Proposed**: Replace flags with an explicit `EmitterGPUState` enum:

```cpp
enum class EmitterGPUState : uint8_t {
    Uninitialized = 0,  // buffer not yet created
    PendingUpload,      // buffer created, initial state upload pending (frame N)
    FirstFrame,         // initial upload done, sim/render not yet run (frame N+1)
    Active,             // steady-state (frame N+2+)
    Retiring,           // being destroyed, final flush pending
};
```

Frame-particle import logic branches on this enum instead of computing `firstCycle` from
`!hasCompletedCycle`. Eliminates the import state ambiguity.

**Files**: `gpu_particle_buffer.h`, `frame_particles.cpp`

---

### B. Move VFXBillboardIndices into vfx_cb_structs.h

**Current problem**: `VFXBillboardIndices` is defined as a local struct inside
`RegisterParticlePasses()` in `frame_particles.cpp`. The HLSL shader defines the same layout
in `particle_billboard_vs.hlsl`. These two definitions will drift apart again.

**Proposed**: Add `VFXBillboardIndicesCB` to `vfx_cb_structs.h` with a `static_assert` on size,
the same way `VFXSpawnCB`, `VFXUpdateCB`, `VFXIndirectArgsCB`, and `VFXBillboardCB` are defined:

```cpp
// In vfx_cb_structs.h
struct VFXBillboardIndicesCB {
    uint32_t positionBufferIdx;
    uint32_t sizeBufferIdx;
    uint32_t colorBufferIdx;
    uint32_t rotationBufferIdx;
    uint32_t sortedIndicesSrvIndex;
    uint32_t velocityBufferIdx;
    uint32_t agesBufferIdx;
    uint32_t lifespansBufferIdx;
    uint32_t spriteTextureIdx;
    uint32_t depthBufferIdx;
    uint32_t dynamicParamsBufferIdx;
    uint32_t _vfxPad1;
};
static_assert(sizeof(VFXBillboardIndicesCB) == 48,
              "VFXBillboardIndicesCB must be 48 bytes to match VFXBillboardConstants in HLSL");
```

---

### C. GPU-driven dispatch for update and render (Niagara pattern)

**Current problem**: `DispatchUpdate` uses `cpuAliveCount` — a CPU-estimated value that is
1-frame stale and 0 on frame 2. This causes the update shader to either over-dispatch (too many
groups) or under-dispatch (skips live particles).

**Niagara pattern**: The indirect args CS writes both `DrawIndexedIndirectArgs` AND
`DispatchIndirectArgs`. Use `cmd->DispatchIndirect(indirectDispatchArgs)` for the update pass
instead of a CPU-computed group count.

**Dependency**: The indirect args CS must run BEFORE the update dispatch (flip the order in
`AddParticleSimulationPass`):

```
Current order:  Spawn → Update → IndirectArgs
Correct order:  IndirectArgs (from prev frame) → Spawn → Update → IndirectArgs (for next frame)
```

This requires the indirectDispatchArgs to carry the PREVIOUS frame's alive count — which is
exactly what the ping-pong buffer provides.

---

### D. Soft particle depth SRV binding

**Current problem**: `depthBufferIdx = UINT32_MAX` is passed as the depth buffer SRV index
(set in FIX 2). The PS likely guards on `depthBufferIdx == UINT32_MAX` to skip soft fading,
but if not, reading from heap index `0xFFFFFFFF` crashes.

**Action**: After FIX 2, wire the scene depth texture SRV into the indices CB:

```cpp
indicesData.depthBufferIdx = m_device->GetTextureSRVIndex(desc.sceneDepthTexture);
```

`ParticleRenderPassDesc` already has `sceneDepthTexture` field. Pass it through to the indices
fill code.

---

### E. Fix staging buffer leak in UploadInitialState

**File**: `e:/UzEngine/runtime/engine/vfx/core/gpu_particle_buffer.cpp`, lines 307-391

**Current problem**: The code explicitly notes it "leaks the staging buffer" with a comment:
`// Staging will be destroyed after the command list executes. For simplicity... we leak the
staging buffer.`

This is a confirmed resource leak — 3 upload buffers (free list, free list head, alive count)
per emitter creation are never freed. For scenes with many emitters being dynamically created/
destroyed, this accumulates until VRAM exhaustion.

**Fix**: Register staging buffers with a frame-deferred deletion queue:

```cpp
// After each CopyBuffer call, instead of leaking:
m_device->DeferredDestroyBuffer(staging);
// OR queue to a deletion list that's processed after GPU fence signals
```

**Risk**: Requires `IRHIDevice` to expose a deferred-destroy API. If not available, add a
`std::vector<RHIBufferHandle> stagingToFree` list to `GPUParticleBuffer` and call
`Destroy()` on them after a fence wait.

---

## 4. Comparison Table: UzEngine vs Niagara

| Subsystem | Niagara (UE5) | UzEngine (current) | Gap / Change Needed |
|-----------|---------------|-------------------|---------------------|
| **Buffer management** | Ping-pong SoA, enum-based state machine per emitter | Ping-pong SoA with ad-hoc bool flags | Replace flags with `EmitterGPUState` enum (Architecture A) |
| **Initial state** | Inline upload in emitter init pass, deferred destroy via frame pool | Manual barriers + staging leak | Fix staging leak (Architecture E); use frame deferred destroy |
| **CB layout verification** | HLSL mirror structs with `static_assert` in C++ | Partial — VFXSpawnCB/VFXUpdateCB have static_assert; VFXBillboardIndices is local struct without assert | Move VFXBillboardIndicesCB to vfx_cb_structs.h with static_assert (Architecture B) |
| **Compute dispatch** | Spawn → IndirectArgs → DispatchIndirect (update uses GPU count) | Spawn → Update (CPU count) → IndirectArgs (for render) | Use DispatchIndirect for update; flip order (Architecture C) |
| **Root binding — b0 (indices)** | Root constants, full HLSL-mirrored struct | 8-field struct (missing 4 PS fields); 32 bytes sent vs 48 expected | Extend VFXBillboardIndices to 12 fields / 48 bytes (FIX 2) |
| **Root binding — b1 (per-frame)** | PerFrameCB correctly inherited across passes | Correctly re-bound after SetPipeline | ✅ Already fixed |
| **Root binding — b2 (per-emitter)** | Per-emitter CBV via root CBV slot | Correctly bound via kSlotPerEmitter=2 | ✅ Already fixed |
| **Indirect args register** | b0 in shader, slot 0 in root sig | b0 in shader, slot 1 in root sig (mismatch) | Fix register to b1 or rebind to slot 0 (FIX 3) |
| **Import states** | Explicit state machine per resource per frame | Frame 2+ imports with wrong states (SRV/IndirectArgument vs actual UAV) | Fix `indirectState` import (FIX 1) |
| **Render pass setup** | Explicit BeginRenderPass / EndRenderPass | ✅ BeginRenderPass added to particle_render_pass.cpp | ✅ Already fixed |
| **Draw call** | DrawIndexedIndirect (GPU-driven) | `cmd->Draw(cpuAliveCount * 6)` — CPU-driven fallback | Acceptable short-term; move to DrawIndirect after FIX 1-3 |
| **Descriptor table slot 5** | Not used in bindless mode | `SetDescriptorTable(5, ...)` calls in trail/mesh/decal/distortion passes | Remove all slot-5 descriptor table calls (RC2) |
| **GPU sort** | Radix sort, 2-pass | Bitonic sort exists in particle_sort_cs.hlsl | ✅ Sort infrastructure exists; validate it runs correctly |
| **Staging buffer lifetime** | Frame pool with deferred destroy | Manual leak with comment explaining the leak | Fix via deferred destroy queue (Architecture E) |
| **Alive count readback** | 1-frame async readback via GPU fence | `cpuAliveCount` — origin unclear, may never be populated | Wire readback properly or always use GPU indirect dispatch |

---

## 5. Implementation Sequence

Execute in this exact order. Each fix is independently buildable.

### Phase 1: Stop Frame 2 crash (2-4 hours)

1. **FIX 1** — Change `indirectState` from `IndirectArgument` to `UnorderedAccess` for non-first
   cycles (`frame_particles.cpp:153`).
2. **FIX 2** — Extend `VFXBillboardIndices` to 12 fields / 48 bytes, set new fields to
   `UINT32_MAX`, update `SetRootConstants` numDwords to 12 (`frame_particles.cpp:580-608`).
3. **FIX 3** — Confirm root sig slot mapping, then change indirect args CS to `register(b1)`
   (`particle_indirect_args_cs.hlsl:89`).

**Test**: Build + run VFX demo. GPU validation (`--gpu-validation`) should produce zero errors.
Particles should appear on screen (even if not moving yet).

### Phase 2: Correctness fixes (1-2 hours)

4. **FIX 5** — Guard `hasCompletedCycle` flag (`frame_particles.cpp:848`).
5. **FIX 6** — Delete dead `TransitionWriteSetToUAV` and `TransitionIndirectArgsToUAV` functions
   (`particle_simulation_pass.cpp:147-213`).

**Test**: Run VFX demo for 60+ frames. No stale state on hot-add/remove of emitters.

### Phase 3: Architecture improvements (4-8 hours)

6. **Architecture B** — Move `VFXBillboardIndices` to `vfx_cb_structs.h` with `static_assert`.
7. **Architecture D** — Wire `sceneDepthTexture` SRV index into indices CB.
8. **Architecture E** — Fix staging buffer leak.
9. **Architecture A** — Introduce `EmitterGPUState` enum (larger refactor, do last).

### Phase 4: Feature work (after Phase 1-3 complete)

- Implement GPU Radix Sort (replaces bitonic sort for better perf on high particle counts).
- Wire `DispatchIndirect` for update dispatch (Architecture C).
- Remove all `SetDescriptorTable(5, ...)` calls from trail/mesh/decal/distortion passes (RC2).
- Integrate soft particle depth fade (Architecture D).

---

## 6. Validation Checklist

Before reporting Phase 1 complete:

- [ ] `--gpu-validation` runs for 300 frames with zero D3D12 errors
- [ ] Particles are visible on screen with correct billboard orientation
- [ ] Multiple emitters work simultaneously
- [ ] Adding/removing emitters at runtime does not crash
- [ ] `--pass-dump` shows VFX_ParticleRender pass output as non-black
- [ ] `--diag` shows no FAIL_NAN or FAIL_INF on any VFX render target
- [ ] `--pixel-debug` at a particle position shows non-zero alpha in HDR buffer

Before reporting Phase 2 complete:

- [ ] Run demo for 10+ minutes without crash
- [ ] Add 5 emitters, remove 2, add 3 more — no crash
- [ ] `hasCompletedCycle` only set when emitters actually processed

---

## 7. Files Modified Summary

| Fix | File | Lines | Change Type |
|-----|------|-------|-------------|
| FIX 1 | `runtime/engine/renderer/frame/frame_particles.cpp` | 153-155 | Change constant |
| FIX 2 | `runtime/engine/renderer/frame/frame_particles.cpp` | 580-608 | Struct extension + fill |
| FIX 3 | `runtime/shaders/runtime/vfx/particle_indirect_args_cs.hlsl` | 89 | Register change |
| FIX 4 | `runtime/engine/renderer/passes/vfx/particle_simulation_pass.cpp` | 280-284 | Dispatch order |
| FIX 5 | `runtime/engine/renderer/frame/frame_particles.cpp` | ~848 | Guard condition |
| FIX 6 | `runtime/engine/renderer/passes/vfx/particle_simulation_pass.cpp` | 147-213 | Delete dead code |
| Arch A | `runtime/engine/vfx/core/gpu_particle_buffer.h`, `frame_particles.cpp` | New | Enum + refactor |
| Arch B | `runtime/engine/vfx/core/vfx_cb_structs.h` | New struct | Move + static_assert |
| Arch C | `runtime/engine/renderer/passes/vfx/particle_simulation_pass.cpp` | Dispatch order | Flip Spawn/Update/IndirectArgs |
| Arch D | `runtime/engine/renderer/frame/frame_particles.cpp` | ~596 | Wire depth SRV |
| Arch E | `runtime/engine/vfx/core/gpu_particle_buffer.cpp` | 307-391 | Fix staging leak |
