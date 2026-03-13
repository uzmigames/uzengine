# Camera System — Spring Arm

> **Status**: Planned (Phase 2)
> **Source**: `engine/components/spring_arm_component.h`, `spring_arm_component.cpp`
> **Depends on**: SceneComponent (§10), Physics system (§13), collision queries
> **References**: UE5 `USpringArmComponent`, Godot `SpringArm3D`

## Purpose

The `SpringArmComponent` provides collision-aware camera mounting for third-person games. It maintains a desired offset from its parent but shortens if obstacles block the camera view. This prevents cameras from clipping through walls, doors, or other geometry.

## SpringArm Architecture

```
Player Entity
├── SceneComponent (root, at player's center)
│   ├── SkeletalMeshComponent (character model)
│   ├── CapsuleComponent (collision)
│   └── SpringArmComponent (socket at head height)
│       ├── CameraComponent (end of spring arm)
│       └── Internal collision probe (sphere)
```

The spring arm is a child SceneComponent that sweeps a sphere from its attach point along its local -Z axis. If a collision is detected during the sweep, it shortens the arm to prevent penetration.

## SpringArmComponent API

```cpp
/// Collision-aware arm for camera follow.
/// Sphere sweeps from attach socket to target position.
/// If collision detected, clamps arm length to keep camera clear of obstacles.
class SpringArmComponent : public SceneComponent {
public:
    // ─────────────────────────────────────────────────────────────────
    // Arm Configuration
    // ─────────────────────────────────────────────────────────────────

    /// Desired arm length in centimeters (world space)
    float target_arm_length = 300.0f;

    /// Radius of sphere cast for collision detection (cm)
    float probe_radius = 12.0f;

    /// Collision channel for sphere sweep
    /// Only geometry on this channel will stop the arm
    CollisionChannel probe_channel = CollisionChannel::Camera;

    /// If true, return to target_arm_length when obstacle clears
    bool restore_to_target = true;

    // ─────────────────────────────────────────────────────────────────
    // Lag and Smoothing
    // ─────────────────────────────────────────────────────────────────

    /// Enable smooth camera lag (follow behind by small amount)
    bool enable_camera_lag = true;

    /// How fast the camera catches up to target position (Hz)
    /// Higher = snappier. 5-15 is typical.
    float camera_lag_speed = 10.0f;

    /// Enable smooth rotation lag
    bool enable_camera_rotation_lag = false;

    /// How fast camera rotation catches up to target (Hz)
    float camera_rotation_lag_speed = 10.0f;

    // ─────────────────────────────────────────────────────────────────
    // Offsets
    // ─────────────────────────────────────────────────────────────────

    /// Offset from attach socket in local space (typically head height)
    /// Example: (0, 60, 0) for eye level
    Vec3 socket_offset = {0, 60, 0};

    /// Additional offset at target position (e.g., slight behind shoulder)
    /// Applied after sphere sweep
    Vec3 target_offset = {50, 0, 0};  // Offset to the right

    // ─────────────────────────────────────────────────────────────────
    // Control Rotation
    // ─────────────────────────────────────────────────────────────────

    /// If true, arm rotates with player's control yaw/pitch (mouse input)
    /// Standard for third-person: mouse rotates camera, not character
    bool use_pawn_control_rotation = true;

    /// Yaw (horizontal) rotation from input (degrees)
    float control_yaw = 0.0f;

    /// Pitch (vertical) rotation from input (degrees, clamped ±90)
    float control_pitch = 0.0f;

    // ─────────────────────────────────────────────────────────────────
    // Queries
    // ─────────────────────────────────────────────────────────────────

    /// Get current arm length (may be shorter than target due to collision)
    float GetCurrentArmLength() const;

    /// Get number of obstacles currently blocking the arm
    int32_t GetBlockingObstacleCount() const;

    /// Is the arm currently shortened by collision?
    bool IsColliding() const { return GetCurrentArmLength() < target_arm_length; }

    /// Get the actual world position of the camera socket
    Vec3 GetSocketWorldPosition() const;

    /// Get the actual world position of the camera target
    Vec3 GetTargetWorldPosition() const;
};
```

## Sphere Sweep Algorithm

Each frame during PostPhysics tick, the spring arm performs collision detection:

```cpp
void SpringArmComponent::UpdateArmLength(float dt) {
    // 1. Compute desired end position
    Vec3 socket_world = GetSocketWorldPosition();
    Vec3 arm_direction = GetWorldForwardVector() * -1.0f;  // Local -Z in world space
    Vec3 desired_target = socket_world + arm_direction * target_arm_length;

    // 2. Sphere sweep from socket to target
    PhysicsSystem* physics = GetSubsystem<PhysicsSubsystem>();
    SweepResult sweep = physics->SphereSweep(
        socket_world,
        desired_target,
        probe_radius,
        probe_channel,
        SweepIgnore::Owner  // Ignore parent character
    );

    // 3. If hit, shorten arm to hit location
    if (sweep.hit) {
        // Hit distance as fraction (0.0 = start, 1.0 = desired_target)
        float hit_time = sweep.time;
        current_arm_length = Lerp(target_arm_length, 0.0f, hit_time);
        collision_time = 0.0f;  // Just hit
    }
    // 4. If no hit, restore toward target (with lag)
    else {
        if (restore_to_target) {
            float delta = target_arm_length - current_arm_length;
            if (std::abs(delta) > 0.01f) {
                current_arm_length += delta * camera_lag_speed * dt;
            }
        }
        collision_time += dt;
    }

    // 5. Apply socket and target offsets
    Vec3 camera_socket = socket_world + socket_offset;
    Vec3 camera_target = camera_socket + arm_direction * current_arm_length + target_offset;

    // 6. If camera lag enabled, smooth the position
    if (enable_camera_lag) {
        Vec3 lag_direction = (camera_target - last_camera_pos).Normalize();
        float lag_distance = (camera_target - last_camera_pos).Length();
        float max_lag_distance = camera_lag_speed * dt * 100.0f;  // cm/s
        if (lag_distance > max_lag_distance) {
            camera_target = last_camera_pos + lag_direction * max_lag_distance;
        }
    }
    last_camera_pos = camera_target;

    // 7. Update child camera position
    if (CameraComponent* camera = GetChildCamera()) {
        camera->SetLocalPosition(camera_target - GetWorldPosition());
    }
}
```

