# Task 3.83.0 — VFX Vortex Forces

Particles swirl around a configurable axis (tornado/whirlpool). Computes tangential
force via `cross(axisDir, toParticle)` plus an optional radial pull/push that
tightens or loosens the spiral.

**Reference:** Stomakhin et al. 2013 "A Material Point Method for Snow Simulation"
Appendix A — vortex field formulation; also Niagara "Vortex Force" module.

---

## 1. Shader — Vortex Force HLSLI

- [ ] 1.1 Create `runtime/shaders/runtime/vfx/vfx_vortex.hlsli`. Add header:
  ```hlsl
  /// @ref Stomakhin 2013 Appendix A — tangential vortex + radial pull/push
  /// @constants vortex: tangentialStrength, radialStrength, maxRadius
  ```
- [ ] 1.2 Implement `float3 ComputeVortexForce(float3 pos, float3 axisOrigin, float3 axisDir,
  float tangentialStrength, float radialStrength, float maxRadius)`:
  1. `float3 toParticle = pos - axisOrigin`
  2. `float3 axialComponent = dot(toParticle, axisDir) * axisDir`
  3. `float3 radialVec = toParticle - axialComponent` (vector from axis to particle in the perpendicular plane)
  4. `float dist = length(radialVec)` — radial distance from axis
  5. Early exit: if `dist < 0.0001 || dist > maxRadius` return `float3(0,0,0)`
  6. `float3 radialDir = radialVec / dist`
  7. `float3 tangentDir = normalize(cross(axisDir, radialDir))`
  8. `float falloff = 1.0 - saturate(dist / maxRadius)` — linear falloff at edge
  9. Return `(tangentDir * tangentialStrength - radialDir * radialStrength) * falloff`
  — negative radialStrength = push outward, positive = pull inward (spiral tightening).
  Add `/// @ref Stomakhin 2013` inline comment at step 7.
- [ ] 1.3 Add `@constants` annotation documenting the falloff formula.

## 2. Constant Buffer — Update CS Extension

- [ ] 2.1 Add a 32-byte vortex block to `VFXUpdateCB` in
  `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  ```cpp
  float    vortexAxisOrigin[3];   float vortexTangentialStrength;  // 16 bytes
  float    vortexAxisDir[3];      float vortexRadialStrength;       // 16 bytes
  float    vortexMaxRadius;       float _padVortex[3];              // 16 bytes
  ```
  Update the `static_assert` size check.
- [ ] 2.2 Mirror these fields verbatim in the HLSL `VFXUpdateConstants` cbuffer in
  `runtime/shaders/runtime/vfx/particle_update_cs.hlsl`. Verify byte offsets match
  by counting existing fields. Fields must appear after the existing `_padShared0`.

## 3. Particle Update CS — Vortex Force Integration

- [ ] 3.1 Add `#include "vfx_vortex.hlsli"` at the top of
  `runtime/shaders/runtime/vfx/particle_update_cs.hlsl`.
- [ ] 3.2 In `main()`, after gravity + damping and before position integration, insert:
  ```hlsl
  if (vortexTangentialStrength != 0.0f || vortexRadialStrength != 0.0f) {
      float3 vortexF = ComputeVortexForce(
          pos,
          vortexAxisOrigin,
          normalize(vortexAxisDir),
          vortexTangentialStrength,
          vortexRadialStrength,
          vortexMaxRadius
      );
      vel += vortexF * dt;
  }
  ```
  Add `/// @ref Stomakhin 2013 — vortex tangential + radial` above the block.

## 4. CPU — VFXEmitterConfig Vortex Fields

- [ ] 4.1 Add to `VFXEmitterConfig` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  ```cpp
  float    vortexAxisOrigin[3]       = {0.0f, 0.0f, 0.0f};
  float    vortexAxisDir[3]          = {0.0f, 1.0f, 0.0f}; ///< Default: +Y up axis
  float    vortexTangentialStrength  = 0.0f;  ///< 0 = disabled
  float    vortexRadialStrength      = 0.0f;  ///< >0 pulls in, <0 pushes out
  float    vortexMaxRadius           = 5.0f;  ///< World-space cutoff radius
  ```
- [ ] 4.2 In the CB fill function in `runtime/demos/vfx_viewport/vfx_viewport_vfx.cpp`,
  map `VFXEmitterConfig` vortex fields into `VFXUpdateCB` vortex fields.
  Normalize `vortexAxisDir` on the CPU before upload (guard against zero-length input).

## 5. Demo — Vortex Presets + Toggle

- [ ] 5.1 Add a `"Tornado"` preset to the presets array in
  `runtime/demos/vfx_viewport/vfx_viewport_vfx.cpp`:
  - `vortexAxisDir = {0,1,0}` (+Y up axis)
  - `vortexTangentialStrength = 8.0f`, `vortexRadialStrength = 2.0f` (inward pull)
  - `vortexMaxRadius = 3.0f`
  - `spawnVelocity = {1, 4, 0}`, `gravity = {0, -0.5, 0}` (gentle gravity)
  - `spawnRadius = 2.0f`
- [ ] 5.2 Add a `"Whirlpool"` preset:
  - `vortexAxisDir = {0,1,0}`, `vortexTangentialStrength = 5.0f`
  - `vortexRadialStrength = -1.0f` (slight outward push)
  - `gravity = {0, -9.81, 0}`, `spawnRadius = 1.0f`
  - `blendMode = 0` (AlphaBlend, water-like)
- [ ] 5.3 Add keyboard toggle `'V'` in `runtime/demos/vfx_viewport/vfx_viewport_demo.cpp`
  to cycle vortex mode: `Off` → `Tornado` → `Whirlpool`. On each mode, apply the
  corresponding preset config values. Print mode name to ImGui overlay.

## 6. Visual Verification

- [ ] 6.1 Build runtime, then `runtime/demos/vfx_viewport/`. Fix all errors.
- [ ] 6.2 Capture Tornado preset:
  `vfx_viewport.exe --screenshot-stdout --screenshot-frame 10 --frames 15`
  View via Read tool. Verify particles spiral around the Y axis rather than
  moving in straight lines.
- [ ] 6.3 Capture with vortex disabled. Verify no regression in baseline behavior.

## 7. Tasks Update

- [ ] 7.1 Mark all checklist items `[x]` in this file as they are completed.
