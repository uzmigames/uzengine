# Camera System — Camera Shake

> **Status**: Planned (Phase 2)
> **Source**: `engine/rendering/camera_shake.h`, `camera_shake.cpp`
> **Depends on**: CameraComponent (§14.1), TimeManager (§9.3)
> **References**: UE5 `UCameraShakeBase`, Godot camera_shake property, modern game feel

## Purpose

Camera shake adds visceral feedback to impactful events: weapon fire, explosions, impacts, and tremors. The system supports multiple simultaneous shakes (stacking), damped oscillation, distance-based falloff, and smooth blending.

## Camera Shake Overview

Camera shake is applied as an additive perturbation to the active camera's position and rotation:

```
Rendered Camera = Ideal Camera + Shake Offset
                             |
                             ├─ Location offset (X, Y, Z)
                             └─ Rotation offset (pitch, yaw, roll)
```

Multiple shakes accumulate:

```
Total Shake = Shake1 + Shake2 + Shake3 + ...
```

## CameraShake Structure

```cpp
/// Camera shake instance. Multiple can be active simultaneously (additive).
/// Uses sinusoidal oscillation (like a spring) rather than noise.
struct CameraShake {
    // ─────────────────────────────────────────────────────────────────
    // Timing
    // ─────────────────────────────────────────────────────────────────

    /// Total duration of shake (seconds)
    float duration = 0.5f;

    /// Fade-in time at start (seconds)
    /// 0 = instant, 0.1 = smooth ramp up
    float blend_in = 0.1f;

    /// Fade-out time at end (seconds)
    /// 0 = instant cut, 0.2 = smooth ramp down
    float blend_out = 0.2f;

    // ─────────────────────────────────────────────────────────────────
    // Oscillation Per Axis
    // ─────────────────────────────────────────────────────────────────

    struct Oscillation {
        /// Maximum displacement from zero (world units for location, degrees for rotation)
        float amplitude = 0.0f;

        /// Oscillation frequency in Hz (cycles per second)
        /// 1 Hz = slow shake, 10 Hz = rapid vibration
        float frequency = 5.0f;
    };

    // Location oscillations (world space offset)
    Oscillation location_x;
    Oscillation location_y;
    Oscillation location_z;

    // Rotation oscillations (euler angles, degrees)
    Oscillation rotation_pitch;
    Oscillation rotation_yaw;
    Oscillation rotation_roll;

    // ─────────────────────────────────────────────────────────────────
    // Attenuation (Distance-based falloff)
    // ─────────────────────────────────────────────────────────────────

    /// Distance from epicenter where shake has full intensity
    float inner_radius = 0.0f;

    /// Distance where shake has zero intensity
    float outer_radius = 1000.0f;

    /// Falloff exponent (2.0 = quadratic, 1.0 = linear)
    float falloff_exponent = 2.0f;
};
```

## Example Presets

### Rifle Fire (Responsive, Sharp)

```cpp
CameraShake rifle_fire = {
    .duration = 0.15f,
    .blend_in = 0.02f,
    .blend_out = 0.05f,
    .location_x = {0.5f, 8.0f},      // 0.5cm at 8 Hz
    .location_y = {1.0f, 6.0f},      // 1cm at 6 Hz
    .rotation_yaw = {1.5f, 8.0f},    // 1.5° at 8 Hz
    .rotation_pitch = {1.0f, 6.0f},  // 1° at 6 Hz
    .inner_radius = 0.0f,
    .outer_radius = 0.0f,  // No distance attenuation (always full intensity)
};
```

### Explosion (Longer, Lower Frequency)

```cpp
CameraShake explosion = {
    .duration = 1.0f,
    .blend_in = 0.2f,
    .blend_out = 0.4f,
    .location_x = {2.0f, 3.0f},      // 2cm at 3 Hz (low freq)
    .location_y = {3.0f, 2.0f},      // 3cm at 2 Hz
    .location_z = {1.0f, 2.5f},
    .rotation_yaw = {3.0f, 3.0f},    // 3° at 3 Hz
    .rotation_pitch = {2.0f, 2.5f},  // 2° at 2.5 Hz
    .inner_radius = 500.0f,          // Full intensity within 5m
    .outer_radius = 2000.0f,         // Zero intensity at 20m
    .falloff_exponent = 2.0f,        // Quadratic falloff
};
```

### Ground Tremor (Vertical Only)

