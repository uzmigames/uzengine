## 1. SpringArm Node

- [ ] 1.1 `SpringArmComponent` struct: arm_length, socket_offset_z, lag_speed, rotation_lag_speed, collision_test, probe_radius
- [ ] 1.2 `SpringArm::update(dt, pivot, desired_rot, physics)` — smooth arm + collision avoidance
- [ ] 1.3 Sphere-cast from pivot to ideal camera position (Jolt `PhysicsSystem::CastShape`)
- [ ] 1.4 Pull camera in if hit: `actual_dist = fraction * arm_length - probe_radius`
- [ ] 1.5 `SpringArm` registered as node type in SceneManager

## 2. Camera Modes

- [ ] 2.1 `CameraMode` enum: ThirdPerson, FirstPerson, Orbit, Cinematic
- [ ] 2.2 Third-person: drive rotation from `IA_Look` input, feed to SpringArm
- [ ] 2.3 First-person: camera attached to head bone, direct rotation from look input
- [ ] 2.4 Orbit: mouse/right-stick yaw+pitch around orbit_target, scroll for distance
- [ ] 2.5 Cinematic: Catmull-Rom spline position + optional look-at target or spline

## 3. FOV Animation

- [ ] 3.1 `FovSettings`: default, ads, sprint, interp_speed
- [ ] 3.2 `Camera::update_fov(dt, is_aiming, is_sprinting)` — exponential lerp
- [ ] 3.3 `camera.setFov(fov)` IPC overrides target FOV directly

## 4. Camera Shake

- [ ] 4.1 `CameraShake` struct: magnitude, frequency, duration, elapsed
- [ ] 4.2 Perlin noise evaluate with exponential decay envelope
- [ ] 4.3 Multiple shakes accumulate additively
- [ ] 4.4 Shake presets in `.uzcam`: `[shake preset=land]` etc.
- [ ] 4.5 `camera.addShake(preset or params)` IPC handler

## 5. Depth of Field

- [ ] 5.1 `DoFParams` struct: enabled, focus_dist, focus_range, near_blur, far_blur, bokeh_radius
- [ ] 5.2 Auto-focus: center raycast per frame → lerp toward hit distance
- [ ] 5.3 DoF post-process pass: `shaders/post/dof.hlsl` (circle of confusion based)
- [ ] 5.4 `camera.setDoF(params)` IPC handler

## 6. `.uzcam` Preset Loader

- [ ] 6.1 Parse `[mode]`, `[spring_arm]`, `[fov]`, `[dof]`, `[shake preset=X]` sections
- [ ] 6.2 Load preset via `camera.loadPreset(path)` IPC
- [ ] 6.3 All params editable at runtime via `camera.setSpringArm`, `camera.setFov`, etc.

## 7. Editor Camera (Viewport)

- [ ] 7.1 Editor uses orbit/fly camera — NOT tied to scene Camera node
- [ ] 7.2 Right-click + WASD = fly, Middle-click drag = pan, Scroll = speed
- [ ] 7.3 F key = frame selected node (camera moves to fit selection)
- [ ] 7.4 Editor camera position/rotation persisted in `editor_state.json`

## 8. IPC Handlers (camera.*)

- [ ] 8.1 `camera.setMode(mode)`
- [ ] 8.2 `camera.setFov(fov)`
- [ ] 8.3 `camera.addShake(preset or {magnitude, frequency, duration})`
- [ ] 8.4 `camera.setDoF(params)`
- [ ] 8.5 `camera.setSpringArm(params)`
- [ ] 8.6 `camera.getState()` → position, rotation, fov, mode
- [ ] 8.7 `camera.loadPreset(path)`

## 9. Testing

- [ ] 9.1 Third-person: spring arm follows character, camera pulls in when close to wall
- [ ] 9.2 FOV lerps correctly between 90 and 45 when aiming
- [ ] 9.3 Camera shake on explosion: correct magnitude and decay
- [ ] 9.4 DoF: objects beyond focus_dist are blurred
- [ ] 9.5 Cinematic mode: camera smoothly follows spline path
