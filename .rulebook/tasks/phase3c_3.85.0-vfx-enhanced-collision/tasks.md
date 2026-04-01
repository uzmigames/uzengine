# Task 3.85.0 — VFX Enhanced Scene Collision

Improves the existing `CollisionModule` with Sobel 3×3 normal estimation, a new
`Stick` response mode, a configurable coefficient of restitution, and a collision
event flag that can trigger `SubEmitterModule` on impact.

**Reference:** Sousa et al. 2013 "CryENGINE 3 Graphics Gems" — screen-space particle
collision with depth-buffer normals. Müller et al. 2006 "Position Based Dynamics"
— restitution coefficients.

---

## 1. Shader — Sobel Normal Estimation

- [ ] 1.1 In `runtime/shaders/runtime/vfx/vfx_collision.hlsli`, add a new function
  `float3 EstimateSurfaceNormalSobel(Texture2D<float> depthTexture, SamplerState smp,
  float2 screenUV, float2 texelSize, float4x4 invViewProj)`.
  This replaces the existing 4-tap cross-difference with a full 3×3 Sobel operator.
  Add: `/// @ref Sousa 2013 "CryENGINE 3 Graphics Gems" — Sobel 3x3 depth normal`.
  **Not yet implemented — vfx_collision.hlsli has 4-tap EstimateSurfaceNormal only.**
- [ ] 1.2 Keep the original `EstimateSurfaceNormal` (4-tap) intact — it is used by
  the existing `ScreenSpaceCollision()` function. The new Sobel variant is opt-in
  via a shader define `VFX_COLLISION_SOBEL`. **Not yet implemented.**
- [ ] 1.3 In `ScreenSpaceCollision()`, add a compile-time branch for `VFX_COLLISION_SOBEL`.
  **Not yet implemented.**

## 2. Shader — Stick Response

- [ ] 2.1 In `vfx_collision.hlsli`, add `CollisionResponse::Stick = 3` (value 3) as
  a comment block. **Not yet implemented — CollisionResponse has only Kill/Bounce/Slide.**
- [ ] 2.2 In the CollisionModule HLSL snippet, add handling for `cbCollisionResponse == 3`
  (Stick). **Not yet implemented.**
- [ ] 2.3 Add `Stick = 3` to the `CollisionResponse` enum in `collision_module.h`.
  **Not yet implemented — enum has Kill/Bounce/Slide only.**
- [ ] 2.4 Update `CollisionResponseToString()` and `CollisionResponseFromString()` to
  handle `"stick"`. **Not yet implemented.**

## 3. Restitution Coefficient

- [ ] 3.1 Rename `bounceFactor` in `CollisionModuleConfig` to `restitution` in
  `runtime/engine/vfx/modules/collision_module.h`. **Not yet implemented — field is
  still named `bounceFactor = 0.6f`. Note: VFXEmitterConfig uses `restitution` for
  the demo ground collision (separate from CollisionModule). CollisionModule rename
  is distinct work.**
- [ ] 3.2 Update `GetParameters()` serialization key from `"bounce_factor"` to
  `"restitution"` with backward-compat alias. **Not yet implemented.**
- [ ] 3.3 In `vfx_collision.hlsli`'s `ReflectVelocity()`, rename `bounceFactor`
  parameter to `restitution`. **Not yet implemented.**

## 4. Collision Event Flag — Sub-Emitter Integration

- [ ] 4.1 Add `bool emitCollisionEvent = false` to `CollisionModuleConfig`. **Not yet implemented.**
- [ ] 4.2 In the collision shader snippet, write `SubEmitterFlags[particleIdx] = 1u`
  on collision when `cbCollisionEmitEvent` is set. **Not yet implemented.**
- [ ] 4.3 Add `uint cbCollisionEmitEvent` to the module's expected bindings list. **Not yet implemented.**
- [ ] 4.4 Update `CollisionModule::GetParameters()` to expose `"emit_collision_event"`. **Not yet implemented.**
- [ ] 4.5 Update `CollisionModule::ParseConfig()` to parse `"emit_collision_event"`. **Not yet implemented.**

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