```cpp
CameraShake tremor = {
    .duration = 2.0f,
    .blend_in = 0.3f,
    .blend_out = 0.5f,
    .location_z = {4.0f, 1.5f},      // Heavy vertical shake
    .rotation_pitch = {2.0f, 1.5f},  // Slight pitch oscillation
    .inner_radius = 1000.0f,
    .outer_radius = 5000.0f,
};
```

## Triggering Shakes

From C++ code:

```cpp
// Get camera shake system
CameraShakeSystem* shake_sys = GetSubsystem<CameraShakeSystem>();

// Trigger explosion near camera
Vec3 explosion_center = {100, 50, 200};
Vec3 camera_pos = GetActiveCamera()->GetWorldPosition();

// Create shake instance
CameraShake shake = explosion;
shake_sys->PlayShake(shake, explosion_center);  // Attenuated by distance
```

From Lua script:

```lua
function on_gun_fire(ctx)
    ctx.camera:play_shake({
        duration = 0.15,
        location_y = {amplitude = 0.5, frequency = 8},
        location_z = {amplitude = 1.0, frequency = 6},
        rotation_yaw = {amplitude = 1.5, frequency = 8},
    })
end
```

## Implementation: Oscillation

Each axis uses sinusoidal oscillation damped over time:

```cpp
Vec3 ComputeShakeOffset(const CameraShake& shake, float elapsed_time) {
    // Compute blend envelope (0 to 1 to 0)
    float blend = 1.0f;
    if (elapsed_time < shake.blend_in) {
        blend = elapsed_time / shake.blend_in;  // Fade in
    } else if (elapsed_time > shake.duration - shake.blend_out) {
        float fade_out_start = shake.duration - shake.blend_out;
        blend = (shake.duration - elapsed_time) / shake.blend_out;  // Fade out
    }

    // Compute sine oscillation for each axis
    Vec3 offset = {};
    float t = elapsed_time * 2.0f * M_PI;  // Convert to radians

    offset.x = shake.location_x.amplitude * std::sin(t * shake.location_x.frequency) * blend;
    offset.y = shake.location_y.amplitude * std::sin(t * shake.location_y.frequency) * blend;
    offset.z = shake.location_z.amplitude * std::sin(t * shake.location_z.frequency) * blend;

    return offset;
}

Quat ComputeShakeRotation(const CameraShake& shake, float elapsed_time) {
    // Same blend envelope
    float blend = ComputeBlend(shake, elapsed_time);
    float t = elapsed_time * 2.0f * M_PI;

    // Compute rotation as three separate oscillations
    float pitch_rad = Radians(shake.rotation_pitch.amplitude * std::sin(t * shake.rotation_pitch.frequency) * blend);
    float yaw_rad = Radians(shake.rotation_yaw.amplitude * std::sin(t * shake.rotation_yaw.frequency) * blend);
    float roll_rad = Radians(shake.rotation_roll.amplitude * std::sin(t * shake.rotation_roll.frequency) * blend);

    // Compose into single quaternion (pitch * yaw * roll)
    Quat pitch_quat = Quat::FromAxisAngle({1, 0, 0}, pitch_rad);
    Quat yaw_quat = Quat::FromAxisAngle({0, 1, 0}, yaw_rad);
    Quat roll_quat = Quat::FromAxisAngle({0, 0, 1}, roll_rad);

    return yaw_quat * pitch_quat * roll_quat;
}
```

## Distance Attenuation

If the camera is far from the shake's epicenter, reduce its intensity:

```cpp
float ComputeAttenuationFactor(const CameraShake& shake,
                               Vec3 camera_pos,
                               Vec3 shake_origin) {
    if (shake.inner_radius > shake.outer_radius) {
        return 0.0f;  // Invalid config
    }

    float distance = (camera_pos - shake_origin).Length();

    // Full intensity within inner radius
    if (distance <= shake.inner_radius) {
        return 1.0f;
    }

    // Zero intensity beyond outer radius
    if (distance >= shake.outer_radius) {
        return 0.0f;
    }

    // Linear interpolation in between
    float t = (distance - shake.inner_radius) / (shake.outer_radius - shake.inner_radius);
    // Apply falloff exponent (2.0 = quadratic)
    return std::pow(1.0f - t, shake.falloff_exponent);
}
```

## Shake Manager

The `CameraShakeManager` maintains a list of active shakes and combines them:

