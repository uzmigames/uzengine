# Task 3.86.0 — VFX Wind Zones

Global and local wind forces affecting particles. Each `WindZone` defines a position,
radius, direction, base strength, and gust parameters. Multiple active zones are
uploaded to a GPU structured buffer; the update CS accumulates force contributions
from all zones within reach.

**Reference:** Stam 1999 "Stable Fluids" — wind field accumulation model.
Niagara "Wind Force" module (GDC 2019).

---

## 1. CPU — WindZone Struct

- [ ] 1.1 Create `runtime/engine/vfx/modules/wind_zone.h`. Define:
  ```cpp
  /// @ref Stam 1999 "Stable Fluids" — wind zone force field
  struct WindZone {
      float position[3]     = {0,0,0};    ///< World-space center of influence.
      float radius          = 20.0f;      ///< Influence radius. 0 = global (infinite).
      float direction[3]    = {1,0,0};    ///< Normalized wind direction.
      float strength        = 3.0f;       ///< Base wind speed (m/s).
      float gustFrequency   = 0.5f;       ///< Gusts per second.
      float gustAmplitude   = 1.5f;       ///< Peak gust speed added to base (m/s).
      float _pad[2]         = {};         ///< Pad to 48 bytes (3 float4s).
  };
  static_assert(sizeof(WindZone) == 48, "WindZone must be 48 bytes (3 x float4)");
  ```
  Add full Rust-style `///` doc on every field.
- [ ] 1.2 Define `static constexpr uint32_t kMaxWindZones = 8` in the same file.
  This caps the GPU structured buffer size.

## 2. CPU — WindZone GPU Buffer

- [ ] 2.1 Add to `VFXParticleSystem` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  ```cpp
  RHIBufferHandle m_windZoneBuffer;        ///< StructuredBuffer<WindZone>[kMaxWindZones].
  uint32_t        m_windZoneSrvIndex = 0;  ///< Bindless SRV descriptor index.
  uint32_t        m_activeWindZones  = 0;  ///< Number of active zones this frame.
  ```
- [ ] 2.2 Allocate `m_windZoneBuffer` in `VFXParticleSystem::Init()` as a
  `StructuredBuffer` with stride `sizeof(WindZone)` and element count
  `kMaxWindZones`. Mark as `UnorderedAccess | ShaderResource` so it can be
  written by CPU upload and read by CS.
- [ ] 2.3 Add `void UploadWindZones(IRHICommandList* cmd, const WindZone* zones,
  uint32_t count)` to `VFXParticleSystem`. Implementation:
  - Clamp `count` to `kMaxWindZones`.
  - Write to a per-frame staging buffer (use the `CBRingAllocator` pattern already
    used for other staging uploads in the demo).
  - Issue a `CopyBufferRegion` from staging to `m_windZoneBuffer`.
  - Store `count` in `m_activeWindZones`.
- [ ] 2.4 Call `UploadWindZones()` each frame before `DispatchCompute()` in
  `runtime/demos/vfx_viewport/vfx_viewport_demo.cpp`.

## 3. Constant Buffer — Wind Zone CB Slot

- [ ] 3.1 Add to `VFXUpdateCB` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  ```cpp
  uint32_t windZoneBufferSrvIndex;   ///< Bindless SRV for WindZone[].
  uint32_t activeWindZoneCount;      ///< Number of active wind zones [0..8].
  float    totalTime;               ///< Elapsed time for gust animation (if not already present).
  float    _padWind;
  ```
  Update the static_assert.
- [ ] 3.2 Mirror in the HLSL `VFXUpdateConstants` cbuffer in
  `runtime/shaders/runtime/vfx/particle_update_cs.hlsl`.

## 4. Shader — Wind Zone HLSLI

- [ ] 4.1 Create `runtime/shaders/runtime/vfx/vfx_wind.hlsli`. Add:
  ```hlsl
  /// @ref Stam 1999 "Stable Fluids" — wind zone accumulation
  struct WindZone {
      float3 position;   float radius;
      float3 direction;  float strength;
      float  gustFrequency; float gustAmplitude; float2 _pad;
  };
  ```
- [ ] 4.2 Implement `float3 AccumulateWindForce(float3 particlePos, float time,
  StructuredBuffer<WindZone> zones, uint count)`:
  - Accumulate over `count` zones.
  - For each zone: if `zone.radius > 0`, compute `dist = length(particlePos - zone.position)`,
    skip if `dist > zone.radius`, compute linear falloff `= 1.0 - dist/zone.radius`.
  - If `zone.radius <= 0` (global zone), falloff = 1.0.
  - Compute gust: `float gust = zone.gustAmplitude * sin(time * zone.gustFrequency * 6.2832f)`.
    Use `saturate(gust)` (gusts only add, do not subtract). Cite:
    `/// @ref Stam 1999 — gust = sin(freq * 2pi * t) * amplitude`.
  - Accumulate `zone.direction * (zone.strength + gust) * falloff`.
  - Return total accumulated force.

## 5. Particle Update CS — Wind Force Integration

- [ ] 5.1 Add `#include "vfx_wind.hlsli"` at top of
  `runtime/shaders/runtime/vfx/particle_update_cs.hlsl`.
- [ ] 5.2 In `main()`, after gravity/damping and before position integration, insert:
  ```hlsl
  if (activeWindZoneCount > 0) {
      StructuredBuffer<WindZone> windZones =
          ResourceDescriptorHeap[windZoneBufferSrvIndex];
      float3 windForce = AccumulateWindForce(pos, totalTime, windZones,
                                              activeWindZoneCount);
      vel += windForce * dt;
  }
  ```
  Add `/// @ref Stam 1999 — wind zone accumulation` above the block.

## 6. Demo — Wind Zone Presets + Toggle

- [ ] 6.1 Add `WindZone g_windZones[kMaxWindZones]` and `int g_windZoneCount = 0`
  to `runtime/demos/vfx_viewport/vfx_viewport_demo.cpp` (static/global state).
- [ ] 6.2 Add `BuildAmbientWind()` that configures one global zone:
  - `radius = 0` (global), `direction = {1, 0, 0}`, `strength = 2.0f`,
    `gustFrequency = 0.3f`, `gustAmplitude = 3.0f`.
- [ ] 6.3 Add `BuildGustWind()` that configures two zones (ambient + a local strong gust):
  - Zone 0: global, `direction = {0.7, 0, 0.7}`, `strength = 1.0f`.
  - Zone 1: `position = {0, 0, 0}`, `radius = 5.0f`, `direction = {1, 0.2, 0}`,
    `strength = 8.0f`, `gustFrequency = 1.0f`, `gustAmplitude = 4.0f`.
- [ ] 6.4 Add keyboard toggle `'W'` to cycle: `No Wind` → `Ambient` → `Gust`.
  Print `[W] Wind: OFF/AMBIENT/GUST` to ImGui overlay.

## 7. Visual Verification

- [ ] 7.1 Build runtime, then `runtime/demos/vfx_viewport/`. Fix all errors.
- [ ] 7.2 Capture `Ambient` wind screenshot:
  `vfx_viewport.exe --screenshot-stdout --screenshot-frame 12 --frames 18`
  View via Read tool. Verify particles drift horizontally in the wind direction
  rather than rising straight up.
- [ ] 7.3 Capture `No Wind`. Verify no regression.
- [ ] 7.4 Capture `Gust` wind. Verify local zone creates stronger drift near origin.

## 8. Tasks Update

- [ ] 8.1 Mark all checklist items `[x]` in this file as they are completed.