## Control Rotation

For third-person, the camera follows the mouse, not the character body:

```cpp
// In InputComponent or PlayerController
void UpdateCameraRotation() {
    SpringArmComponent* arm = GetComponent<SpringArmComponent>();

    // Get mouse delta
    Vec2 mouse_delta = input->GetMouseDelta();

    // Update control rotation
    arm->control_yaw += mouse_delta.x * mouse_sensitivity;
    arm->control_pitch = Clamp(
        arm->control_pitch + mouse_delta.y * mouse_sensitivity,
        -90.0f, 90.0f
    );

    // Apply to arm's world rotation
    if (arm->use_pawn_control_rotation) {
        Quat yaw_rot = Quat::FromAxisAngle({0, 1, 0}, Radians(arm->control_yaw));
        Quat pitch_rot = Quat::FromAxisAngle({1, 0, 0}, Radians(arm->control_pitch));
        arm->SetWorldRotation(yaw_rot * pitch_rot);
    }
}
```

## Configuration Examples

### Third-Person Behind Camera

```cpp
spring_arm->target_arm_length = 300.0f;        // 3 meters back
spring_arm->socket_offset = {0, 70, 0};        // Eye level
spring_arm->target_offset = {0, 0, 0};         // Centered
spring_arm->camera_lag_speed = 10.0f;          // Responsive
spring_arm->use_pawn_control_rotation = true;  // Mouse controls camera
```

### Over-Shoulder Camera

```cpp
spring_arm->target_arm_length = 250.0f;
spring_arm->socket_offset = {0, 70, 0};
spring_arm->target_offset = {80, 0, 0};        // Offset to right shoulder
spring_arm->camera_lag_speed = 12.0f;
spring_arm->enable_camera_rotation_lag = true;
spring_arm->camera_rotation_lag_speed = 8.0f;
```

### Close Combat Camera

```cpp
spring_arm->target_arm_length = 150.0f;        // Closer, more immersive
spring_arm->probe_radius = 15.0f;              // Larger collision probe
spring_arm->camera_lag_speed = 15.0f;          // Faster tracking
spring_arm->enable_camera_rotation_lag = false;  // No lag for combat
```

## Collision Channel Setup

The spring arm only reacts to obstacles on its designated collision channel:

```cpp
// Physics world collision matrix setup
CollisionMatrix::SetCanCollide(
    CollisionChannel::Camera,      // Spring arm channel
    CollisionChannel::WorldStatic, // Collide with walls
    true
);
CollisionMatrix::SetCanCollide(
    CollisionChannel::Camera,
    CollisionChannel::Character,   // Don't collide with nearby NPCs
    false
);
CollisionMatrix::SetCanCollide(
    CollisionChannel::Camera,
    CollisionChannel::Trigger,     // Don't collide with triggers
    false
);
```

## Edge Cases

### Camera Inside Geometry

If the player moves so the camera starts inside a wall:

```cpp
if (sweep.start_in_geometry) {
    // Camera is inside collision
    // Set arm length to near zero to pull camera toward player
    current_arm_length = probe_radius * 0.5f;
}
```

### Extreme Angles

At extreme camera pitch, the arm direction might point upward/downward more than horizontally:

```cpp
Vec3 arm_direction = (-GetWorldForwardVector()).Normalize();
// This naturally handles pitch > 45 degrees
```

### Multiple Obstructions

Only the closest obstruction matters:

```cpp
for (int i = 0; i < sweep_results.size(); i++) {
    if (sweep_results[i].time < earliest_hit_time) {
        earliest_hit_time = sweep_results[i].time;
    }
}
current_arm_length = Lerp(target_arm_length, 0.0f, earliest_hit_time);
```

## Engine Comparison

| Feature | UE5 | Godot 4 | Unity | **UzEngine** |
|---------|-----|---------|-------|------------|
| Component | `USpringArmComponent` | `SpringArm3D` | None (usually Cinemachine) | **SpringArmComponent** |
| Collision probe | Sphere sweep | Raycast | Configurable | **Sphere sweep (adjustable radius)** |
| Lag smoothing | `CameraLagSpeed` | `drag_margin` | Manual damping | **camera_lag_speed (Hz)** |
| Control rotation | `bUsePawnControlRotation` | `rotation` property | Manual input binding | **use_pawn_control_rotation** |
| Socket offset | `SocketOffset` | `SpringLength` | Manual position | **socket_offset + target_offset** |

## Rules

1. **ALWAYS set probe_radius based on scene scale** — too small = false penetration, too large = camera pulls in early
2. **NEVER use infinite arm length** — clamp target_arm_length to reasonable bounds (100-500cm)
3. **ALWAYS filter collision channels** — camera should NOT collide with players or triggers
4. **NEVER disable collision during level transitions** — obstacles still block camera in loading screens
5. **ALWAYS use lag_speed > 0** — unlagged camera feels jittery; 8-15 Hz is typical
