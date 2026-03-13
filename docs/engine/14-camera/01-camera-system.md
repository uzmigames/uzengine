# Camera System

> **Status**: Planned (Phase 2)
> **Source**: `engine/components/camera_component.h`, `engine/components/spring_arm_component.h`
> **Depends on**: SceneComponent (§10), RenderWorld sync (§12)

## Purpose

Provides perspective and orthographic cameras with collision-avoiding spring arm support. The camera system feeds the `RenderWorld` via the sync point — `CameraComponent.is_active` determines which camera's view/projection matrices are used for rendering.

## CameraComponent

```cpp
/// Perspective or orthographic camera. Only one active camera per World.
/// Inherits SceneComponent (has position/rotation) but NOT PrimitiveComponent
/// (no render proxy — cameras are invisible).
class CameraComponent : public SceneComponent {
public:
    float fov = 60.0f;            // degrees, perspective only
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
    bool  is_ortho = false;
    float ortho_size = 10.0f;     // half-height, ortho only
    bool  is_active = false;      // set true to make this the active camera
    uint32_t layer_mask = RenderLayer::Default | RenderLayer::FirstPerson;

    /// Computed view-projection matrix (world_matrix inverse * projection).
    Mat4 GetViewProjection() const;
    Mat4 GetProjectionMatrix() const;
};
```

### Active Camera Resolution

Only one camera is active per World. When multiple cameras have `is_active = true`, the last one set wins. The `RenderWorldSubsystem::Sync()` reads the active camera's transform and projection to build the `ViewState` used for visibility culling and rendering.

## SpringArmComponent

Collision-avoiding arm for third-person camera follow. Attaches as a child of the player's root SceneComponent, with the CameraComponent as its child.

```
Player (entity)
├── SceneComponent (root)
│   ├── SpringArmComponent
│   │   └── CameraComponent
│   ├── SkeletalMeshComponent
│   └── ...
```

### Design

The spring arm performs a sphere sweep from its attach point along its local -Z axis (or configurable direction). If the sweep hits geometry, the arm shortens to prevent the camera from clipping through walls.

```cpp
/// Collision-avoiding arm for camera follow.
/// Sphere sweeps along arm direction each frame. If collision is detected,
/// shortens the arm to keep the camera in front of the obstacle.
class SpringArmComponent : public SceneComponent {
public:
    float target_arm_length = 300.0f;   // desired arm length (cm)
    float probe_radius = 12.0f;         // sphere sweep radius
    CollisionChannel probe_channel = CollisionChannel::Camera;

    /// Lag settings — smooth camera follow instead of snapping
    bool  enable_camera_lag = true;
    float camera_lag_speed = 10.0f;         // higher = faster follow
    bool  enable_camera_rotation_lag = false;
    float camera_rotation_lag_speed = 10.0f;

    /// Offset from attach point (local space)
    Vec3 socket_offset = {};
    Vec3 target_offset = {};

    /// If true, use control rotation instead of parent rotation.
    /// Standard for third-person: mouse rotates the arm, not the character.
    bool use_pawn_control_rotation = true;

    /// Read-only: actual arm length this frame (may be shorter due to collision).
    float GetCurrentArmLength() const;
};
```

### Collision Avoidance

Each frame during the game tick:

1. Compute desired end position: `attach_point + arm_direction * target_arm_length`
2. Sphere sweep from attach point to desired end using `probe_radius` and `probe_channel`
3. If hit: shorten arm to `hit.time * target_arm_length`
4. If no hit: restore toward `target_arm_length` (with lag if enabled)
5. Apply `socket_offset` and `target_offset`
6. Set the child CameraComponent's local position

### Camera Modes

Common camera configurations built from these components:

| Mode | Setup |
|------|-------|
| **First-person** | CameraComponent attached to head bone, no SpringArm |
| **Third-person** | SpringArmComponent → CameraComponent, `use_pawn_control_rotation = true` |
| **Top-down** | CameraComponent with fixed rotation (-90° pitch), ortho or high perspective |
| **Side-scroller** | CameraComponent at fixed Z offset, ortho, follow X/Y only |
| **Cinematic** | CameraComponent on a spline or animation track, `is_active` toggled |

## Camera Shake

Per-camera screen shake system for weapon recoil, explosions, and environmental effects.

```cpp
/// Camera shake instance. Applied additively to the active camera's transform.
/// Multiple shakes can be active simultaneously — they sum.
struct CameraShake {
    float duration = 0.5f;
    float blend_in = 0.1f;
    float blend_out = 0.2f;

    /// Oscillation parameters per axis
    struct Oscillation {
        float amplitude = 0.0f;
        float frequency = 0.0f;  // Hz
    };
    Oscillation location_x, location_y, location_z;
    Oscillation rotation_pitch, rotation_yaw, rotation_roll;

    /// Attenuation — shake reduces with distance from epicenter
    float inner_radius = 0.0f;     // full intensity within
    float outer_radius = 1000.0f;  // zero intensity beyond
    float falloff = 1.0f;          // exponent for distance falloff
};
```

## Engine Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Camera component | `UCameraComponent` | `Camera3D` | `Camera` | `CameraComponent` |
| Spring arm | `USpringArmComponent` (sweep) | None built-in | None built-in | `SpringArmComponent` (sweep) |
| Camera shake | `UCameraShakeBase` + perlin/oscillation | None built-in | Cinemachine Impulse | `CameraShake` (oscillation) |
| Active camera | `APlayerCameraManager` | `make_current()` | Tag system | `is_active` flag |
| Ortho support | Yes | Yes | Yes | Yes |
| Camera lag | `bEnableCameraLag` | Manual | Manual | `enable_camera_lag` |
