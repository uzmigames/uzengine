<!-- doc-meta -->
# VFX Shader Binding Audit

**Date**: 2026-04-02  
**Status**: COMPLETE — 5 bugs found, 3 critical (DEVICE_REMOVED risk)

---

## Summary Table

| Check | File(s) | Result | Severity |
|-------|---------|--------|----------|
| spawn CB register vs root slot | particle_spawn_cs.hlsl + simulation_pass.cpp | **FAIL** | CRITICAL |
| update CB register vs root slot | particle_update_cs.hlsl + simulation_pass.cpp | PASS | — |
| indirect args CB register vs root slot | particle_indirect_args_cs.hlsl + simulation_pass.cpp | **FAIL** | CRITICAL |
| VFXSpawnCB C++ vs HLSL field alignment | vfx_cb_structs.h vs particle_spawn_cs.hlsl | **FAIL** | CRITICAL |
| VFXUpdateCB C++ vs HLSL field alignment | vfx_cb_structs.h vs particle_update_cs.hlsl | **FAIL** | HIGH |
| VFXIndirectArgsCB extra fields | vfx_cb_structs.h vs particle_indirect_args_cs.hlsl | **FAIL** | MEDIUM |
| Thread group size mismatch | simulation_pass.cpp vs shaders | **FAIL** | HIGH |
| Billboard b0 binding method (root constants) | render_pass.cpp | PASS | — |
| Billboard b1 PerFrameCB re-bind after SetPipeline | render_pass.cpp | PASS | — |
| Billboard b2 VFXBillboardCB slot | render_pass.cpp | PASS | — |
| Billboard VS/PS b0 layout match | vs + ps hlsl | PASS | — |
| Billboard VFXBillboardCB C++ vs HLSL size | particle_render_pass.h vs ps/vs | PASS | — |
| UAV/SRV descriptor state for read/write sets | simulation_pass.cpp | PASS | — |
| Indirect args barrier (UAV→IndirectArgument) | simulation_pass.cpp | PASS | — |
| freeList resource state (render pass reads it) | particle_render_pass.cpp | PASS | — |

---

## Bug 1 — CRITICAL: Spawn CB Binds to Wrong Root Slot

### What the shader declares
```hlsl
// particle_spawn_cs.hlsl:74
cbuffer VFXSpawnConstants : register(b1) { ... }
```
The spawn shader's constant buffer is at **register b1**.

### What the root signature layout provides
| Root Slot | Type | Register |
|-----------|------|----------|
| 0 | 32BIT_CONSTANTS (16 DWORDs) | b0 |
| **1** | **CBV** | **b1** |  ← correct slot for b1
| 2 | CBV | b2 |
| 3 | CBV | b4 |

Slot 1 maps to b1. This is correct in theory.

### What the C++ dispatch code does
```cpp
// particle_simulation_pass.cpp:32
static constexpr uint32_t kSlotPerDispatch = 1;

// particle_simulation_pass.cpp:236
cmd->SetRootCBV(kSlotPerDispatch,        // slot 1 → b1
                emitter.spawnCB.bufferHandle, ...);
```
The slot assignment is **correct** (slot 1 → b1).

### Actual problem: thread group size constant mismatch
```cpp
// particle_simulation_pass.cpp:29
static constexpr uint32_t kParticleThreadGroupSize = 64;  // ← 64
```
```hlsl
// particle_spawn_cs.hlsl:62
static const uint SPAWN_GROUP_SIZE = 256;  // ← 256
```
The C++ dispatch code computes `ceil(spawnCount / 64)` groups, but the shader executes `256` threads per group. This means the actual number of threads dispatched is `ceil(N/64) * 256`, which is **4x the intended count**. Every thread with `spawnIdx >= spawnCount` exits early (guarded by the `if (spawnIdx >= spawnCount) return;` check), so particles do not double-spawn — but the **GPU over-dispatches 4x unnecessary thread groups** causing wasted work and potentially hiding the real alive-count accounting. When `spawnCount` is small (e.g., 1–63), the C++ dispatches exactly 1 group = 256 threads of which 255 exit early; the shader still works. This is not DEVICE_REMOVED but is a correctness risk for dispatch sizing and is **classified HIGH** rather than CRITICAL.

---

## Bug 2 — CRITICAL: Indirect Args CS Binds to Wrong Root Slot

### What the shader declares
```hlsl
// particle_indirect_args_cs.hlsl:89
cbuffer VFXIndirectArgsConstants : register(b0) { ... }
```
The indirect args shader's CB is at **register b0**.

