# Phase 4d — 4.28.0 VFX LOD Spawn Rate Scaling

Scale emission rate and max particle cap based on camera distance. Extends the
existing `VFXLodSettings` (3.65.0) with per-level rate multipliers and smooth
transitions. Niagara reference: Scalability settings + significance manager.

---

## 1. LOD Settings Extension

- [x] 1.1 Added `float spawnRateScale[4] = {1.0f, 0.5f, 0.25f, 0.1f}` to `VFXLodSettings`
      in `vfx_lod.h`. One scale per LOD level (0=nearest, 3=farthest).
- [x] 1.2 Added `float maxParticlesScale[4] = {1.0f, 0.75f, 0.5f, 0.25f}` to `VFXLodSettings`.
      Reduces the emitter's max_particles cap at distance.
- [x] 1.3 Added `float lodTransitionTime = 0.5f` to `VFXLodSettings`. Seconds to lerp
      between LOD levels to avoid pop.
- [x] 1.4 Added `float currentSpawnScale = 1.0f` and `float targetSpawnScale = 1.0f` as
      runtime fields on `ParticleEmitter` for smooth interpolation.

## 2. .uzvfx Format

- [ ] 2.1 Add optional `lod_spawn_rate_scale` and `lod_max_particles_scale` arrays
      to emitter JSON. Default to [1.0, 0.5, 0.25, 0.1] if absent.
- [ ] 2.2 Add `lod_transition_time` optional field. Default 0.5.
- [ ] 2.3 Validate: all scale values must be in [0, 1] range. Error if > 1 or < 0.

## 3. Runtime Scaling

- [ ] 3.1 In `frame_particles.cpp` (or LOD evaluation point), after computing
      `currentLodLevel` from distance, set `targetSpawnScale = spawnRateScale[level]`.
- [ ] 3.2 Smooth `currentSpawnScale` toward `targetSpawnScale` each frame:
      `currentSpawnScale = lerp(currentSpawnScale, targetSpawnScale, dt / lodTransitionTime)`.
      Clamp to [0.01, 1.0] to avoid zero emission.
- [ ] 3.3 Multiply the emitter's `emissionRate` by `currentSpawnScale` before feeding
      the spawn accumulator. This reduces how many particles spawn per frame.
- [ ] 3.4 Multiply the emitter's `maxParticles` by `maxParticlesScale[level]` to reduce
      the particle cap. New particles beyond the scaled cap are not spawned.

## 4. Screen-Size Significance (Optional Enhancement)

- [x] 4.1 Added `float screenSizeThreshold = 0.01f` to `VFXLodSettings`. If the emitter's
      bounding sphere projects to < threshold fraction of screen height, skip rendering
      entirely (LOD level = culled).
- [ ] 4.2 Computation: `screenSize = boundsSphereRadius / (distance * tan(fovY/2))`.
      Uses the existing bounds from frustum culling or dynamic AABB (4.25.0).

## 5. Stats Overlay

- [ ] 5.1 Display per-emitter LOD level and spawn rate scale in the VFX debug overlay:
      "LOD:2 rate:0.25x cap:4096→1024".
- [ ] 5.2 Display total particle budget savings: "LOD savings: 12,400 particles/frame".

## 6. Tests

- [ ] 6.1 Unit test: LOD level 0 → spawnRateScale = 1.0, level 3 → 0.1.
- [ ] 6.2 Unit test: smooth transition from 1.0 to 0.25 over 0.5s (verify intermediate values).
- [ ] 6.3 Unit test: maxParticles cap reduces correctly (8192 × 0.25 = 2048).
- [ ] 6.4 Unit test: screen-size threshold calculation at known distance/FOV/radius.
- [ ] 6.5 Integration test: emitter at LOD 3 spawns ~10% of particles compared to LOD 0.

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
