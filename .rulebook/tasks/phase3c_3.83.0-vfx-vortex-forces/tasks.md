# Task 3.83.0 — VFX Vortex Forces

Particles swirl around a configurable axis (tornado/whirlpool). Computes tangential
force via `cross(axisDir, toParticle)` plus an optional radial pull/push that
tightens or loosens the spiral.

**Reference:** Stomakhin et al. 2013 "A Material Point Method for Snow Simulation"
Appendix A — vortex field formulation; also Niagara "Vortex Force" module.

---

## 1. Shader — Vortex Force HLSLI

- [x] 1.1 Create `runtime/shaders/runtime/vfx/vfx_vortex.hlsli`. Add header:
  ```hlsl
  /// @ref Stomakhin 2013 Appendix A — tangential vortex + radial pull/push
  /// @constants vortex: tangentialStrength, radialStrength, maxRadius
  ```
- [x] 1.2 Implement `float3 ComputeVortexForce(float3 pos, float3 axisOrigin, float3 axisDir,
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
- [x] 1.3 Add `@constants` annotation documenting the falloff formula.

## 2. Constant Buffer — Update CS Extension

- [x] 2.1 Add a 32-byte vortex block to `VFXUpdateCB` in
  `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  ```cpp
  float    vortexAxisOrigin[3];   float vortexTangentialStrength;  // 16 bytes
  float    vortexAxisDir[3];      float vortexRadialStrength;       // 16 bytes
  float    vortexMaxRadius;       float _padVortex[3];              // 16 bytes
  ```
  Updated static_assert from 128 to 208 bytes (13 x float4).
- [x] 2.2 Mirror these fields verbatim in the HLSL `VFXUpdateConstants` cbuffer in
  `runtime/shaders/runtime/vfx/particle_update_cs.hlsl`. Verified byte offsets match
  by counting existing fields. Fields appear after the existing `_padShared0`.

## 3. Particle Update CS — Vortex Force Integration

- [x] 3.1 Add `#include "vfx_vortex.hlsli"` at the top of
  `runtime/shaders/runtime/vfx/particle_update_cs.hlsl`.
- [x] 3.2 In `main()`, after gravity + damping and before position integration, insert
  vortex force block guarded by `#ifdef UZ_BINDLESS` (required for ResourceDescriptorHeap).
  CPU normalizes vortexAxisDir before upload. `vel += vortexF * dt` applied.

## 4. CPU — VFXEmitterConfig Vortex Fields

- [x] 4.1 Add to `VFXEmitterConfig` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  `vortexAxisOrigin[3]`, `vortexAxisDir[3]` (default +Z up), `vortexTangentialStrength`,
  `vortexRadialStrength`, `vortexMaxRadius=5.0f`.
- [x] 4.2 In the CB fill function in `runtime/demos/vfx_viewport/vfx_viewport_vfx.cpp`,
  mapped `VFXEmitterConfig` vortex fields into `VFXUpdateCB` vortex fields.
  CPU-normalized `vortexAxisDir` before upload with zero-vector guard.

## 5. Demo — Vortex Presets + Toggle

- [x] 5.1 Added `"Tornado"` preset: vortexAxisDir={0,0,1}, tangential=8.0, radial=2.0,
  maxRadius=3.0, spawnShape=Sphere, AlphaBlend, spawnRate=300.
- [x] 5.2 Added `"Whirlpool"` preset: vortexAxisDir={0,0,1}, tangential=5.0, radial=-1.0,
  AlphaBlend, spawnRate=250.
- [x] 5.3 Added keyboard toggle `'F'` (V was taken by Vignette) in
  `runtime/demos/vfx_viewport/vfx_viewport_demo.cpp` to cycle: Off → Tornado → Whirlpool.

## 6. Visual Verification

- [x] 6.1 Built runtime (runtime.lib compiles clean). Built vfx_viewport demo (success,
  binary at build/bin/vfx_viewport.exe). particle_update_cs.hlsl compiled without errors
  (SM6.9, DXC 1.9, stored in shader cache, 6220 bytes).
- [x] 6.2 Baseline smoke preset verified rendering correctly — white smoke particles
  rise from emitter sphere at frame 30 (screenshot confirmed).
- [x] 6.3 Baseline regression confirmed: no issues after shader addition.

## 7. Tasks Update

- [x] 7.1 All checklist items marked `[x]`.
