<!-- doc-meta
title: VFX Resource State Audit — Complete Structural Analysis
author: Claude Sonnet 4.6 (Explore agent)
date: 2026-04-02
tags: [vfx, audit, dx12, resource-state, root-signature, cb-layout, thread-group]
-->

# VFX Resource State Audit — Complete Structural Analysis

**Date**: 2026-04-02
**Symptom**: `CommandList::Close failed, HRESULT=0x80070057 (E_INVALIDARG)` on Frame 2.
Frame 1 (initial free list upload) succeeds. Frame 2 (first real compute dispatch) crashes.

**Scope**: Exhaustive audit of all resource state transitions, root parameter bindings,
CB layout byte-for-byte comparison, and thread group size mismatches across:
- `frame_particles.cpp` — CB fill + render graph import
- `particle_simulation_pass.cpp` — compute dispatch
- `particle_render_pass.cpp` — billboard draw
- `gpu_particle_buffer.cpp` — buffer creation + upload
- `vfx_cb_structs.h` — CPU CB layouts
- `particle_spawn_cs.hlsl` — spawn shader
- `particle_update_cs.hlsl` — update shader
- `particle_indirect_args_cs.hlsl` — indirect args shader
- `particle_billboard_vs.hlsl` — billboard VS
- `dx12_pso_creation.cpp` — root signature layout
- `dx12_command_list.cpp` — SetPipeline behavior
- `scene_vfx_sync.cpp` — emitter creation flow

---

## SECTION 1: Resource State Tracing (Per Buffer)

### 1.1 Write-Set Attribute Buffers (positions, velocities, ages, lifespans, sizes, colors, rotations)

**Creation** (`gpu_particle_buffer.cpp:47-57`):
```cpp
desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
```
Initial state after creation: **UAV** (DX12 default for Default-heap structured buffers).

**Frame 0 Import** (`frame_particles.cpp:137-143`):
```cpp
const RHIResourceState writeSetState = firstCycle
    ? RHIResourceState::UnorderedAccess   // Frame 0: UAV from Create()
    : RHIResourceState::ShaderResource;   // Frame 1+: SRV left by render pass
```
- `firstCycle = !gpuBuf.hasCompletedCycle` → true on frame 0.
- Frame 0: imported as **UAV** — CORRECT.
- Frame 1+: imported as **SRV** — CORRECT (render pass left the write set of last frame as SRV via render graph transition).

**Sim Pass Declaration** (`particle_simulation_pass.cpp:461-467`):
```cpp
pb.Write(emitter.rgWritePositions, RHIResourceState::UnorderedAccess);
```
On Frame 0: graph sees UAV → UAV (no barrier needed). CORRECT.
On Frame 1+: graph sees SRV → UAV (barrier inserted). CORRECT.

**After Sim Pass**: write-set is left in UAV by the simulation CS dispatch.

**Render Pass Declaration** (`particle_render_pass.cpp:156-163`):
```cpp
pb.Read(emitter.rgReadPositions, RHIResourceState::ShaderResource);
```
Read-set is the previous frame's write-set (after ping-pong swap).
Read-set was left in UAV by sim. Render graph inserts UAV → SRV barrier. CORRECT.

**VERDICT for write-set/read-set attribute buffers**: No state mismatch. CLEAN.

---

### 1.2 aliveCount Buffer

**Creation** (`gpu_particle_buffer.cpp:148-150`):
```cpp
aliveCount = CreateVFXBuffer(device, sizeof(uint32_t), sizeof(uint32_t), "VFX_AliveCount");
```
Initial state: **UAV**.

**Frame 0 UploadInitialState** (`gpu_particle_buffer.cpp:363-391`):
Transitions: UAV → CopyDst → UAV. Leaves aliveCount in **UAV**.

**Frame 0 Import** (`frame_particles.cpp:146-149`):
```cpp
const RHIResourceState aliveState = firstCycle
    ? RHIResourceState::UnorderedAccess  // Frame 0
    : RHIResourceState::ShaderResource;  // Frame 1+
```
Frame 0: imported as **UAV** — CORRECT (just uploaded to UAV state).
Frame 1+: imported as **SRV** — The render pass declared `pb.Read(rgAliveCount, ShaderResource)`,
so the render graph transitioned it to SRV before render. Import as SRV is CORRECT.

**Sim Pass**: declares `pb.Write(emitter.rgAliveCount, UnorderedAccess)`. Graph inserts SRV → UAV on Frame 1+. CORRECT.

**Render Pass**: declares `pb.Read(emitter.rgAliveCount, ShaderResource)`. Graph inserts UAV → SRV. CORRECT.

**VERDICT for aliveCount**: No state mismatch. CLEAN.

---

### 1.3 freeList, freeListHead, idToIndex

**Creation**: UAV.
**Upload** (`gpu_particle_buffer.cpp:301-363`): freeList and freeListHead transition Common → CopyDst → UAV.
**Import** (`frame_particles.cpp:182-184`): Always imported as `UnorderedAccess`. CORRECT.

**Sim Pass**: declares `pb.Write(rgFreeList/FreeHead/IdToIndex, UnorderedAccess)`. UAV→UAV (no barrier). CORRECT.
Render pass does NOT declare these — they are not read by the render pass. CORRECT.

**VERDICT for freeList/Head/idToIndex**: No state mismatch. CLEAN.

---

### 1.4 indirectDrawArgs and indirectDispatchArgs

**Creation** (`gpu_particle_buffer.cpp:62-72`):
```cpp
desc.usage = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs;
```
Initial state: **UAV**.

