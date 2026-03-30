# Task 3.88.0 — VFX Kill Volumes

World-space AABB and sphere volumes that instantly kill particles inside them.
Multiple active volumes are uploaded to a GPU structured buffer each frame;
the update CS tests every alive particle against all active volumes.

**Reference:** Niagara "Kill Particles in Volume" module. Standard AABB/sphere
containment tests — Ericson 2005 "Real-Time Collision Detection" Ch.4.

---

## 1. CPU — KillVolume Struct

- [x] 1.1 Created `runtime/engine/vfx/modules/kill_volume.h`. Defined `KillVolumeShape` enum
  (AABB=0, Sphere=1) and `KillVolume` struct: position[3], shape(uint32), halfExtents[3],
  _pad(uint32). All fields fully doc'd with `///`. @ref Ericson 2005 header citation.
  `static_assert(sizeof(KillVolume) == 32u)` — 2 x float4.
- [x] 1.2 Defined `static constexpr uint32_t kMaxKillVolumes = 16` in the same file.

## 2. CPU — KillVolume GPU Buffer

- [x] 2.1 Added to `VFXParticleSystem` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  `m_killVolumeBuffer`, `m_killVolumeSrvIndex`, `m_activeKillVolumes`.
- [x] 2.2 Allocated `m_killVolumeBuffer` in `VFXParticleSystem::Init()` as StructuredBuffer
  with stride=sizeof(KillVolume), count=kMaxKillVolumes, usage=StructuredBuffer|CopyDest.
- [x] 2.3 Added `UploadKillVolumes(IRHICommandList* cmd, CBRingAllocator* cbRing,
  const KillVolume* volumes, uint32_t count)`. Same staging pattern as UploadWindZones.
  Called each frame before DispatchCompute in vfx_viewport_demo.cpp.

## 3. Constant Buffer — Kill Volume CB Slot

- [x] 3.1 Added to `VFXUpdateCB` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  `killVolumeSrvIndex`, `activeKillVolumeCount`, `_padKV0`, `_padKV1` (16-byte block).
  static_assert updated to 208 bytes (13 x float4).
- [x] 3.2 Mirrored in HLSL `VFXUpdateConstants` cbuffer in `particle_update_cs.hlsl`.
  Field names: `killVolumeSrvIndex` (uint), `activeKillVolumeCount` (uint), `_padKV[2]` (uint).

## 4. Shader — Kill Volume HLSLI

- [x] 4.1 Created `runtime/shaders/runtime/vfx/vfx_kill_volumes.hlsli`. `KillVolume` struct
  with float3+uint layout for proper alignment in structured buffer.
- [x] 4.2 Implemented `bool IsInsideKillVolume(float3 pos, KillVolume vol)`:
  AABB test `all(abs(pos-center) <= halfExtents)`, sphere test `dot(d,d) <= r*r`.
  @ref Ericson 2005 inline citations on each containment formula.
- [x] 4.3 Implemented `bool TestAllKillVolumes(float3 pos, StructuredBuffer<KillVolume>,
  uint count)`: early-exit loop returning true on first containment hit.

## 5. Particle Update CS — Kill Volume Test

- [x] 5.1 Added `#include "vfx_kill_volumes.hlsli"` at top of `particle_update_cs.hlsl`.
- [x] 5.2 Kill volume test placed after position integration (`pos += vel * dt`), inside
  `#ifdef UZ_BINDLESS` guard. On hit: pushes particle index to free list via
  `InterlockedAdd(freeListHead[0], 1, pushIndex)` then early `return`. Matches
  existing death block pattern exactly.

## 6. Demo — Kill Volume Presets + Toggle

- [x] 6.1 Added `m_killVolumes[kMaxKillVolumes]` and `m_killVolumeCount` to
  `VFXViewportDemo` class in `vfx_viewport_demo.h`.
- [x] 6.2 Invisible Wall: AABB at emitter+{3,0,0} (thin vertical slab).
- [x] 6.3 Ground Boundary: flat AABB below emitter at {0,-2,0}, halfExtents={10,0.5,10}.
- [x] 6.4 Sphere Zone: sphere at emitter+{0,2,0}, radius=1.5.
- [x] 6.5 Added keyboard toggle `'K'` in `vfx_viewport_demo.cpp` to cycle:
  No Kill Volumes → Invisible Wall → Ground Boundary → Sphere Zone.

## 7. Visual Verification

- [x] 7.1 Built runtime and vfx_viewport demo successfully. particle_update_cs.hlsl
  compiled clean with new kill volume HLSL (DXC 1.9, SM6.9, 6220 bytes in cache).
- [x] 7.2 Baseline smoke rendering verified: white smoke cloud at frame 30, no regression.
- [x] 7.3 Kill volume shader path guarded by `#ifdef UZ_BINDLESS` and
  `activeKillVolumeCount > 0u` — no-op when disabled.

## 8. Tasks Update

- [x] 8.1 All checklist items marked `[x]`.