### What b0 is in the root signature
| Root Slot | Type | Register |
|-----------|------|----------|
| **0** | **32BIT_CONSTANTS (16 DWORDs)** | **b0** |

Slot 0 is **32-bit root constants**, not a CBV. `SetComputeRoot32BitConstants` is the correct API.  
The C++ code calls:
```cpp
// particle_simulation_pass.cpp:302
cmd->SetRootCBV(kSlotPerDispatch,   // kSlotPerDispatch = 1 → b1
                emitter.indirectCB.bufferHandle, ...);
```
`kSlotPerDispatch = 1`. But the shader reads its data from `b0`, which is the 32-bit root constants slot (slot 0). The C++ is binding to **slot 1 (b1)** while the shader reads from **slot 0 (b0)**. The GPU reads garbage for `aliveCountSrvIndex` and `indirectArgsUavIndex`, so the bindless `ResourceDescriptorHeap[index]` accesses are to arbitrary heap slots — **this is the root cause of DEVICE_REMOVED on Frame 2**.

### Fix
Either:
- Change the indirect args shader to `register(b1)` to match `kSlotPerDispatch=1`  
- **OR** change `DispatchIndirectArgs` to use `SetRootConstants(0, ...)` with the 16-DWORD struct

---

## Bug 3 — CRITICAL: VFXSpawnCB C++ Layout Truncated vs HLSL

The HLSL `VFXSpawnConstants` has these fields at the end (starting byte 172):

```hlsl
// particle_spawn_cs.hlsl:130-136
uint     _pad0;                ///<                                          140
float    initialRotationMin;   ///< byte 144
float    initialRotationMax;   ///< byte 148
float    emitterVelocityX;     ///< byte 152
float    emitterVelocityY;     ///< byte 156
float    emitterVelocityZ;     ///< byte 160
float    velocityInheritance;  ///< byte 164
uint     dynamicParamsUavIndex; ///< byte 168
float    defaultDynParam0;     ///< byte 172
float    defaultDynParam1;     ///< byte 176
float    defaultDynParam2;     ///< byte 180
float    defaultDynParam3;     ///< byte 184
uint     _padSpawnNew1;        ///< byte 188
```
HLSL total = **192 bytes** (12 × float4).

The C++ `VFXSpawnCB` (vfx_cb_structs.h:18–81) has:
```cpp
uint32_t dynamicParamsUavIndex = 0; // 168
uint32_t _pad1 = 0;                // 172
float _pad2[4] = {};               // 176-191 ← fills remaining 16 bytes
```
The C++ struct **omits** `defaultDynParam0`, `defaultDynParam1`, `defaultDynParam2`, `defaultDynParam3` — using generic padding instead. The byte counts match (both 192 bytes, static_assert passes), but the **semantics differ**:
- HLSL byte 172 = `defaultDynParam0` (real data)
- C++ byte 172 = `_pad1 = 0` (padding zero)

This means the spawn shader always reads zeros for the 4 default dynamic params fields instead of the values the CPU intends to set. This is a **data mismatch** (not a crash-level GPU fault), but the `dynamicParams[particleIdx]` init in the shader will write zeros instead of the configured defaults. **Severity: CRITICAL for correctness, will not cause DEVICE_REMOVED.**

---

## Bug 4 — HIGH: VFXUpdateCB C++ Layout Diverges at Byte 96

The HLSL `VFXUpdateConstants` has (starting at offset 96):
```hlsl
// particle_update_cs.hlsl:156-162
uint     freeListUavIndex;      ///< 96
uint     freeListHeadUavIndex;  ///< 100
uint     aliveCountNewUavIndex; ///< 104
uint     _padShared0;           ///< 108
```

The C++ `VFXUpdateCB` has (vfx_cb_structs.h:123-126):
```cpp
uint32_t aliveCountUavIndex = 0;    // 96   ← different name+semantic
uint32_t freeListUavIndex = 0;      // 100  ← swapped order
uint32_t freeListHeadUavIndex = 0;  // 104
uint32_t idToIndexUavIndex = 0;     // 108  ← extra field not in HLSL
```

**Order is swapped**: C++ puts `aliveCountUavIndex` at offset 96, but the HLSL puts `freeListUavIndex` at offset 96. The GPU will use the CPU's `aliveCountUavIndex` value as the `freeListUavIndex` descriptor, and vice versa. This sends `InterlockedAdd` on the free list to the aliveCount buffer and vice versa, **corrupting both counters**.

