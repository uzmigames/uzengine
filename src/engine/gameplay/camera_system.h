#pragma once

// camera_system.h — Game camera system: modes, FOV animation, shake, DoF, spring arm.
//
// This is the GAMEPLAY camera system (runtime). The editor fly-camera
// (camera_component.h) is separate and not affected by these settings.
//
// Thread safety: CameraSystemState lives in SceneParams and is written
// by the IPC thread, read by the render thread. Access is guarded by
// cameraSysMutex in SceneParams.

#include "engine/core/math.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace uz {

// ============================================================================
// Camera Mode
// ============================================================================

enum class CameraMode : uint8_t {
    ThirdPerson = 0,
    FirstPerson = 1,
    Orbit       = 2,
    Cinematic   = 3,
};

// ============================================================================
// FOV Settings — exponential lerp between target FOVs
// ============================================================================

struct FovSettings {
    float default_fov  = 90.0f;   // Default vertical FOV (degrees)
    float ads_fov      = 45.0f;   // Aim-down-sights FOV
    float sprint_fov   = 100.0f;  // Sprint FOV stretch
    float interp_speed = 8.0f;    // Exponential lerp speed
};

/// Compute the next FOV given current state and frame delta.
/// Returns the interpolated FOV in degrees.
[[nodiscard]] float fov_lerp(float current_fov, float target_fov,
                             float interp_speed, float dt);

// ============================================================================
// Camera Shake — Perlin noise with exponential decay
// ============================================================================

struct CameraShake {
    float magnitude = 5.0f;    // degrees
    float frequency = 20.0f;   // oscillation Hz
    float duration  = 0.5f;    // seconds
    float elapsed   = 0.0f;    // seconds elapsed

    /// Evaluate shake offset (pitch, yaw) at current elapsed time.
    /// Returns Vec2 {pitch_deg, yaw_deg}.
    [[nodiscard]] Vec2 evaluate() const;

    /// Has the shake expired?
    [[nodiscard]] bool expired() const { return elapsed >= duration; }
};

/// Simple 1D Perlin-like noise for camera shake (deterministic, fast).
[[nodiscard]] float shake_noise(float t);

/// Accumulate all active shakes, advance elapsed by dt, remove expired.
/// Returns total offset as Vec2 {pitch_deg, yaw_deg}.
Vec2 shakes_accumulate(std::vector<CameraShake>& shakes, float dt);

// ============================================================================
// Spring Arm — boom arm with lag smoothing (collision deferred to Phase 3)
// ============================================================================

struct SpringArmData {
    float arm_length         = 4.0f;   // meters (distance behind pivot)
    float socket_offset_z    = 0.8f;   // vertical offset above pivot
    float lag_speed          = 10.0f;  // position smoothing speed
    float rotation_lag_speed = 8.0f;   // rotation smoothing speed
    bool  collision_test     = true;   // sphere-cast (requires physics — Phase 3)
    float probe_radius       = 0.12f;  // sphere-cast radius
};

struct SpringArmState {
    Vec3 arm_pos  = {0, 0, 0, 0};
    Quat arm_rot  = {0, 0, 0, 1};  // identity
};

/// Update spring arm position from pivot and desired rotation.
/// Writes output camera position/rotation. Collision is a no-op until Phase 3.
void spring_arm_update(const SpringArmData& data, SpringArmState& state,
                       Vec3 pivot, Quat desired_rot, float dt,
                       Vec3& out_pos, Quat& out_rot);

// ============================================================================
// Depth of Field parameters (post-process — shader deferred to Phase 5+)
// ============================================================================

struct DoFParams {
    bool  enabled      = false;
    float focus_dist   = 5.0f;    // meters — distance to in-focus plane
    float focus_range  = 2.0f;    // meters — in-focus zone half-width
    float near_blur    = 0.0f;    // 0..1 near blur intensity
    float far_blur     = 1.0f;    // 0..1 far blur intensity
    float bokeh_radius = 8.0f;    // pixels
};

// ============================================================================
// Cinematic Spline
// ============================================================================

struct CinematicPath {
    std::vector<Vec3> position_points;
    std::vector<Vec3> look_at_points;
    float speed    = 1.0f;
    float current_t = 0.0f;
};

/// Catmull-Rom spline evaluation for cinematic camera paths.
/// t is in [0, n-1] where n = number of control points.
[[nodiscard]] Vec3 catmull_rom_eval(const std::vector<Vec3>& points, float t);

// ============================================================================
// Camera Shake Preset
// ============================================================================

struct ShakePreset {
    char name[64]   = "";
    float magnitude = 5.0f;
    float frequency = 20.0f;
    float duration  = 0.5f;
};

// ============================================================================
// Camera System State — aggregates all camera subsystem data
// ============================================================================

struct CameraSystemState {
    CameraMode  mode = CameraMode::Orbit;  // default for editor

    // FOV
    FovSettings fov_settings;
    float       current_fov = 90.0f;   // current interpolated FOV (degrees)
    bool        is_aiming    = false;
    bool        is_sprinting = false;

    // Spring Arm
    SpringArmData  spring_arm_data;
    SpringArmState spring_arm_state;

    // Shakes
    std::vector<CameraShake> active_shakes;

    // DoF
    DoFParams dof;

    // Cinematic
    CinematicPath cinematic_path;

    // Shake presets (loaded from .uzcam)
    std::vector<ShakePreset> shake_presets;
};

} // namespace uz
