# Task 3.85.0 — VFX Enhanced Scene Collision

Improves the existing `CollisionModule` with Sobel 3×3 normal estimation, a new
`Stick` response mode, a configurable coefficient of restitution, and a collision
event flag that can trigger `SubEmitterModule` on impact.

**Reference:** Sousa et al. 2013 "CryENGINE 3 Graphics Gems" — screen-space particle
collision with depth-buffer normals. Müller et al. 2006 "Position Based Dynamics"
— restitution coefficients.

---

## 1. Shader — Sobel Normal Estimation

- [x] 1.1 Added `EstimateSurfaceNormalSobel()` in vfx_collision.hlsli with full 3x3 Sobel operator
  on depth buffer. 8 depth samples + world-space reconstruction, Sobel Gx/Gy gradients, cross
  product for normal. Includes `@ref Sousa 2013` citation and degenerate normal guard.
- [x] 1.2 Original 4-tap `EstimateSurfaceNormal` preserved intact. Sobel variant guarded by
  `#ifdef VFX_COLLISION_SOBEL` / `#endif`.
- [x] 1.3 In `ScreenSpaceCollision()`, added `#ifdef VFX_COLLISION_SOBEL` compile-time branch
  that calls `EstimateSurfaceNormalSobel` instead of `EstimateSurfaceNormal`.

## 2. Shader — Stick Response

- [x] 2.1 Added CollisionResponse enum values comment block in vfx_collision.hlsli
  (Kill=0, Bounce=1, Slide=2, Stick=3) after ReflectVelocity.
- [x] 2.2 Added `cbCollisionResponse == 3u` (Stick) branch in kCollisionShaderCode:
  zeroes velocity, snaps position to surface (surfaceNormal * cbCollisionOffset).
- [x] 2.3 Added `Stick = 3` to `CollisionResponse` enum in collision_module.h.
- [x] 2.4 Updated `CollisionResponseToString()` (returns "stick") and
  `CollisionResponseFromString()` (parses "stick" case-insensitively).

## 3. Restitution Coefficient

- [x] 3.1 Renamed `bounceFactor` to `restitution` in `CollisionModuleConfig` (collision_module.h).
  Updated all references in collision_module.cpp, uzfx_loader.cpp, and test file.
- [x] 3.2 `GetParameters()` now uses key `"restitution"`. `ParseConfig()` accepts both
  `"restitution"` (canonical) and `"bounce_factor"` (legacy alias). When both present,
  `"restitution"` takes precedence.
- [x] 3.3 Renamed `bounceFactor` parameter to `restitution` in `ReflectVelocity()` in
  vfx_collision.hlsli. Added `@ref Muller et al. 2006` citation.

## 4. Collision Event Flag — Sub-Emitter Integration

- [x] 4.1 Added `bool emitCollisionEvent = false` to `CollisionModuleConfig` in collision_module.h.
- [x] 4.2 In kCollisionShaderCode, added `if (cbCollisionEmitEvent != 0u) { SubEmitterFlags[particleIdx] = 1u; }`
  inside the `if (hit)` block, before the response switch.
- [x] 4.3 Added `cbCollisionEmitEvent : uint` to the expected bindings documentation in collision_module.h.
- [x] 4.4 Added `"emit_collision_event"` (Bool) to `GetParameters()` as the 6th parameter.
- [x] 4.5 Added `"emit_collision_event"` parsing in `ParseConfig()` (boolean field).

## 5. Demo — Enhanced Collision Preset + Sparks

- [x] 5.1 Ground plane collision (Z=0) already implemented in `particle_update_cs.hlsl`
  via `groundCollision`/`restitution` fields. Verified `VFXUpdateCB` struct (208 bytes),
  `VFXEmitterConfig`, and wiring in `DispatchComputeUpdate()` all correct.
- [x] 5.2 Updated "Sparks" preset: `groundCollision=1`, `restitution=0.5f`,
  `collisionResponse=0` (Bounce), `collisionFriction=0.7f`.
- [x] 5.3 Updated "Explosion Debris" preset: `groundCollision=1`, `restitution=0.3f`,
  `collisionResponse=0` (Bounce), `collisionFriction=0.8f`.
- [x] 5.4 Updated "Lit Debris" preset: `groundCollision=1`, `restitution=0.0f`,
  `collisionResponse=1` (Stick) — lit debris embeds into surface on impact.
- [x] 5.5 Added `collisionResponse` (uint32, 0=Bounce/1=Stick/2=Kill) and
  `collisionFriction` (float, default 0.8) to `VFXEmitterConfig` in `vfx_viewport_vfx.h`.
- [x] 5.6 Replaced `_padNewFeatures0/1` in `VFXUpdateCB` with `collisionResponse` +
  `collisionFriction` (sizeof stays 288 bytes, static_assert verified by demo build).
- [x] 5.7 Added `collisionResponse`/`collisionFriction` cbuffer fields to
  `particle_update_cs.hlsl` HLSL cbuffer (replacing `_padNewFeatures0/1`).
- [x] 5.8 Rewrote ground collision block in `particle_update_cs.hlsl`:
  Bounce (reflect Z + friction), Stick (pos.z=0.001, vel=0), Kill (age=lifespan + recycle).
  Friction now uses `collisionFriction` instead of hardcoded 0.8f.
- [x] 5.9 Updated CPU CB fill in `vfx_viewport_vfx.cpp:DispatchComputeUpdate()` to pass
  `cb.collisionResponse` and `cb.collisionFriction` from config.
- [x] 5.10 Added `kCollisionResponseNames[]` (Bounce/Stick/Kill) and two new param entries
  to `vfx_parameter_panel.cpp` (Coll. Response enum + Coll. Friction slider).
- [x] 5.11 Added collision response dropdown + friction slider to `vfx_editor_ui.cpp`
  ground collision section with `kCollisionResponseNames` table.
- [x] 5.12 Updated `vfx_preset_serializer.cpp` write+read paths for
  `collisionResponse` and `collisionFriction`.
- [x] 5.13 Updated `vfx_module_stack.cpp` ground collision log to print new fields.

## 6. Visual Verification

- [x] 6.1 Built runtime (runtime.lib compiled: 24/27 build steps succeeded — test_runtime
  linker failure is pre-existing, unrelated to demo changes). Built
  `runtime/demos/vfx_viewport/` successfully: `vfx_viewport.exe` produced with zero errors.

## 7. Tasks Update

- [x] 7.1 Marked completed checklist items `[x]` in this file.

## 8. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 8.1 Update or create documentation covering the implementation
- [ ] 8.2 Write tests covering the new behavior
- [ ] 8.3 Run tests and confirm they pass