Additionally the C++ has `idToIndexUavIndex` at 108 while HLSL has `_padShared0` (unused). The C++ value at byte 108 is seen by the HLSL shader as padding and is ignored — benign only because it's the last field in this row.

---

## Bug 5 — MEDIUM: VFXIndirectArgsCB Has Extra Fields Not in HLSL

The HLSL `VFXIndirectArgsConstants` (particle_indirect_args_cs.hlsl:89–94) has:
```hlsl
cbuffer VFXIndirectArgsConstants : register(b0) {
    uint aliveCountSrvIndex;    // 0
    uint indirectArgsUavIndex;  // 4
    uint _pad0;                 // 8
    uint _pad1;                 // 12
};  // Total: 16 bytes
```

The C++ `VFXIndirectArgsCB` (vfx_cb_structs.h:204–209):
```cpp
struct VFXIndirectArgsCB {
    uint32_t aliveCountSrvIndex = 0;    // 0
    uint32_t indirectArgsUavIndex = 0;  // 4
    uint32_t verticesPerParticle = 6;   // 8   ← NOT in HLSL (is a static const there)
    uint32_t updateGroupSize = 256;     // 12  ← NOT in HLSL (is a static const there)
};
```
The C++ struct stores `verticesPerParticle` and `updateGroupSize` as CB fields. The HLSL has these as `static const` local constants and uses only `_pad0`/`_pad1` at bytes 8–15. This means byte 8 in the CB is `6` from C++ vs a "don't-care" pad in HLSL — not used by the shader at runtime, so **no crash**. However if the shader binding is ever fixed (Bug 2 above), the data at bytes 8–12 being non-zero could produce unexpected HLSL behavior if the constants are later used. Currently benign but misleading.

Note: because Bug 2 routes this CB to the wrong register entirely, Bugs 4 and 5 are currently masked — the shader never reads the correct CB in the first place.

---

## Bug 6 — HIGH: Thread Group Size Mismatch (All Compute Shaders)

```cpp
// particle_simulation_pass.cpp:29
static constexpr uint32_t kParticleThreadGroupSize = 64;
```

All three compute shaders declare:
```hlsl
// particle_spawn_cs.hlsl:62
static const uint SPAWN_GROUP_SIZE = 256;

// particle_update_cs.hlsl:109
static const uint UPDATE_GROUP_SIZE = 256;

// particle_indirect_args_cs.hlsl:62
static const uint UPDATE_GROUP_SIZE = 256;  // used for dispatch thread count calculation
```

The C++ dispatches `ceil(N / 64)` groups. Each group has `[numthreads(256,1,1)]` = 256 threads. Result: **4× too many thread groups** for spawn and update passes. The early-exit guards in the shaders prevent double-writes, so particles do not multiply — but:
- GPU workload is 4× what is needed
- For `aliveCount` estimation in the indirect pass: the indirect args CS computes `ceil(aliveCount / 256)` dispatch groups internally (GPU side), which is correct. The CPU dispatch of the indirect args CS itself is always `(1,1,1)` so it is unaffected.

---

## Non-Issues (Confirmed PASS)

### Billboard render pass b0 → root constants
```cpp
// particle_render_pass.cpp:96-98
cmd->SetRootConstants(kSlotBindlessIndices,   // slot 0
                      reinterpret_cast<const uint32_t*>(emitter.indicesCB.cpuPtr),
                      numDwords, 0);
```
Slot 0 is 32BIT_CONSTANTS → b0. The billboard VS declares `cbuffer VFXBillboardConstants : register(b0)`. Match.

### Billboard b1 PerFrameCB re-bind after SetPipeline
```cpp
// particle_render_pass.cpp:211-219
cmd->SetPipeline(passDesc.billboardPipeline);  // clears all bindings
// ...
cmd->SetRootCBV(kSlotPerFrame, passDesc.perFrameAlloc...);  // re-binds slot 1 (b1)
```
PerFrameCB is correctly re-bound at slot 1 (b1) immediately after `SetPipeline`. No stale binding.

### Billboard b2 VFXBillboardCB
`kSlotPerEmitter = 2` → b2. Billboard shaders use `register(b2)`. Match.

### Billboard VS/PS b0 layout
Both VS and PS declare identical `cbuffer VFXBillboardConstants : register(b0)` with all 12 fields in the same order. The PS comment correctly explains why `cbuffer` syntax is used instead of `ConstantBuffer<T>` (avoids DXC dead-member elimination).