```cpp
class CameraShakeManager {
private:
    struct ActiveShake {
        CameraShake config;
        float elapsed_time = 0.0f;
        Vec3 origin;  // For distance-based attenuation
    };

    std::vector<ActiveShake> active_shakes;

public:
    /// Play a shake at the camera's location
    void PlayShake(const CameraShake& shake) {
        CameraComponent* cam = GetActiveCamera();
        PlayShakeAtOrigin(shake, cam->GetWorldPosition());
    }

    /// Play a shake at a specific world location (attenuated by distance)
    void PlayShakeAtOrigin(const CameraShake& shake, Vec3 origin) {
        active_shakes.push_back({
            .config = shake,
            .elapsed_time = 0.0f,
            .origin = origin,
        });
    }

    /// Update all active shakes and compute total offset
    void Update(float dt, CameraComponent* camera) {
        Vec3 total_offset = {};
        Quat total_rotation = {};

        // Remove expired shakes and accumulate offsets
        for (auto it = active_shakes.begin(); it != active_shakes.end();) {
            if (it->elapsed_time >= it->config.duration) {
                it = active_shakes.erase(it);
                continue;
            }

            float attenuation = ComputeAttenuationFactor(
                it->config,
                camera->GetWorldPosition(),
                it->origin
            );

            Vec3 shake_offset = ComputeShakeOffset(it->config, it->elapsed_time) * attenuation;
            total_offset += shake_offset;

            // Rotation accumulation is trickier; use additive quaternion blending
            Quat shake_rot = ComputeShakeRotation(it->config, it->elapsed_time);
            total_rotation = total_rotation * shake_rot;

            it->elapsed_time += dt;
            ++it;
        }

        // Apply combined shake to camera
        camera->SetShakeOffset(total_offset);
        camera->SetShakeRotation(total_rotation);
    }
};
```

## Triggering from Game Code

```cpp
// In a weapon system
void Weapon::Fire() {
    // ...render muzzle flash, play sound...

    // Apply recoil shake to camera
    CameraShakeManager* shake_mgr = GetSubsystem<CameraShakeManager>();
    shake_mgr->PlayShake(rifle_fire_shake);
}

// In a physics impact handler
void OnPhysicsImpact(const ImpactEvent& impact) {
    float impact_force = impact.velocity.Length();

    // Intensity scales with impact force
    CameraShake impact_shake = {
        .duration = 0.2f,
        .location_y = {impact_force * 0.01f, 8.0f},  // Stronger hits shake more
        .rotation_pitch = {impact_force * 0.005f, 8.0f},
    };

    CameraShakeManager* shake_mgr = GetSubsystem<CameraShakeManager>();
    shake_mgr->PlayShakeAtOrigin(impact_shake, impact.contact_point);
}
```

## Lua API

```lua
-- Play a shake at camera location
ctx.camera:play_shake({
    duration = 0.5,
    blend_in = 0.1,
    blend_out = 0.2,
    location_x = {amplitude = 0.5, frequency = 8},
    location_y = {amplitude = 1.0, frequency = 6},
    rotation_yaw = {amplitude = 1.5, frequency = 8},
})

-- Play a shake at a specific world location (with distance falloff)
ctx.camera:play_shake_at_origin({
    duration = 1.0,
    location_y = {amplitude = 3.0, frequency = 2},
    inner_radius = 500,
    outer_radius = 2000,
    falloff_exponent = 2.0,
}, explosion_position)
```

## Engine Comparison

| Feature | UE5 | Godot | Unity | **UzEngine** |
|---------|-----|-------|-------|------------|
| Shake system | `UCameraShakeBase` + perlin/oscillation | Camera jitter via script | Cinemachine Impulse | **CameraShakeManager** + oscillation |
| Multiple shakes | Via `AddCameraShake` | Manual accumulation | Impulse receiver list | **Automatic stacking** |
| Distance falloff | Via `GetFalloffMultiplier` | Manual calculation | Not built-in | **Automatic with falloff_exponent** |
| Oscillation type | Perlin noise + sine | Noise via script | Impulse curve | **Sine waves (damped spring)** |

## Rules

1. **ALWAYS use blend_in/blend_out > 0** — instant shakes feel mechanical
2. **NEVER create shakes with amplitude > 5cm** — camera becomes unreadable
3. **ALWAYS test shakes on 60+ FPS hardware** — low framerates make oscillation look stuttery
4. **NEVER stack more than 5 simultaneous shakes** — performance and readability degrade
5. **ALWAYS attenuate by distance for world events** — camera should feel world-relative, not player-relative
