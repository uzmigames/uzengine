// camera_system.cpp — Game camera system implementations.

#include "camera_system.h"

#include <algorithm>
#include <cmath>

namespace uz {

// ============================================================================
// FOV Interpolation
// ============================================================================

float fov_lerp(float current_fov, float target_fov,
               float interp_speed, float dt) {
    if (dt <= 0.0f) return current_fov;
    const float alpha = 1.0f - std::exp(-interp_speed * dt);
    return current_fov + (target_fov - current_fov) * alpha;
}

// ============================================================================
// Shake Noise — simple hash-based smooth noise (deterministic, no rand())
// ============================================================================

static float noise_hash(int x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return static_cast<float>(x & 0xFFFF) / 65535.0f;
}

float shake_noise(float t) {
    const int   i = static_cast<int>(std::floor(t));
    const float f = t - static_cast<float>(i);
    // Smoothstep interpolation.
    const float s = f * f * (3.0f - 2.0f * f);
    const float a = noise_hash(i)     * 2.0f - 1.0f;
    const float b = noise_hash(i + 1) * 2.0f - 1.0f;
    return a + (b - a) * s;
}

// ============================================================================
// Camera Shake
// ============================================================================

Vec2 CameraShake::evaluate() const {
    if (duration <= 0.0f) return {0.0f, 0.0f};
    const float t_norm = std::clamp(elapsed / duration, 0.0f, 1.0f);
    // Quadratic decay envelope.
    const float env = (1.0f - t_norm) * (1.0f - t_norm);
    const float t = elapsed * frequency;
    return {
        shake_noise(t + 0.0f) * magnitude * env,
        shake_noise(t + 100.0f) * magnitude * env,
    };
}

Vec2 shakes_accumulate(std::vector<CameraShake>& shakes, float dt) {
    Vec2 total = {0.0f, 0.0f};
    for (auto& s : shakes) {
        Vec2 offset = s.evaluate();
        total.x += offset.x;
        total.y += offset.y;
        s.elapsed += dt;
    }
    // Remove expired shakes.
    shakes.erase(
        std::remove_if(shakes.begin(), shakes.end(),
                       [](const CameraShake& s) { return s.expired(); }),
        shakes.end());
    return total;
}

// ============================================================================
// Spring Arm Update (collision deferred — Phase 3 Jolt integration)
// ============================================================================

void spring_arm_update(const SpringArmData& data, SpringArmState& state,
                       Vec3 pivot, Quat desired_rot, float dt,
                       Vec3& out_pos, Quat& out_rot) {
    if (dt <= 0.0f) {
        out_pos = state.arm_pos;
        out_rot = state.arm_rot;
        return;
    }

    // Smooth pivot position with exponential lag.
    const float pa = 1.0f - std::exp(-data.lag_speed * dt);
    Vec3 target_pos = {
        pivot.x,
        pivot.y + data.socket_offset_z,
        pivot.z,
        0.0f
    };
    state.arm_pos = {
        state.arm_pos.x + (target_pos.x - state.arm_pos.x) * pa,
        state.arm_pos.y + (target_pos.y - state.arm_pos.y) * pa,
        state.arm_pos.z + (target_pos.z - state.arm_pos.z) * pa,
        0.0f
    };

    // Smooth rotation with slerp.
    const float ra = 1.0f - std::exp(-data.rotation_lag_speed * dt);
    state.arm_rot = quat_slerp(state.arm_rot, desired_rot, ra);

    // Compute arm direction from rotation (back direction = -forward).
    // The arm extends "behind" the pivot along the rotated -Z axis.
    Vec3 arm_dir = quat_rotate_vec3(state.arm_rot, Vec3{0.0f, 0.0f, -1.0f, 0.0f});

    float dist = data.arm_length;

    // Collision avoidance stub — requires Jolt PhysicsWorld (Phase 3).
    // When physics is available:
    //   if (data.collision_test) {
    //       if (physics.sphere_cast(state.arm_pos, ideal_cam, data.probe_radius, hit)) {
    //           dist = hit.fraction * data.arm_length - data.probe_radius;
    //           dist = std::max(dist, 0.0f);
    //       }
    //   }

    out_pos = {
        state.arm_pos.x + arm_dir.x * dist,
        state.arm_pos.y + arm_dir.y * dist,
        state.arm_pos.z + arm_dir.z * dist,
        0.0f
    };
    out_rot = state.arm_rot;
}

// ============================================================================
// Catmull-Rom Spline Evaluation
// ============================================================================

Vec3 catmull_rom_eval(const std::vector<Vec3>& points, float t) {
    if (points.empty()) return {0, 0, 0, 0};
    if (points.size() == 1) return points[0];

    const int n = static_cast<int>(points.size());
    t = std::clamp(t, 0.0f, static_cast<float>(n - 1));

    const int i = std::min(static_cast<int>(t), n - 2);
    const float f = t - static_cast<float>(i);

    // Clamp indices for boundary control points.
    const int i0 = std::max(i - 1, 0);
    const int i1 = i;
    const int i2 = std::min(i + 1, n - 1);
    const int i3 = std::min(i + 2, n - 1);

    const Vec3& p0 = points[i0];
    const Vec3& p1 = points[i1];
    const Vec3& p2 = points[i2];
    const Vec3& p3 = points[i3];

    // Catmull-Rom basis (tau = 0.5).
    const float f2 = f * f;
    const float f3 = f2 * f;

    auto cr = [&](float a, float b, float c, float d) -> float {
        return 0.5f * ((2.0f * b) +
                       (-a + c) * f +
                       (2.0f * a - 5.0f * b + 4.0f * c - d) * f2 +
                       (-a + 3.0f * b - 3.0f * c + d) * f3);
    };

    return {
        cr(p0.x, p1.x, p2.x, p3.x),
        cr(p0.y, p1.y, p2.y, p3.y),
        cr(p0.z, p1.z, p2.z, p3.z),
        0.0f
    };
}

} // namespace uz
