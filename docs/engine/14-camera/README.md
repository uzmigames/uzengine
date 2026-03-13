# Camera System — Complete Reference

> **Status**: Planned (Phase 2–3)
> **Source**: `runtime/engine/components/camera_component.h`, `runtime/engine/renderer/camera/`
> **Depends on**: SceneComponent (§10), Transform (§02), Input System (§13)

## Section Overview

The camera system handles perspective/orthographic rendering, field of view control, collision-aware spring arms, screen shakes, and gameplay camera modes. This section covers both the core CameraComponent and convenience utilities for common patterns.

| Document | Status | Topic | For... |
|----------|--------|-------|--------|
| [01-camera-system.md](01-camera-system.md) | Planned (2.0) | CameraComponent, active camera resolution, projection matrices | Core camera setup |
| [01a-camera-utilities.md](01a-camera-utilities.md) | Planned (1.5) | FOV conversion, angle-based setup, FPSCameraController | Convenience methods |
| [02-spring-arm.md](02-spring-arm.md) | Planned (2.0) | SpringArmComponent, collision avoidance, third-person follow | Spring arm behavior |
| [03-camera-modes.md](03-camera-modes.md) | Planned (3.0) | Cinematic, RTS, flight sim, first-person camera mode presets | Camera behavior templates |
| [04-camera-shake.md](04-camera-shake.md) | Planned (2.0) | Procedural camera shake, trauma curves, noise functions | Visual feedback |
| [05-post-process-volumes.md](05-post-process-volumes.md) | Planned (3.0) | Local post-processing overrides, blend volumes, priority system | Local effects |

## Quick Navigation

### I want to...

**Set up a basic camera**

1. Read [01-camera-system.md](01-camera-system.md) for `CameraComponent`
2. Use [01a-camera-utilities.md](01a-camera-utilities.md) convenience methods (`SetPerspectiveDegrees()`, `SetLookFromAngles()`)
3. Set `is_active = true` on the component

**Create a first-person camera with mouse look**

1. Read [01a-camera-utilities.md](01a-camera-utilities.md) for `FPSCameraController`
2. Use `FPSCameraController::Update()` in your game loop
3. Set move speed and mouse sensitivity as needed

**Implement third-person follow with collision avoidance**

1. Read [02-spring-arm.md](02-spring-arm.md) for `SpringArmComponent`
2. Parent a `CameraComponent` to the spring arm
3. Configure collision channels and desired socket offset

**Add screen shake to explosions**

1. Read [04-camera-shake.md](04-camera-shake.md) for trauma-based shake
2. Call `Camera::AddTrauma(impact_magnitude)`
3. Shake automatically decays per frame

**Change camera behavior for different gameplay states**

1. Read [03-camera-modes.md](03-camera-modes.md) for preset configurations
2. Switch presets at runtime based on input or game state
3. Blend between presets for smooth transitions

## Milestone Status

### Phase 2 (Planned)

**CameraComponent & Spring Arm**
- [ ] CameraComponent (perspective + orthographic)
- [ ] Active camera resolution and ViewState sync
- [ ] SpringArmComponent with collision avoidance
- [ ] Camera shake (simple and trauma-based)

### Phase 1.5 (Convenience Methods — Planned)

**Camera Utilities & Controllers**
- [ ] `Camera::SetHorizontalFOV()` (degree-based convenience)
- [ ] `Camera::SetPerspectiveDegrees()` (all-in-one setup)
- [ ] `Camera::SetLookFromAngles()` (Euler angle positioning)
- [ ] `FPSCameraController` library integration
- [ ] Update all demos to use FPSCameraController

### Phase 3 (Planned)

**Advanced Camera Systems**
- [ ] Camera mode presets (Cinematic, RTS, Flight Sim)
- [ ] Camera blend system for smooth transitions
- [ ] Post-process volumes with blend regions
- [ ] Depth of field per camera, motion blur parameters

## Rules

1. **NEVER manually convert FOV in demo code** — ALWAYS use `Camera::SetHorizontalFOV()` or `SetPerspectiveDegrees()`
2. **NEVER duplicate camera controller logic** — ALWAYS use `FPSCameraController` from the engine library
3. **NEVER hardcode camera setup** — ALWAYS use convenience methods
4. **ALWAYS test camera at different aspect ratios** — particularly ultrawide and mobile

## Implementation Notes

The camera system integrates with multiple other systems:

- **SceneComponent**: CameraComponent inherits from SceneComponent for transform hierarchy
- **RenderWorld**: Active camera is synced to ViewState for visibility culling
- **Input System**: FPSCameraController uses enhanced input for remappable controls
- **Physics**: SpringArmComponent traces collision rays to the physics system
- **Post-Processing**: Camera shake parameters feed into temporal jitter and motion vectors
