# Task 3.88.0 — VFX Kill Volumes

World-space AABB and sphere volumes that instantly kill particles inside them.
Multiple active volumes are uploaded to a GPU structured buffer each frame;
the update CS tests every alive particle against all active volumes.

**Reference:** Niagara "Kill Particles in Volume" module. Standard AABB/sphere
containment tests — Ericson 2005 "Real-Time Collision Detection" Ch.4.

---

## 1. CPU — KillVolume Struct

- [ ] 1.1 Create `runtime/engine/vfx/modules/kill_volume.h`. Define:
  ```cpp
  /// Shape of the kill volume.
  enum class KillVolumeShape : uint32_t {
      AABB   = 0, ///< Axis-aligned bounding box.
      Sphere = 1, ///< Sphere centered at position.
  };

  /// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4 — AABB/sphere containment
  struct KillVolume {
      float    position[3]  = {0,0,0};    ///< World-space center.
      uint32_t shape        = 0;          ///< KillVolumeShape enum (0=AABB, 1=Sphere).
      float    halfExtents[3] = {1,1,1};  ///< AABB: half-size per axis. Sphere: uses halfExtents[0] as radius.
      uint32_t _pad         = 0;          ///< Align to 32 bytes.
  };
  static_assert(sizeof(KillVolume) == 32, "KillVolume must be 32 bytes (2 x float4)");
  ```
  All fields fully doc'd with `///`.
- [ ] 1.2 Define `static constexpr uint32_t kMaxKillVolumes = 16` in the same file.

## 2. CPU — KillVolume GPU Buffer

- [ ] 2.1 Add to `VFXParticleSystem` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  ```cpp
  RHIBufferHandle m_killVolumeBuffer;          ///< StructuredBuffer<KillVolume>[16].
  uint32_t        m_killVolumeSrvIndex = 0;    ///< Bindless SRV descriptor index.
  uint32_t        m_activeKillVolumes  = 0;
  ```
- [ ] 2.2 Allocate `m_killVolumeBuffer` in `VFXParticleSystem::Init()` as a
  `StructuredBuffer` with stride `sizeof(KillVolume)` and count `kMaxKillVolumes`.
  Usage: `ShaderResource | CopyDest`.
- [ ] 2.3 Add `void UploadKillVolumes(IRHICommandList* cmd, const KillVolume* volumes,
  uint32_t count)` to `VFXParticleSystem`. Implementation mirrors `UploadWindZones`
  pattern (staging copy via `CBRingAllocator`, `CopyBufferRegion` to GPU buffer).

## 3. Constant Buffer — Kill Volume CB Slot

- [ ] 3.1 Add to `VFXUpdateCB` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  ```cpp
  uint32_t killVolumeBufferSrvIndex;   ///< Bindless SRV for KillVolume[].
  uint32_t activeKillVolumeCount;      ///< Number of active kill volumes [0..16].
  uint32_t _padKV0;
  uint32_t _padKV1;
  ```
  Pack into a new 16-byte block. Update static_assert.
- [ ] 3.2 Mirror in HLSL `VFXUpdateConstants` cbuffer in `particle_update_cs.hlsl`.

## 4. Shader — Kill Volume HLSLI

- [ ] 4.1 Create `runtime/shaders/runtime/vfx/vfx_kill_volumes.hlsli`. Add:
  ```hlsl
  /// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4 — containment tests
  struct KillVolume {
      float3   position;   uint   shape;      // 16 bytes
      float3   halfExtents; uint  _pad;       // 16 bytes
  };
  ```
- [ ] 4.2 Implement `bool IsInsideKillVolume(float3 pos, KillVolume vol)`:
  ```hlsl
  // AABB test
  if (vol.shape == 0u) {
      float3 d = abs(pos - vol.position) - vol.halfExtents;
      return all(d <= 0.0f);
  }
  // Sphere test: halfExtents.x = radius
  float3 delta = pos - vol.position;
  float  r     = vol.halfExtents.x;
  return dot(delta, delta) <= r * r;
  ```
  Add `/// @ref Ericson 2005 — AABB point containment: all(abs(p-c) <= h)` and
  `/// @ref Ericson 2005 — sphere point containment: dot(p-c,p-c) <= r*r` inline.
- [ ] 4.3 Implement `bool TestAllKillVolumes(float3 pos,
  StructuredBuffer<KillVolume> volumes, uint count)`: iterate `count` volumes,
  return `true` immediately if any `IsInsideKillVolume()` returns true.

## 5. Particle Update CS — Kill Volume Test

- [ ] 5.1 Add `#include "vfx_kill_volumes.hlsli"` at top of `particle_update_cs.hlsl`.
- [ ] 5.2 In `main()`, after the death check (`age >= lifespan`) and before physics,
  add:
  ```hlsl
  if (activeKillVolumeCount > 0) {
      StructuredBuffer<KillVolume> killVols =
          ResourceDescriptorHeap[killVolumeBufferSrvIndex];
      if (TestAllKillVolumes(pos, killVols, activeKillVolumeCount)) {
          // Instant kill: push to free list and return
          // [copy death block from existing age >= lifespan path]
          age = lifespan; // mark as dead
          uint pushIndex;
          InterlockedAdd(freeListHead[0], 1, pushIndex);
          if (pushIndex < maxParticles) { freeList[pushIndex] = threadIdx; }
          return;
      }
  }
  ```
  Add `/// @ref Ericson 2005 — kill volume containment test` above the block.
  Important: use the same free-list push pattern as the existing death block (with
  proper bindless/SM5 macro guards).

## 6. Demo — Kill Volume Presets + Toggle

- [ ] 6.1 Add `KillVolume g_killVolumes[kMaxKillVolumes]` and `int g_killVolumeCount = 0`
  to `runtime/demos/vfx_viewport/vfx_viewport_demo.cpp`.
- [ ] 6.2 Define `BuildInvisibleWall()`: one AABB kill volume at `{3, 0, 0}` with
  `halfExtents = {0.3, 5, 5}` — a thin vertical wall that stops particles drifting right.
- [ ] 6.3 Define `BuildUnderwaterBoundary()`: one AABB kill volume at `{0, -2, 0}` with
  `halfExtents = {10, 0.5, 10}` — kills particles that fall below the ground plane.
- [ ] 6.4 Define `BuildSphereKillZone()`: one Sphere at `{0, 2, 0}` with radius `1.5f` —
  a sphere that kills particles passing through the emitter's upper region.
- [ ] 6.5 Add keyboard toggle `'K'` to cycle: `No Kill Volumes` → `Invisible Wall`
  → `Underwater Boundary` → `Sphere Zone`. Print to ImGui overlay.
- [ ] 6.6 Call `g_particleSystem->UploadKillVolumes(cmd, g_killVolumes, g_killVolumeCount)`
  each frame in the command recording section.

## 7. Visual Verification

- [ ] 7.1 Build runtime, then `runtime/demos/vfx_viewport/`. Fix all errors.
- [ ] 7.2 Capture `Invisible Wall`:
  `vfx_viewport.exe --screenshot-stdout --screenshot-frame 10 --frames 15`
  View via Read tool. Verify particles stop (disappear) at x=3 and do not pass through.
- [ ] 7.3 Capture `Sphere Zone`. Verify particles vanish when entering the sphere.
- [ ] 7.4 Capture `No Kill Volumes`. Verify no regression.

## 8. Tasks Update

- [ ] 8.1 Mark all checklist items `[x]` in this file as they are completed.
