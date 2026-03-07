## 1. SpringArm Node

- [x] 1.1 `SpringArmComponent` struct: arm_length, socket_offset_z, lag_speed, rotation_lag_speed, collision_test, probe_radius
- [x] 1.2 `SpringArm::update(dt, pivot, desired_rot, physics)` — smooth arm (collision deferred)
- [ ] 1.3 Sphere-cast from pivot to ideal camera position (Jolt `PhysicsSystem::CastShape`) — deferred Phase 3 (Jolt not integrated)
- [ ] 1.4 Pull camera in if hit: `actual_dist = fraction * arm_length - probe_radius` — deferred Phase 3
- [ ] 1.5 `SpringArm` registered as node type in SceneManager — deferred

## 2. Camera Modes

- [x] 2.1 `CameraMode` enum: ThirdPerson, FirstPerson, Orbit, Cinematic
- [ ] 2.2 Third-person: drive rotation from `IA_Look` input, feed to SpringArm — deferred (input system not wired)
- [ ] 2.3 First-person: camera attached to head bone, direct rotation from look input — deferred (skeletal system needed)
- [ ] 2.4 Orbit: mouse/right-stick yaw+pitch around orbit_target, scroll for distance — deferred (input system)
- [x] 2.5 Cinematic: Catmull-Rom spline position + optional look-at target or spline

## 3. FOV Animation

- [x] 3.1 `FovSettings`: default, ads, sprint, interp_speed
- [x] 3.2 `Camera::update_fov(dt, is_aiming, is_sprinting)` — exponential lerp
- [x] 3.3 `camera.setFov(fov)` IPC overrides target FOV directly

## 4. Camera Shake

- [x] 4.1 `CameraShake` struct: magnitude, frequency, duration, elapsed
- [x] 4.2 Hash-based smooth noise with quadratic decay envelope
- [x] 4.3 Multiple shakes accumulate additively
- [x] 4.4 Shake presets in `.uzcam`: `[shake preset=land]` etc.
- [x] 4.5 `camera.addShake(preset or params)` IPC handler

## 5. Depth of Field

- [x] 5.1 `DoFParams` struct: enabled, focus_dist, focus_range, near_blur, far_blur, bokeh_radius
- [ ] 5.2 Auto-focus: center raycast per frame — deferred (needs physics raycast)
- [ ] 5.3 DoF post-process pass: `shaders/post/dof.hlsl` — deferred (HLSL shader work)
- [x] 5.4 `camera.setDoF(params)` IPC handler

## 6. `.uzcam` Preset Loader

- [x] 6.1 Parse `[mode]`, `[spring_arm]`, `[fov]`, `[dof]`, `[shake preset=X]` sections
- [x] 6.2 Load preset via `camera.loadPreset(path)` IPC
- [x] 6.3 All params editable at runtime via `camera.setSpringArm`, `camera.setFov`, etc.

## 7. Editor Camera (Viewport)

- [x] 7.1 Editor uses orbit/fly camera — NOT tied to scene Camera node
- [x] 7.2 Right-click + WASD = fly, Middle-click drag = pan, Scroll = speed
- [ ] 7.3 F key = frame selected node (camera moves to fit selection) — deferred
- [x] 7.4 Editor camera position/rotation persisted in `editor_state.json`

## 8. IPC Handlers (camera.*)

- [x] 8.1 `camera.setMode(mode)`
- [x] 8.2 `camera.setFov(fov)`
- [x] 8.3 `camera.addShake(preset or {magnitude, frequency, duration})`
- [x] 8.4 `camera.setDoF(params)`
- [x] 8.5 `camera.setSpringArm(params)`
- [x] 8.6 `camera.getState()` → position, rotation, fov, mode
- [x] 8.7 `camera.loadPreset(path)`

## 9. Testing

- [ ] 9.1 Third-person: spring arm collision test — deferred Phase 3 (Jolt)
- [x] 9.2 FOV lerps correctly between 90 and 45 when aiming
- [x] 9.3 Camera shake on explosion: correct magnitude and decay
- [ ] 9.4 DoF: objects beyond focus_dist are blurred — deferred (shader)
- [x] 9.5 Cinematic mode: camera smoothly follows spline path

## Deferred to Future Phases

- [ ] D1. SpringArm collision (1.3, 1.4, 9.1) — blocked on Jolt Physics integration (Phase 3)
- [ ] D2. Camera mode input wiring (2.2, 2.3, 2.4) — blocked on input system / skeletal system
- [ ] D3. DoF auto-focus raycast + HLSL shader (5.2, 5.3, 9.4) — needs physics raycast + shader pass
- [ ] D4. SpringArm registered as node type (1.5) — needs SceneManager node type registration
- [ ] D5. Frame-selected-node camera command (7.3) — editor feature, low priority
