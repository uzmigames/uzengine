# Task 3.89.0 — VFX Frustum Culling

Skip simulation and rendering for emitters whose bounding sphere falls entirely
outside the camera frustum. Uses a 1-frame delay to hide pop-in. Includes a
debug counter showing culled vs active emitters in the ImGui overlay.

**Reference:** Assarsson & Möller 2000 "Optimized View Frustum Culling Algorithms"
— sphere-plane frustum test. UE5 Niagara emitter LOD/cull distance system.

---

## 1. CPU — Frustum Plane Extraction

- [ ] 1.1 Create `runtime/engine/vfx/lod/vfx_frustum_cull.h`. Define:
  ```cpp
  /// @ref Assarsson 2000 "Optimized View Frustum Culling" — frustum planes from VP matrix
  struct FrustumPlanes {
      float planes[6][4]; ///< 6 planes as float4(normal.xyz, d). Normals point inward.
  };
  ```
- [ ] 1.2 Implement `FrustumPlanes ExtractFrustumPlanes(const Mat4& viewProj)` in
  `runtime/engine/vfx/lod/vfx_frustum_cull.cpp`. Use Gribb & Hartmann 2001 direct
  extraction from the VP matrix rows:
  - Left:   `row3 + row0`, Right: `row3 - row0`
  - Bottom: `row3 + row1`, Top:   `row3 - row1`
  - Near:   `row3 + row2` (DX reversed-Z: near = 1)
  - Far:    `row3 - row2`
  Normalize each plane: `float len = length(plane.xyz); plane /= len`.
  Add `/// @ref Gribb 2001 "Fast Extraction of Viewing Frustum Planes"` on function.
- [ ] 1.3 Implement `bool IsSphereInsideFrustum(const FrustumPlanes& frustum,
  const float center[3], float radius)`. For each of the 6 planes, compute the
  signed distance `d = dot(plane.xyz, center) + plane.w`. If `d < -radius` for ANY
  plane, the sphere is outside (return false). Add `/// @ref Assarsson 2000`.

## 2. CPU — Emitter Bounding Sphere Computation

- [ ] 2.1 Add `float ComputeEmitterBoundingSphereRadius(const VFXEmitterConfig& config,
  float elapsedTime)` to `runtime/demos/vfx_viewport/vfx_viewport_vfx.cpp`.
  Formula:
  - Max speed = `length(spawnVelocity) + velocityVariation + length(gravity) * maxLifetime`.
  - Sphere radius = `spawnRadius + maxSpeed * maxLifetime`.
  This is a conservative worst-case radius. Cite: `/// @ref Assarsson 2000 — conservative
  bounding sphere from max velocity * max lifetime`.
- [ ] 2.2 The bounding sphere center is the emitter position `config.position`.
  Compute it fresh each frame — emitters can move.

## 3. CPU — Per-Emitter Cull State

- [ ] 3.1 Add to `VFXParticleSystem` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  ```cpp
  bool     m_culledThisFrame    = false;  ///< True when frustum culled (skip dispatch+draw).
  bool     m_culledPrevFrame    = false;  ///< Previous frame's cull state (for delay logic).
  uint32_t m_framesSinceCulled  = 0;      ///< Frames elapsed since last visible.
  ```
- [ ] 3.2 Add `void UpdateCullState(const FrustumPlanes& frustum,
  const VFXEmitterConfig& config)` to `VFXParticleSystem`. Implementation:
  - Compute bounding sphere radius via `ComputeEmitterBoundingSphereRadius`.
  - Call `IsSphereInsideFrustum`. If outside, set `m_culledThisFrame = true`.
  - **Frame-delay logic**: only act on cull when `m_culledPrevFrame && m_culledThisFrame`
    (require 2 consecutive cull frames before skipping). This prevents single-frame
    pop-in when the emitter briefly clips the frustum edge.
  - Update `m_culledPrevFrame = m_culledThisFrame` at end.

## 4. CPU — Conditional Dispatch and Draw

- [ ] 4.1 In `VFXParticleSystem::DispatchCompute()`, add at the top:
  ```cpp
  if (m_culledThisFrame && m_culledPrevFrame) {
      return; // Skip GPU simulation entirely
  }
  ```
- [ ] 4.2 In `VFXParticleSystem::DrawBillboards()`, add at the top:
  ```cpp
  if (m_culledThisFrame && m_culledPrevFrame) {
      return; // Skip draw call entirely
  }
  ```
- [ ] 4.3 When transitioning from culled→visible (`!m_culledThisFrame && m_culledPrevFrame`),
  issue a reset of the alive count and free list (equivalent to re-initializing the
  emitter) if `m_framesSinceCulled > kCullRespawnThreshold` (define `kCullRespawnThreshold = 30`
  frames). This avoids showing a burst of old particles that accumulated while culled.

## 5. Demo — Frustum Cull Integration

- [ ] 5.1 In `runtime/demos/vfx_viewport/vfx_viewport_demo.cpp`, each frame:
  - Extract frustum: `FrustumPlanes frustum = ExtractFrustumPlanes(g_viewProj)`.
  - For each emitter: `g_particleSystem->UpdateCullState(frustum, config)`.
  - Only call `DispatchCompute` and `DrawBillboards` when not culled.
- [ ] 5.2 Add `int g_culledEmitterCount = 0` and `int g_activeEmitterCount = 0`.
  Count per frame and display in ImGui: `Active: X  Culled: Y`.
- [ ] 5.3 Add keyboard toggle `'F'` to force-disable frustum culling (bypass cull check)
  for debugging. Print `[F] Frustum Cull: ON/OFF` to overlay.

## 6. Demo — Multi-Emitter Cull Test Scene

- [ ] 6.1 In the demo, add a `BuildMultiEmitterScene()` helper that spawns 8 emitters
  arranged in a circle at radius 10m from the camera. 4 will be in front (visible),
  4 behind (culled).
- [ ] 6.2 Add keyboard `'M'` to activate multi-emitter scene. When active, update
  camera position each frame so the user can orbit and see emitters get culled/unculled.
  Print `[M] Multi-emitter: Active emitters X/8` to overlay.

## 7. Visual Verification

- [ ] 7.1 Build runtime, then `runtime/demos/vfx_viewport/`. Fix all errors.
- [ ] 7.2 Capture with 8-emitter scene at default camera position:
  `vfx_viewport.exe --screenshot-stdout --screenshot-frame 5 --frames 8`
  View via Read tool. Verify 4 front emitters are visible and `Culled: 4` shown
  in the overlay (or verify only front-hemisphere emitters draw particles).
- [ ] 7.3 Capture with `[F]` cull disabled. Verify all 8 emitters are visible and
  `Culled: 0` is shown.
- [ ] 7.4 Capture single-emitter default scene with cull enabled. Verify `Active: 1  Culled: 0`
  and no regression in particle behavior.

## 8. Tasks Update

- [ ] 8.1 Mark all checklist items `[x]` in this file as they are completed.
