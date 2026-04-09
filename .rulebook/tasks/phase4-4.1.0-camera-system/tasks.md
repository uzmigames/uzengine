## 1. SpringArm Component

- [ ] 1.1 Create `runtime/engine/ecs/components/spring_arm.h` — CSpringArm: arm_length (3.0), socket_offset (Vec3, {0, 0.5, 0}), probe_radius (0.12), enable_lag (true), lag_speed (8.0), target_entity (optional). Collision channel mask.
- [ ] 1.2 Create `runtime/engine/ecs/components/spring_arm.cpp` — Update(dt): compute desired position = target_pos - forward * arm_length + socket_offset. Sphere cast from target to desired; if hit, shorten arm. Lag smoothing: lerp current position toward desired at lag_speed * dt. Write result to TransformComponent.

## 2. Camera Modes

- [ ] 2.1 Create `runtime/engine/camera/camera_mode.h` — CameraMode enum: FreeFly, ThirdPerson, FirstPerson, Orbit, Cinematic. CameraModeBase: virtual Update(dt, input) → CameraState { position, rotation, fov }.
- [ ] 2.2 Create ThirdPersonMode — Follows entity via SpringArm. Input yaw/pitch rotate the arm pivot. Pitch clamped to [-80, 80] degrees.
- [ ] 2.3 Create OrbitMode — Orbits around target entity. Mouse drag → yaw/pitch. Scroll → distance. Min/max distance clamps.
- [ ] 2.4 Create CinematicMode — Follows CubicBezier spline path. speed (float), look_at_target (optional entity). Time-based progression along spline.

## 3. Camera Manager

- [ ] 3.1 Create `runtime/engine/camera/camera_manager.h/.cpp` — CameraManager: SetActiveCamera(entity), GetActiveCamera(). BlendToCamera(entity, blend_time): smooth transition between cameras. Manages camera stack for priority (gameplay < cinematic < cutscene).

## 4. Camera Shake

- [ ] 4.1 Create `runtime/engine/camera/camera_shake.h/.cpp` — CameraShake: magnitude (float), frequency (float), duration (float), elapsed (float). Update(dt) → offset Vec3 via Perlin noise with exponential decay envelope. ShakeManager: vector<CameraShake> active shakes, AddShake(), Update(dt) → total offset (additive accumulation). Maximum 8 concurrent shakes.

## 5. FOV Interpolation

- [ ] 5.1 Add FOV management to CameraComponent — FovSettings: default_fov (75), ads_fov (45), sprint_fov (90), interp_speed (10.0). SetTargetFov(fov) triggers smooth interpolation. Update(dt): lerp current_fov toward target.

## 6. .uzcam Preset Format

- [ ] 6.1 Create `runtime/engine/camera/camera_preset.h/.cpp` — Load/Save .uzcam JSON: { "mode": "third_person", "spring_arm": { "length": 3.0, ... }, "fov": { "default": 75, ... }, "shake_presets": { "explosion": { ... } } }.

## 7. Lua Camera API

- [ ] 7.1 Extend `runtime/engine/scripting/api/lua_camera.h/.cpp` — `uz.camera.set_mode(entity, "third_person")`, `uz.camera.set_active(entity)`, `uz.camera.shake(magnitude, frequency, duration)`, `uz.camera.set_fov(fov)`, `uz.camera.set_spring_arm(entity, { length=3, lag=true })`, `uz.camera.blend_to(entity, time)`.

## 8. Testing

- [ ] 8.1 Unit tests for SpringArm — normal positioning, collision shortening, lag smoothing.
- [ ] 8.2 Unit tests for camera modes — orbit angle calculation, cinematic spline progression.
- [ ] 8.3 Unit tests for CameraShake — decay over time, multiple shake accumulation.
- [ ] 8.4 Unit tests for FOV interpolation — smooth transition timing.
- [ ] 8.5 Unit tests for .uzcam — load/save round-trip.

## 9. Demo

- [ ] 9.1 Create `demos/camera_system/` — Third-person camera following a cube, orbit mode with mouse drag, camera shake on keypress. Headless + screenshot at multiple positions.

## 10. Documentation

- [ ] 10.1 Update `docs/engine/14-camera/` with SpringArm, modes, shake, FOV.
- [ ] 10.2 Add enhanced `uz.camera` to `docs/lua-api-reference.md`.
- [ ] 10.3 Document .uzcam format.

## 11. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 11.1 Update or create documentation covering the implementation
- [ ] 11.2 Write tests covering the new behavior
- [ ] 11.3 Run tests and confirm they pass