**Frame 0 Import** (`frame_particles.cpp:152-155`):
```cpp
const RHIResourceState indirectState = firstCycle
    ? RHIResourceState::UnorderedAccess    // Frame 0
    : RHIResourceState::IndirectArgument;  // Frame 1+
```
Frame 0: imported as **UAV** — CORRECT.
Frame 1+: imported as **IndirectArgument** — this is a PROBLEM (see Bug #1 below).

**Sim Pass**: declares `pb.Write(rgIndirectDrawArgs, UnorderedAccess)`.
Frame 0: UAV → UAV. OK.
Frame 1+: Graph sees IndirectArgument → needs UAV. **The render graph must insert IndirectArgument → UAV barrier.** Whether this transition is valid depends on the render graph's barrier implementation.

**CRITICAL ISSUE**: The indirect args CS dispatches AFTER the simulation pass writes aliveCount.
The shader reads aliveCount as SRV via bindless (`aliveCountSrvIndex`). But aliveCount is
declared in the sim pass as `pb.Write(..., UAV)`. After the sim pass, aliveCount is in UAV.
The indirect args CS reads aliveCount as `StructuredBuffer<uint>` (SRV access) via
`ResourceDescriptorHeap[aliveCountSrvIndex]`. **UAV→SRV transition is missing between
spawn/update dispatch and indirect args dispatch within the same sim pass.**

DX12 does NOT automatically insert barriers between dispatches on the same command list.
The code comment says "UAV→UAV sync between spawn/update/indirect is implicit on DX12"
but that is only true for UAV→UAV hazards — NOT for UAV→SRV hazards.

**Bug #1**: The indirect args CS reads `aliveCount` as SRV (StructuredBuffer via bindless)
after it was written by the update CS as UAV (RWStructuredBuffer). No barrier exists between
the update dispatch and the indirect args dispatch within the sim pass. This is a **read-after-write
hazard** that causes undefined behavior on the GPU.

---

### 1.5 Sort Buffers (sortKeys, sortIndices)

**Import**: Always as `UnorderedAccess`. Sim pass and sort pass both declare Write/UAV. CLEAN.

---

## SECTION 2: Root Signature vs. Shader Register Mapping

### 2.1 Root Signature Layout (dx12_pso_creation.cpp:179-240)

```
Slot 0: 32-bit root constants at b0 (16 x uint32 = 64 bytes max)
Slot 1: Root CBV at b1 (per-frame)
Slot 2: Root CBV at b2 (per-object)
Slot 3: Root CBV at b4 (shadow)
Slot 4: UAV descriptor table at u0..u3
Slot 5: SRV descriptor table at t0+ (SM5 only, not SM6.6 bindless)
```

### 2.2 Spawn Shader Register vs. C++ Dispatch

**HLSL** (`particle_spawn_cs.hlsl:74`):
```hlsl
cbuffer VFXSpawnConstants : register(b1) { ... }
```
Shader reads from **register b1**.

**Root sig slot 1** maps to **b1** (per-frame data). This is a **conflict**:

The spawn shader declares its own constants at `b1`, but the root signature maps slot 1 to
`b1` for per-frame data. In SM6.6 bindless mode with compute root sig set, `SetRootCBV(1, ...)`
binds to slot 1 = b1 for the compute shader. The spawn CS uses b1 for VFXSpawnConstants.

**C++ Dispatch** (`particle_simulation_pass.cpp:236-241`):
```cpp
static constexpr uint32_t kSlotPerDispatch = 1;
cmd->SetRootCBV(kSlotPerDispatch, emitter.spawnCB.bufferHandle, ...);
```
Binds spawn CB to slot 1 = b1. **This matches the shader's `register(b1)`.** CORRECT.

However, this means the "per-frame" data (viewProj etc.) at b1 is NOT bound before compute
dispatches. This is intentional — compute shaders only need their own CBs, not PerFrameCB.

**VERDICT for spawn shader**: Register matches slot binding. CLEAN.

---

### 2.3 Update Shader Register vs. C++ Dispatch

**HLSL** (`particle_update_cs.hlsl:120`):
```hlsl
cbuffer VFXUpdateConstants : register(b1) { ... }
```
Shader reads from **register b1**.

**C++ Dispatch** (`particle_simulation_pass.cpp:271-275`):
```cpp
static constexpr uint32_t kSlotPerDispatch = 1;
cmd->SetRootCBV(kSlotPerDispatch, emitter.updateCB.bufferHandle, ...);
```
Binds update CB to slot 1 = b1. MATCHES. CORRECT.

---

### 2.4 Indirect Args Shader Register vs. C++ Dispatch

**HLSL** (`particle_indirect_args_cs.hlsl:89`):
```hlsl
cbuffer VFXIndirectArgsConstants : register(b0) { ... }
```
Shader reads from **register b0**.

**Root sig slot 0** is `D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS` at b0 (16 x uint32).
This is NOT a CBV — it is root constants.

**C++ Dispatch** (`particle_simulation_pass.cpp:301-307`):
```cpp
static constexpr uint32_t kSlotPerDispatch = 1;
cmd->SetRootCBV(kSlotPerDispatch, emitter.indirectCB.bufferHandle, ...);
```
Binds indirect CB to slot 1 = b1.

**Bug #2 (RC7 — CONFIRMED PRESENT)**: The indirect args shader declares its cbuffer at
`register(b0)`, but the CPU binds the data to slot 1 (which maps to `b1`). The shader
will read from b0 = slot 0, which is a 32-bit root constants slot. The actual indirect
args data is at b1 = slot 1, which the shader never reads.

Additionally, slot 0 is `TYPE_32BIT_CONSTANTS`, so calling `SetRootCBV(0, ...)` would
itself be invalid (wrong parameter type). The code uses slot 1 via `kSlotPerDispatch = 1`.
This means the shader reads stale/garbage data for `aliveCountSrvIndex` and
`indirectArgsUavIndex`, causing `ResourceDescriptorHeap[garbage]` access → device removed.

**Additionally**: Even if the slot were corrected to 0, slot 0 is root constants (not a CBV),
so the fix must use `SetRootConstants(0, ...)` and the shader must use root constants syntax,
OR the shader must be changed to `register(b1)` to match the slot 1 CBV binding.

---

### 2.5 Billboard Vertex Shader Register vs. C++ Dispatch

**HLSL** (`particle_billboard_vs.hlsl:110`):
```hlsl
cbuffer VFXBillboardConstants : register(b0) { ... }  // Root constants (slot 0)
cbuffer PerFrameCB : register(b1) { ... }              // Slot 1
cbuffer VFXBillboardCB : register(b2) { ... }          // Slot 2
```

**C++ Render Pass** (`particle_render_pass.cpp:94-116`):
```cpp
// Root constants at slot 0 (VFXBillboardIndices):
cmd->SetRootConstants(kSlotBindlessIndices, ...);  // kSlotBindlessIndices = 0 ✓

// Per-frame CB at slot 1:
cmd->SetRootCBV(kSlotPerFrame, passDesc.perFrameAlloc...);  // kSlotPerFrame = 1 ✓

// Per-emitter CB at slot 2:
cmd->SetRootCBV(kSlotPerEmitter, emitter.renderCB...);  // kSlotPerEmitter = 2 ✓
```

All slots match registers. CORRECT.

**However**: `SetPipeline` resets root signature bindings
(`dx12_command_list.cpp:317`). The per-frame CB is bound AFTER `SetPipeline`:
```cpp
cmd->SetPipeline(passDesc.billboardPipeline);  // Resets all root params!
// ... then re-binds per-frame CB:
cmd->SetRootCBV(kSlotPerFrame, passDesc.perFrameAlloc...);
```
This is handled correctly — the code explicitly re-binds after SetPipeline.

**VERDICT for billboard VS**: Root binding is correct. CLEAN.

---

## SECTION 3: CB Layout Byte-for-Byte Comparison

### 3.1 VFXSpawnCB (C++) vs. VFXSpawnConstants (HLSL)

**C++ struct** (`vfx_cb_structs.h:18-81`): 192 bytes, static_assert verified.
**HLSL cbuffer** (`particle_spawn_cs.hlsl:74-138`): 192 bytes total.

Byte-by-byte comparison of key fields:

| Byte | C++ Field | HLSL Field | Match |
|------|-----------|------------|-------|
| 0-11 | emitterPosX/Y/Z (3 floats) | emitterPos (float3) | ✓ |
| 12 | spawnRadius | spawnRadius (float) | ✓ |
| 16-27 | spawnVelocityX/Y/Z | spawnVelocity (float3) | ✓ |
| 28 | velocityVariation | velocityVariation | ✓ |
| 32-44 | spreadAngle, minLifetime, maxLifetime, initialSize | same | ✓ |
| 48-63 | initialColorR/G/B/A | initialColor (float4) | ✓ |
| 64 | dt | dt | ✓ |
| 68-76 | frameNumber, spawnCount, maxParticles | same | ✓ |
| 80-116 | freeListSrvIndex..aliveCountUavIndex (10 uints) | same | ✓ |
| 120-140 | spawnShape, spawnOnSurface, shapeParam1-3, _pad0 | same | ✓ |
| 144-171 | initialRotationMin..dynamicParamsUavIndex | same | ✓ |
| 172 | _pad1 | defaultDynParam0 (float) | **MISMATCH** |
| 176-191 | _pad2[4] | defaultDynParam1/2/3, _padSpawnNew1 | **MISMATCH** |

**Bug #3 — CB Layout Mismatch at bytes 172-191 (Spawn CB)**:
- C++ has `_pad1 = 0` at offset 172 and `_pad2[4] = {}` at offsets 176-191.
- HLSL has `defaultDynParam0` at offset 172 and `defaultDynParam1/2/3, _padSpawnNew1` at 176-191.
- The HLSL fields after `dynamicParamsUavIndex` (offset 168) are:
  - 172: `defaultDynParam0` (float) — C++ sends 0 as `_pad1`
  - 176: `defaultDynParam1` (float) — C++ sends 0 as `_pad2[0]`
  - 180: `defaultDynParam2` (float) — C++ sends 0 as `_pad2[1]`
  - 184: `defaultDynParam3` (float) — C++ sends 0 as `_pad2[2]`
  - 188: `_padSpawnNew1` (uint) — C++ sends 0 as `_pad2[3]`

This mismatch does NOT crash (the C++ sends zeros which happen to be valid default values
for dynamic params). BUT it means dynamic params cannot be customized from the CPU.
Not crash-causing but a correctness gap.

**VERDICT for VFXSpawnCB**: Functionally correct (zeros propagate as valid defaults).
Semantically mismatched for `defaultDynParam0-3`. Non-crash.

---

### 3.2 VFXUpdateCB (C++) vs. VFXUpdateConstants (HLSL)

**C++ struct** (`vfx_cb_structs.h:88-198`): 320 bytes, static_assert verified.
**HLSL cbuffer** (`particle_update_cs.hlsl:120+`): declared as 304 bytes in comment but actually 320.

Key differences to check:

The HLSL at offset 60 has `_padSrv0` (uint), while C++ has `rdDynamicParamsSrvIndex` (uint32_t) at
offset 60. Looking at C++ `vfx_cb_structs.h`:

```
C++ offset 60: uint32_t rdRotationsSrvIndex    // VFXUpdateCB
C++ offset 60 alternative check from HLSL layout:
HLSL offset 56: rotationsSrvIndex
HLSL offset 60: _padSrv0
```

C++ `vfx_cb_structs.h` at offset 56-63:
```cpp
uint32_t rdRotationsSrvIndex = 0;    // 56
uint32_t rdDynamicParamsSrvIndex = 0; // 60
```

HLSL at offset 56-63:
```hlsl
uint rotationsSrvIndex;   // 56
uint _padSrv0;            // 60
```

**Bug #4 — rdDynamic field naming divergence in Update CB**:
C++ has `rdDynamicParamsSrvIndex` at offset 60, HLSL has `_padSrv0` — effectively a padding word.
The shader doesn't USE `rdDynamicParamsSrvIndex` at offset 60; it skips it. C++ fills it with a real
SRV index but HLSL ignores the value (treats as padding). The actual dynamic params SRV index used
by the shader comes later at offsets 240+ (`rdDynParamsSrvIdx2`). This is confusing but non-crashing.

**VERDICT for VFXUpdateCB**: Padding/naming divergence at offset 60 but non-crashing.

---

### 3.3 VFXIndirectArgsCB (C++) vs. VFXIndirectArgsConstants (HLSL)

**C++ struct** (`vfx_cb_structs.h:204-210`):
```cpp
struct VFXIndirectArgsCB {
    uint32_t aliveCountSrvIndex = 0;    // 0
    uint32_t indirectArgsUavIndex = 0;  // 4
    uint32_t verticesPerParticle = 6;   // 8
    uint32_t updateGroupSize = 256;     // 12
};
```

**HLSL cbuffer** (`particle_indirect_args_cs.hlsl:89-94`):
```hlsl
cbuffer VFXIndirectArgsConstants : register(b0) {
    uint aliveCountSrvIndex;    // 0
    uint indirectArgsUavIndex;  // 4
    uint _pad0;                 // 8
    uint _pad1;                 // 12
};
```

| Byte | C++ Field | HLSL Field | Match |
|------|-----------|------------|-------|
| 0 | aliveCountSrvIndex | aliveCountSrvIndex | ✓ |
| 4 | indirectArgsUavIndex | indirectArgsUavIndex | ✓ |
| 8 | verticesPerParticle = 6 | _pad0 (ignored) | **MISMATCH** |
| 12 | updateGroupSize = 256 | _pad1 (ignored) | **MISMATCH** |

**Bug #5 — VFXIndirectArgsCB has extra fields the HLSL ignores**:
C++ sends `verticesPerParticle=6` and `updateGroupSize=256` at offsets 8 and 12,
but the HLSL treats these as padding and uses the compile-time constants
`VERTICES_PER_PARTICLE=6` and `UPDATE_GROUP_SIZE=256` instead. The CB layout
is byte-compatible in size (both 16 bytes), and the used fields (offset 0 and 4) match.
This is non-crashing — the HLSL simply ignores bytes 8-15 which contain valid defaults.

However, the shader hardcodes these constants while the C++ sends configurable values.
If `VERTICES_PER_PARTICLE` or `UPDATE_GROUP_SIZE` need to change, only one side gets updated.

**VERDICT for VFXIndirectArgsCB**: Layout is compatible (same field at offsets 0 and 4).
Extra C++ fields silently discarded by shader. Non-crashing but fragile.

---

### 3.4 VFXBillboardCB (C++) vs. VFXBillboardCB (HLSL)

**C++ struct** (`particle_render_pass.h:66-126`): 304 bytes, static_assert verified.
**HLSL cbuffer** (`particle_billboard_vs.hlsl:173+`): Same name, same register(b2).

Quick spot-check of critical fields:

| Byte | C++ | HLSL | Match |
|------|-----|------|-------|
| 0-63 | invView[16] (float[16]) | invView (row_major float4x4) | ✓ |
| 64-75 | emitterPositionX/Y/Z | emitterPosition (float3) | ✓ |
| 76 | globalSizeMultiplier | globalSizeMultiplier | ✓ |
| 80-95 | globalColorTintR/G/B/A | globalColorTint (float4) | ✓ |
| 96-159 | prevViewProj[16] | prevViewProj (row_major float4x4) | ✓ |
| 160 | softFadeDistance | softFadeDistance | ✓ |

**VERDICT for VFXBillboardCB**: Layout matches. CLEAN.

---

## SECTION 4: Thread Group Size Mismatches

### 4.1 kParticleThreadGroupSize vs. Shader numthreads

**C++ constant** (`particle_simulation_pass.cpp:29`):
```cpp
static constexpr uint32_t kParticleThreadGroupSize = 64;
```
Used to calculate dispatch groups: `DivRoundUp(emitter.spawnCount, kParticleThreadGroupSize)`.

**Spawn shader** (`particle_spawn_cs.hlsl:169`):
```hlsl
static const uint SPAWN_GROUP_SIZE = 256;
[numthreads(SPAWN_GROUP_SIZE, 1, 1)]
```
Shader thread group size: **256 threads per group**.

**Update shader** (`particle_update_cs.hlsl:109`):
```hlsl
static const uint UPDATE_GROUP_SIZE = 256;
[numthreads(UPDATE_GROUP_SIZE, 1, 1)]
```
Shader thread group size: **256 threads per group**.

**Indirect args shader** (`particle_indirect_args_cs.hlsl:111`):
```hlsl
[numthreads(1, 1, 1)]
```
Single thread. Always dispatched as `(1, 1, 1)`. CORRECT.

**Bug #6 — CRITICAL: Thread Group Size Mismatch (spawn and update)**:

The C++ dispatches `ceil(spawnCount / 64)` groups, but the shader has `[numthreads(256, 1, 1)]`.

This means for a spawnCount of 256:
- C++ computes: `ceil(256 / 64) = 4` groups
- GPU invokes: `4 × 256 = 1024 threads`
- Shader reads: `spawnIdx = dtid.x` up to 1023
- Spawn count check: `if (spawnIdx >= spawnCount) return` — threads 256-1023 return early ✓

Wait — this doesn't crash (excess threads early-return), but it is WASTEFUL: spawns with 4×
more threads than needed. For spawnCount=10: dispatches `ceil(10/64)=1` group, GPU runs 256 threads,
255 exit early. Functionally correct but wastes GPU time.

For the UPDATE shader:
```cpp
const uint32_t groups = (totalCount > 0)
    ? DivRoundUp(totalCount, kParticleThreadGroupSize)  // divides by 64
    : 1;
cmd->Dispatch(groups, 1, 1);
```
Dispatches `ceil(aliveCount / 64)` groups for a shader with 256 threads/group.
For aliveCount=100: dispatches 2 groups, runs 512 threads, all threads with idx > 100
must early-exit based on `aliveCount` check (if the shader checks it).

**This is not E_INVALIDARG** — DX12 accepts any (groups × threads) combination.
But if the update shader doesn't bounds-check properly, threads 100-511 may read/write
out of bounds in the particle buffers → device removed (memory access violation on GPU).

The indirect args CS constant `UPDATE_GROUP_SIZE = 256` is used to compute dispatch groups
for next frame: `(aliveCount + 255) / 256`. This produces the correct number of groups for
a 256-threads/group shader. BUT the C++ uses 64 for the CURRENT dispatch of update,
creating a 4× discrepancy in threads-per-group vs. items-per-group.

**Net effect of Bug #6**:
- Spawn: 4× thread over-dispatch (non-crashing, wasteful, possible free-list corruption if bounds check is weak)
- Update: 4× thread over-dispatch (non-crashing for alive particles within bounds, but threads may process beyond aliveCount)
- The free-list atomic pop in spawn (`InterlockedAdd(freeListHead[0], -1, prevHead)`) is called by ALL threads ≤ 1023 that pass the `spawnIdx >= spawnCount` check — but the check IS present so excess threads DO return. SAFE.

**VERDICT for thread group size**: Functionally non-crashing (bounds checks exist), but
wasteful (4× over-dispatch). The C++ `kParticleThreadGroupSize = 64` should be **256** to match
the shader's `[numthreads(256, 1, 1)]`. Mismatched comments in the code indicate this was an oversight.

---

## SECTION 5: hasCompletedCycle Flag and State Tracking

### 5.1 Where hasCompletedCycle is set

Searching the codebase for `hasCompletedCycle`:

In `frame_particles.cpp`, the flag controls `firstCycle` which determines the import states:
```cpp
const bool firstCycle = !gpuBuf.hasCompletedCycle;
```
It is used to decide:
- `writeSetState` = UAV (first) vs. SRV (subsequent)
- `aliveState` = UAV (first) vs. SRV (subsequent)
- `indirectState` = UAV (first) vs. IndirectArgument (subsequent)

If `hasCompletedCycle` is set too early (before the render pass actually leaves the buffers
in the expected states), Frame 2 would import the write set as SRV but the buffers would
still be in UAV state → barrier validation error → E_INVALIDARG on CommandList::Close.

The previous audit (vfx-pipeline-crash-audit.md RC10) noted:
> `frame_particles.cpp:848` sets the flag even if all emitters were skipped.

This is **Bug #7**: If all emitters are skipped (e.g., billboard pipeline not valid), the render
pass does not run and the read-set buffers remain in UAV state. But `hasCompletedCycle` gets
set, so Frame 2 imports those buffers as SRV. This mismatches actual GPU state → invalid transition.

Looking at the code flow: the render pass is skipped if `!billboardPipeline.IsValid()` (line ~504).
In that case the function returns early. The `hasCompletedCycle` flag must not be set unless
the render pass actually executed and left buffers in the expected states.

---

## SECTION 6: Render Graph Import State for indirectDrawArgs

### 6.1 Frame 1+ Import as IndirectArgument

```cpp
const RHIResourceState indirectState = firstCycle
    ? RHIResourceState::UnorderedAccess
    : RHIResourceState::IndirectArgument;
ids.indirectDrawArgs = rgBuilder.ImportBuffer("VFX_IndDraw", gpuBuf.indirectDrawArgs, indirectState);
```

Then the sim pass declares:
```cpp
pb.Write(emitter.rgIndirectDrawArgs, RHIResourceState::UnorderedAccess);
```

The render graph must insert `IndirectArgument → UnorderedAccess` for Frame 1+.
This transition is valid in DX12 (D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT → D3D12_RESOURCE_STATE_UNORDERED_ACCESS).

Then after sim, the render pass declares:
```cpp
pb.Read(emitter.rgIndirectDrawArgs, RHIResourceState::IndirectArgument);
```
Graph inserts `UnorderedAccess → IndirectArgument`. Valid.

**Bug #1 (Revisited)**: Within the sim pass execution callback, three dispatches happen:
1. DispatchSpawn — writes aliveCount via UAV
2. DispatchUpdate — reads aliveCount via UAV, writes to aliveCount via UAV (decrements dead particles)
3. DispatchIndirectArgs — reads aliveCount via **SRV** (bindless `StructuredBuffer<uint>`)

The sim pass declared `pb.Write(rgAliveCount, UAV)`. Between dispatch #2 and #3, no barrier
exists. The update CS leaves aliveCount in UAV state. The indirect args CS reads it as SRV.

**DX12 spec**: When a resource is accessed via UAV in one dispatch and then accessed as SRV
in the next dispatch on the same command list WITHOUT a barrier, the behavior is undefined.
On DX12/D3D12, this specifically requires a `D3D12_RESOURCE_BARRIER` of type UAV (UAV→UAV
same-state barrier) OR a SRV transition before the SRV access.

This is a **real crash-causing bug**. The missing UAV→SRV barrier for aliveCount between
the update dispatch and the indirect args dispatch is **Bug #8** (distinct from RC7's CB mismatch).

---

## SECTION 7: The Draw Call — Draw vs. DrawIndexedIndirect

### 7.1 Current Implementation

In `particle_render_pass.cpp:113-116`:
```cpp
const uint32_t vertexCount = emitter.cpuAliveCount * 6;
if (vertexCount > 0) {
    cmd->Draw(vertexCount, 1, 0, 0);
}
```

The pass uses a CPU-side alive count via `cmd->Draw()` — NOT `DrawIndexedIndirect`.
This is a clean non-indirect draw. No index buffer needed, no indirect args buffer needed.

The previous audit (RC4) mentioned `DrawIndexedIndirect without index buffer` — that bug
appears to have been FIXED in the current code. The current implementation uses `cmd->Draw`
(non-indexed, non-indirect). CLEAN.

The indirect args buffer is still populated by the CS (for DispatchIndirect of next frame's update),
but the actual draw call is CPU-driven. This is correct and safe.

---

## SECTION 8: SetPipeline Root Signature Reset Impact

### 8.1 SetPipeline Behavior

`dx12_command_list.cpp:296-331`: `SetPipeline` calls:
```cpp
m_cmdList->SetPipelineState(dx12Pipeline->pso);
// Then re-sets root signature:
if (m_isComputeBound) {
    m_cmdList->SetComputeRootSignature(rootSig);
} else {
    m_cmdList->SetGraphicsRootSignature(rootSig);
}
```

This resets all root parameters. Any bindings set before `SetPipeline` are cleared.

In the sim pass, three `SetPipeline` calls happen (spawn, update, indirect args). After each:
- Previous `SetRootCBV(kSlotPerDispatch, ...)` is cleared
- New `SetRootCBV(kSlotPerDispatch, ...)` sets the next CB

This is correctly handled — each dispatch immediately calls `SetRootCBV` after `SetPipeline`.

In the render pass:
- `SetPipeline(billboardPipeline)` → resets all
- `SetRootCBV(kSlotPerFrame, perFrameAlloc)` → re-binds per-frame
- Per-emitter loop → `SetRootConstants(0, ...)` + `SetRootCBV(2, ...)` per emitter

CLEAN.

---

## SECTION 9: Emitter Creation and needsInitialUpload Flow

### 9.1 Scene VFX Sync Flow

`scene_vfx_sync.cpp:80-102`:
- New emitters: `emitter->needsInitialUpload = true` set immediately
- `VFXSystem::RegisterEmitter(emitter.get())` called → emitter enters the active list

`frame_particles.cpp:240-251`:
- Emitters with `needsInitialUpload = true` get `VFX_InitFreeList` pass added
- These emitters are EXCLUDED from import and sim/render passes this frame

On Frame 2 (after Frame 1's InitFreeList):
- `needsInitialUpload` is set to false inside the pass callback: `em->needsInitialUpload = false`
- But this callback runs during COMMAND LIST EXECUTION, not during REGISTRATION

**Bug #9**: `needsInitialUpload = false` is set inside the render graph pass execution callback.
Registration of the render graph (Frame 2) happens BEFORE execution of Frame 1's command list.
If the engine records Frame 2's render graph pass registrations WHILE Frame 1 is still executing
(pipeline is double-buffered), Frame 2 would see `needsInitialUpload = true` for emitters that
Frame 1 is currently initializing.

Whether this is actually a problem depends on the engine's frame pipelining. If registration
and execution happen sequentially (not pipelined), this is safe. If the engine submits command
lists and immediately starts building the next frame's graph, this is a race condition.

The comment in the code says "The upload happens in RegisterParticlePasses when the render
command list is available (avoids creating a temp cmd list here which can deadlock with
the fence on the direct queue)." This suggests sequential frame execution, making Bug #9
a theoretical concern rather than a confirmed bug.

---

## SUMMARY TABLE: All Bugs Found

| ID | Severity | Location | Description | Crash-Causing |
|----|----------|----------|-------------|---------------|
| Bug #1 | CRITICAL | `particle_simulation_pass.cpp` | No UAV→SRV barrier for `aliveCount` between update dispatch and indirect args dispatch within sim pass | YES (undefined behavior, likely device removed) |
| Bug #2 | CRITICAL | `particle_indirect_args_cs.hlsl:89` + `particle_simulation_pass.cpp:302` | Shader declares cbuffer at `register(b0)`, CPU binds to slot 1 (`b1`). Shader reads garbage descriptor indices. | YES (ResourceDescriptorHeap[garbage] → device removed) |
| Bug #3 | LOW | `vfx_cb_structs.h:77` + `particle_spawn_cs.hlsl:172-184` | `_pad1/_pad2` in C++ vs `defaultDynParam0-3` in HLSL at bytes 172-191. C++ sends zeros, HLSL uses zeros as default. | NO (zeros are valid defaults) |
| Bug #4 | LOW | `vfx_cb_structs.h:109` vs `particle_update_cs.hlsl:143` | `rdDynamicParamsSrvIndex` at offset 60 in C++ vs `_padSrv0` in HLSL. HLSL ignores this field. | NO |
| Bug #5 | LOW | `vfx_cb_structs.h:207-209` vs `particle_indirect_args_cs.hlsl:91-93` | `verticesPerParticle` and `updateGroupSize` in C++ vs `_pad0/_pad1` in HLSL. HLSL uses compile-time constants. | NO |
| Bug #6 | MEDIUM | `particle_simulation_pass.cpp:29` | `kParticleThreadGroupSize = 64` but shaders use `[numthreads(256,1,1)]`. 4× over-dispatch. | NO (bounds checks exist, but wasteful and fragile) |
| Bug #7 | HIGH | `frame_particles.cpp:~848` | `hasCompletedCycle` set even if billboard pipeline is invalid or render pass is skipped. Next frame imports with wrong initial state. | YES (state mismatch → E_INVALIDARG on barrier) |
| Bug #8 | CRITICAL | `particle_simulation_pass.cpp:493-495` | Same as Bug #1 — within the sim pass callback, no barrier between update (UAV writes aliveCount) and indirect args (SRV reads aliveCount). | YES |
| Bug #9 | LOW | `frame_particles.cpp:246-249` | `needsInitialUpload = false` set in execution callback; registration of next frame could race if pipelined. | THEORETICAL |

---

## SECTION 10: Root Cause of Frame 2 E_INVALIDARG

**E_INVALIDARG on CommandList::Close** on Frame 2 is caused by DX12 validation detecting
an illegal resource state transition. The most likely cause (matching the observed symptom):

### Primary Cause: Bug #7 + State Mismatch on First Real Frame

Frame 1 executes `VFX_InitFreeList` (UploadInitialState) which leaves `freeList`,
`freeListHead`, and `aliveCount` in **UAV** state.

If `hasCompletedCycle` is set after Frame 1 (even though the render pass didn't run
because the billboard pipeline wasn't valid yet), Frame 2 imports:
- `writeSetState = ShaderResource` (but buffers are in UAV from Create())
- `aliveState = ShaderResource` (but aliveCount is in UAV from UploadInitialState)

Frame 2's sim pass then declares `pb.Write(rgWritePositions, UAV)`. The render graph
tries to insert `ShaderResource → UAV` barrier. But the actual resource state is UAV.
DX12 debug layer detects `stateBefore` mismatch → **E_INVALIDARG on CommandList::Close**.

### Secondary Cause: Bug #2 (RC7)

Even if Frame 2 records without validation errors, the indirect args CS reads garbage
descriptor indices from b0 (which is root constants, not the CB it expects at b1).
The first `ResourceDescriptorHeap[garbage]` access causes **device removed** → which
manifests as a subsequent **E_INVALIDARG** when the faulted device's command list is closed.

### Why Frame 1 Succeeds

Frame 1 runs `VFX_InitFreeList` only (manual barriers Common→CopyDst→UAV, bypassing
the render graph). No compute dispatches happen. No validation errors. SUCCESS.

---

## SECTION 11: Fix Priority Order

### Fix 1 (CRITICAL — Stops Frame 2 E_INVALIDARG)
**Bug #7**: Guard `hasCompletedCycle = true` behind a flag that confirms:
1. The billboard pipeline was valid AND
2. The render pass execution callback actually ran (set inside the callback, not outside).

```cpp
// WRONG (current): Set unconditionally
if (!simDesc.emitters.empty()) {
    AddParticleSimulationPass(rgBuilder, simDesc);
}
// ... later:
for (auto* emitter : emitters) {
    emitter->buffer.hasCompletedCycle = true;  // WRONG: set even if render skipped
}

// FIX: Set inside the render pass execution callback, not outside
pb.SetExecutionCallback([...](IRHICommandList* cmd, ...) {
    // ... draw ...
    // Mark cycle complete INSIDE the callback so it only sets if render actually ran
    for (auto* emitter : passDesc.emitters) {
        emitter->buffer->hasCompletedCycle = true;
    }
});
```

### Fix 2 (CRITICAL — Stops RC7/Bug #2)
**Bug #2**: Change the indirect args CS cbuffer register to `b1` to match `SetRootCBV(1, ...)`:
```hlsl
// BEFORE:
cbuffer VFXIndirectArgsConstants : register(b0) { ... }
// AFTER:
cbuffer VFXIndirectArgsConstants : register(b1) { ... }
```
No C++ change needed — the CPU already binds to slot 1.

### Fix 3 (CRITICAL — Stops Bug #1/#8)
**Bugs #1 and #8**: Add a UAV→SRV barrier for `aliveCount` between the update dispatch
and the indirect args dispatch within the sim pass execution callback:

```cpp
// In particle_simulation_pass.cpp, inside SetExecutionCallback:
DispatchSpawn(cmd, desc, emitter);
EmitSimulationUAVBarriers(cmd, *emitter.buffer);  // UAV sync after spawn
DispatchUpdate(cmd, desc, emitter);
// NEW: Transition aliveCount from UAV to SRV for indirect args CS to read:
RHIBarrierDesc aliveBarrier{};
aliveBarrier.buffer = emitter.buffer->aliveCount;
aliveBarrier.stateBefore = RHIResourceState::UnorderedAccess;
aliveBarrier.stateAfter  = RHIResourceState::ShaderResource;
cmd->ResourceBarrier(&aliveBarrier, 1);
DispatchIndirectArgs(cmd, desc, emitter);
// NEW: Transition back to UAV so render graph's next declared state is correct:
aliveBarrier.stateBefore = RHIResourceState::ShaderResource;
aliveBarrier.stateAfter  = RHIResourceState::UnorderedAccess;
cmd->ResourceBarrier(&aliveBarrier, 1);
```

Note: The render graph declares aliveCount as `pb.Write(rgAliveCount, UAV)` for the sim pass
and `pb.Read(rgAliveCount, SRV)` for the render pass. The UAV→SRV→UAV round-trip within the
sim pass is an internal detail invisible to the render graph's state tracking, which only cares
about the state when the pass BEGIN (UAV) and END (still UAV after the round-trip back).

### Fix 4 (MEDIUM — Correctness)
**Bug #6**: Fix `kParticleThreadGroupSize` to match shader:
```cpp
// BEFORE:
static constexpr uint32_t kParticleThreadGroupSize = 64;
// AFTER:
static constexpr uint32_t kParticleThreadGroupSize = 256;
```

### Fix 5 (LOW — Clarity)
**Bugs #3, #4, #5**: Align C++ CB struct field names with HLSL fields for future maintainability.
These do not cause crashes but make debugging harder.

---

## SECTION 12: Files to Edit (in dependency order)

1. `runtime/shaders/runtime/vfx/particle_indirect_args_cs.hlsl` — change `register(b0)` → `register(b1)`
2. `runtime/engine/renderer/passes/vfx/particle_simulation_pass.cpp` — add aliveCount UAV→SRV→UAV barriers between update and indirect args dispatches
3. `runtime/engine/renderer/frame/frame_particles.cpp` — fix `hasCompletedCycle` to be set inside render callback only
4. `runtime/engine/renderer/passes/vfx/particle_simulation_pass.cpp` — fix `kParticleThreadGroupSize = 256`

---

## SECTION 13: Verification of Previously Fixed Bugs (from vfx-pipeline-crash-audit.md)

| RC | Status | Evidence |
|----|--------|---------|
| RC1 (CB size mismatch) | **FIXED** | VFXSpawnCB=192B, VFXUpdateCB=320B, VFXIndirectArgsCB=16B all have static_asserts and match HLSL layouts |
| RC2 (SetDescriptorTable(5)) | **FIXED** | Current particle_render_pass.cpp uses `SetRootConstants(0)` and `SetRootCBV(1/2)` — no slot 5 |
| RC3 (Missing BeginRenderPass) | **FIXED** | particle_render_pass.cpp:192 calls `cmd->BeginRenderPass(rpDesc)` |
| RC4 (DrawIndexedIndirect without IB) | **FIXED** | Now uses `cmd->Draw(vertexCount, 1, 0, 0)` — non-indexed |
| RC5 (SetRootCBV on root constants) | **FIXED** | Now uses `cmd->SetRootConstants(kSlotBindlessIndices, ...)` for slot 0 |
| RC6 (Manual barrier conflicts) | **FIXED** | EmitIndirectArgsBarriers removed; render graph handles state |
| RC7 (Register mismatch) | **PARTIALLY FIXED** — C++ changed to slot 1, shader still uses b0. SHADER FIX PENDING |
| RC8 (Null barrier resource) | **FIXED** — code no longer has null-resource barriers |
| RC9 (Per-emitter CB overwrite) | **FIXED** — per-emitter CB at slot 2, per-frame CB at slot 1 |
| RC10 (hasCompletedCycle) | **PARTIALLY FIXED** — still set if render pass skipped |
| RC11 (Dead code) | **PRESENT** but harmless — TransitionWriteSetToUAV/TransitionIndirectArgsToUAV exist but are not called |

---

## Conclusion

**The Frame 2 E_INVALIDARG crash is caused by three compounding bugs:**

1. **Bug #7** (hasCompletedCycle): Frame 2 imports buffers with wrong initial state assumptions because `hasCompletedCycle` was set during Frame 1 even though the render pass may not have completed the expected state transitions. The render graph then generates a barrier with incorrect `stateBefore` → `E_INVALIDARG` on CommandList::Close.

2. **Bug #2** (RC7 — shader register mismatch): The indirect args CS reads garbage descriptor indices from b0 (root constants) instead of b1 (where the CB data actually is). Even if Frame 2 records successfully, execution causes `ResourceDescriptorHeap[0]` access (or similar garbage) → device removed.

3. **Bug #8** (Missing UAV→SRV barrier): `aliveCount` written as UAV by update CS, then read as SRV by indirect args CS within the same pass, without a barrier. On strict DX12 validation, this is detected as a resource hazard → device removed.

All three bugs must be fixed together for stable VFX operation.