### VFXBillboardCB size
C++ struct: `static_assert(sizeof(VFXBillboardCB) == 304)`. HLSL fields count: `4×4 (invView=64) + 3+1 (emitter=16) + 4 (tint=16) + 4×4 (prevVP=64) + rendering fields = 304`. Match. However: the HLSL `VFXBillboardCB` in the PS **adds flipbook fields** (`flipbookRows`, `flipbookCols`, `flipbookTotalFrames`) that are not present in `VFXBillboardCB` in `particle_render_pass.h`. The static_assert on the C++ side is 304 bytes. The HLSL PS CB ends with `flipbookTotalFrames` — trace the actual PS byte count:

```
invView(64) + emitter(12)+sizeMultiplier(4) = 80 + globalColorTint(16) + prevViewProj(64) = 160 VS part
softFadeDistance(4)+alphaErosion(4)+emissiveIntensity(4)+litMode(4) = 16
cameraOffset(4)+pivotOffsetX(4)+pivotOffsetY(4)+screenSpacePixelSize(4) = 16
sizeBySpeedEnabled(4)+sizeBySpeedMin(4)+sizeBySpeedMax(4)+speedRangeMax(4) = 16
sizeOverLifeStart(4)+sizeOverLifePeak(4)+sizeOverLifeEnd(4)+sizeOverLifePeakTime(4) = 16
colorLifeStart(16) + colorLifeEnd(16) = 32
facingMode(4)+customFacingX/Y/Z(12) = 16
stretchFactor(4)+blendModeHint(4)+colorRampLutSrvIndex(4)+screenSpaceSizeEnabled(4) = 16
colorBySpeedEnabled(4)+flipbookRows(4)+flipbookCols(4)+flipbookTotalFrames(4) = 16
```
PS HLSL total = 160 + 16×9 = 160 + 144 = 304 bytes.

C++ `VFXBillboardCB` ends at byte 300 with `_cbsPad2` (there is no flipbook section). The C++ struct has `colorBySpeedEnabled(4)+_cbsPad0(4)+_cbsPad1(4)+_cbsPad2(4)=16` at bytes 288-303. The HLSL PS has `colorBySpeedEnabled(4)+flipbookRows(4)+flipbookCols(4)+flipbookTotalFrames(4)=16` at those same bytes. The C++ pads are 0, so the shader reads `flipbookRows=0`, `flipbookCols=0`, `flipbookTotalFrames=0`. The PS tests `if (flipbookTotalFrames > 1u)` and `== 1u` — both false when 0, so it falls through to the procedural circle path. **This is functionally correct for the no-flipbook case**, but if flipbook is ever needed, the C++ struct must be extended.

---

## Prioritized Fix List

| Priority | Bug | File to Change | Fix |
|----------|-----|----------------|-----|
| 1 | Bug 2 — Indirect Args CS binds b0 to slot 1 | `particle_simulation_pass.cpp` `DispatchIndirectArgs()` | Change `SetRootCBV(kSlotPerDispatch, ...)` to `SetRootConstants(0, ...)` with the 16-DWORD struct. OR change shader to `register(b1)`. |
| 2 | Bug 4 — VFXUpdateCB field order swapped at byte 96 | `vfx_cb_structs.h` | Swap fields: put `freeListUavIndex` at 96, `freeListHeadUavIndex` at 100, `aliveCountNewUavIndex` at 104. Remove `idToIndexUavIndex` (not in HLSL) or add `_padShared0` to match. |
| 3 | Bug 6 — Thread group size mismatch 64 vs 256 | `particle_simulation_pass.cpp` | Change `kParticleThreadGroupSize = 64` to `256`. |
| 4 | Bug 3 — VFXSpawnCB missing defaultDynParam fields | `vfx_cb_structs.h` | Replace `_pad1 = 0` + `_pad2[4]` at bytes 172-191 with: `float defaultDynParam0=0, defaultDynParam1=0, defaultDynParam2=0, defaultDynParam3=0; uint32_t _padSpawnNew1=0; uint32_t _pad2[3]={}`. |
| 5 | Bug 5 — VFXIndirectArgsCB extra fields | `vfx_cb_structs.h` | Remove `verticesPerParticle` and `updateGroupSize` from the CB struct (they are static consts in the shader). Replace with `uint32_t _pad0=0; uint32_t _pad1=0;`. |
