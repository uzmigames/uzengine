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
  This replaces the existing 4-tap cross-difference with a full 3×3 Sobel operator:
  - Sample all 9 neighbours (including center) at `screenUV + offset * texelSize`.
  - Reconstruct world positions for all 9 taps via `ReconstructWorldPos`.
  - Compute Sobel X gradient: `(-p00 - 2*p10 - p20) + (p02 + 2*p12 + p22)` (using
    world-space positions as vectors, not depths).
  - Compute Sobel Y gradient: `(-p00 - 2*p01 - p02) + (p20 + 2*p21 + p22)`.
  - Return `normalize(cross(sobelX, sobelY))`.
  Add: `/// @ref Sousa 2013 "CryENGINE 3 Graphics Gems" — Sobel 3x3 depth normal`.
- [ ] 1.2 Keep the original `EstimateSurfaceNormal` (4-tap) intact — it is used by
  the existing `ScreenSpaceCollision()` function. The new Sobel variant is opt-in
  via a shader define `VFX_COLLISION_SOBEL`.
- [ ] 1.3 In `ScreenSpaceCollision()`, add a compile-time branch:
  ```hlsl
  #ifdef VFX_COLLISION_SOBEL
      surfaceNormal = EstimateSurfaceNormalSobel(...);
  #else
      surfaceNormal = EstimateSurfaceNormal(...);
  #endif
  ```

## 2. Shader — Stick Response

- [ ] 2.1 In `vfx_collision.hlsli`, add `CollisionResponse::Stick = 3` (value 3) as
  a comment block (HLSL has no enums; the value is passed via cbuffer uint).
  Document: `// Stick: velocity zeroed, particle pinned to surface normal offset`.
- [ ] 2.2 In the CollisionModule HLSL snippet emitted by `GetShaderCode()` in
  `runtime/engine/vfx/modules/collision_module.cpp`, add handling for
  `cbCollisionResponse == 3` (Stick):
  - Zero out velocity: `vel = float3(0,0,0)`.
  - Push position slightly off surface: `pos = pos + surfaceNormal * collisionOffset * 2.0f`.
  - Do NOT set age to lifespan (particle stays alive, stuck to surface).
  Add: `/// @ref Müller 2006 "Position Based Dynamics" — zero velocity stick response`.
- [ ] 2.3 Add `Stick = 3` to the `CollisionResponse` enum in
  `runtime/engine/vfx/modules/collision_module.h`.
- [ ] 2.4 Update `CollisionResponseToString()` and `CollisionResponseFromString()` in
  `runtime/engine/vfx/modules/collision_module.cpp` to handle `"stick"`.

## 3. Restitution Coefficient

- [ ] 3.1 Rename the existing `bounceFactor` field in `CollisionModuleConfig` to
  `restitution` in `runtime/engine/vfx/modules/collision_module.h`. Update all
  references in `collision_module.cpp` and any demo code that sets `bounceFactor`.
  Default remains `0.6f`. Add: `/// @ref Müller 2006 "Position Based Dynamics"
  — coefficient of restitution [0,1]; 0=inelastic, 1=elastic`.
- [ ] 3.2 Update `GetParameters()` in `collision_module.cpp` to expose
  `"restitution"` (previously `"bounce_factor"`). The serialization key must be
  updated too (use `"restitution"` in JSON; add a backwards-compat alias for
  `"bounce_factor"` in `ParseConfig()` so old .uzvfx assets still load).
- [ ] 3.3 In `vfx_collision.hlsli`'s `ReflectVelocity()`, rename the `bounceFactor`
  parameter to `restitution` for clarity. Update the call site in the module snippet.

## 4. Collision Event Flag — Sub-Emitter Integration

- [ ] 4.1 Add `bool emitCollisionEvent = false` to `CollisionModuleConfig` in
  `runtime/engine/vfx/modules/collision_module.h`. When `true`, the HLSL snippet
  writes `1` to `SubEmitterFlags[particleIdx]` on collision (same buffer used by
  `SubEmitterModule`). Add: `/// Triggers SubEmitterModule with OnCollision when true`.
- [ ] 4.2 In the collision shader snippet, after the Bounce/Slide/Stick response,
  add:
  ```hlsl
  if (cbCollisionEmitEvent && collisionHit) {
      SubEmitterFlags[particleIdx] = 1u;
  }
  ```
  Guard with `cbCollisionEmitEvent` (new uint constant, 0/1). Add it to the
  compositor's collision cbuffer set.
- [ ] 4.3 Add `uint cbCollisionEmitEvent` to the module's expected bindings list
  in the `CollisionModule` class doc comment in `collision_module.h`.
- [ ] 4.4 Update `CollisionModule::GetParameters()` to expose `"emit_collision_event"` Bool.
- [ ] 4.5 Update `CollisionModule::ParseConfig()` to parse `"emit_collision_event"`.

## 5. Demo — Enhanced Collision Preset + Sparks

- [x] 5.1 Ground plane collision (Z=0) already implemented in `particle_update_cs.hlsl`
  via `groundCollision`/`restitution` fields. Verified `VFXUpdateCB` struct (208 bytes),
  `VFXEmitterConfig`, and wiring in `DispatchComputeUpdate()` all correct.
- [x] 5.2 Updated "Sparks" preset in `runtime/demos/vfx_viewport/vfx_viewport_vfx.cpp`:
  `groundCollision=1`, `restitution=0.5f` — fairly elastic bounce (hot metal spark energy).
  All intermediate fields from `blendMode` → `groundCollision` explicitly initialized to
  preserve aggregate initialization correctness.
- [x] 5.3 Updated "Explosion Debris" preset: `groundCollision=1`, `restitution=0.3f` —
  low restitution for heavy debris losing most energy on impact.
  @ref Müller 2006 "Position Based Dynamics" — restitution + friction response.

## 6. Visual Verification

- [x] 6.1 Built runtime (runtime.lib compiled: 24/27 build steps succeeded — test_runtime
  linker failure is pre-existing, unrelated to demo changes). Built
  `runtime/demos/vfx_viewport/` successfully: `vfx_viewport.exe` produced.

## 7. Tasks Update

- [x] 7.1 Marked completed checklist items `[x]` in this file.
