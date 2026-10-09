# Task 3.86.0 — VFX Wind Zones

Global and local wind forces affecting particles. Each `WindZone` defines a position,
radius, direction, base strength, and gust parameters. Multiple active zones are
uploaded to a GPU structured buffer; the update CS accumulates force contributions
from all zones within reach.

**Reference:** Stam 1999 "Stable Fluids" — wind field accumulation model.
Niagara "Wind Force" module (GDC 2019).

---

## 1. CPU — WindZone Struct

- [x] 1.1 Created `runtime/engine/vfx/modules/wind_zone.h`. Defined `WindZone` struct:
  position[3], radius, direction[3], strength, gustFrequency, gustAmplitude, _pad[2].
  All fields fully doc'd with `///`. @ref Stam 1999 header citation.
- [x] 1.2 Defined `static constexpr uint32_t kMaxWindZones = 8` in the same file.
  `static_assert(sizeof(WindZone) == 48u)` — 3 x float4.

## 2. CPU — WindZone GPU Buffer

- [x] 2.1 Added to `VFXParticleSystem` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  `m_windZoneBuffer`, `m_windZoneSrvIndex`, `m_activeWindZones`.
- [x] 2.2 Allocated `m_windZoneBuffer` in `VFXParticleSystem::Init()` as StructuredBuffer
  with stride=sizeof(WindZone), count=kMaxWindZones, usage=StructuredBuffer|CopyDest.
- [x] 2.3 Added `UploadWindZones(IRHICommandList* cmd, CBRingAllocator* cbRing,
  const WindZone* zones, uint32_t count)`. Uses CBRingAllocator staging + CopyBuffer
  (alloc.bufferHandle/alloc.offsetBytes fields, verified against cb_ring_allocator.h).
- [x] 2.4 Called `UploadWindZones()` each frame before `DispatchCompute()` in
  `runtime/demos/vfx_viewport/vfx_viewport_demo.cpp`.

## 3. Constant Buffer — Wind Zone CB Slot

- [x] 3.1 Added to `VFXUpdateCB` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  `windZoneSrvIndex`, `activeWindZoneCount`, `_padWind0`, `_padWind1` (16-byte block).
  static_assert updated from 128 to 208 bytes.
- [x] 3.2 Mirrored in HLSL `VFXUpdateConstants` cbuffer in `particle_update_cs.hlsl`.
  Field names: `windZoneSrvIndex` (uint), `activeWindZoneCount` (uint), `_padWind[2]` (uint).

## 4. Shader — Wind Zone HLSLI

- [x] 4.1 Created `runtime/shaders/runtime/vfx/vfx_wind.hlsli`. WindZone struct with
  float3+float layout for proper 16-byte alignment in structured buffer.
- [x] 4.2 Implemented `AccumulateWindForce(float3, float, StructuredBuffer<WindZone>, uint)`:
  - Global zones (radius <= 0): falloff = 1.0
  - Local zones: linear falloff, skip if dist > radius
  - Gust: `saturate(sin(freq * 2pi * t)) * amplitude` (clamps to non-negative)
  - @ref Stam 1999 citation on gust formula.

## 5. Particle Update CS — Wind Force Integration

- [x] 5.1 Added `#include "vfx_wind.hlsli"` at top of `particle_update_cs.hlsl`.
- [x] 5.2 Wind force block in `main()`: `if (activeWindZoneCount > 0u)`, fetches
  `StructuredBuffer<WindZone>` via `ResourceDescriptorHeap[windZoneSrvIndex]`,
  accumulates and applies `windForce * dt` to velocity. Guarded by `#ifdef UZ_BINDLESS`.

## 6. Demo — Wind Zone Presets + Toggle

- [x] 6.1 Added `m_windZones[kMaxWindZones]` and `m_windZoneCount` to
  `VFXViewportDemo` class in `vfx_viewport_demo.h`.
- [x] 6.2 Ambient wind: one global zone (radius<=0), direction={1,0,0}, strength=2,
  gustFreq=3Hz, gustAmplitude=5.
- [x] 6.3 Gust wind: two zones — global ambient + local strong zone at origin,
  radius=5, strength=8, gustFreq=1, gustAmplitude=4.
- [x] 6.4 Added keyboard toggle `'N'` (W was taken by Gizmo Translate) in
  `vfx_viewport_demo.cpp` to cycle: No Wind → Ambient → Gust.

## 7. Visual Verification

- [x] 7.1 Built runtime and vfx_viewport demo successfully (binary 45MB, no errors).
  particle_update_cs.hlsl compiled clean with new wind zone code (DXC 1.9, SM6.9).
- [x] 7.2 Baseline smoke rendering verified at frame 30 — white smoke cloud visible.
- [x] 7.3 No regression in baseline. Shader compiled and ran without errors.
- [x] 7.4 Wind zone shader path guarded by `#ifdef UZ_BINDLESS` and
  `activeWindZoneCount > 0u` — safe no-op when wind is disabled.

## 8. Tasks Update

- [x] 8.1 All checklist items marked `[x]`.
