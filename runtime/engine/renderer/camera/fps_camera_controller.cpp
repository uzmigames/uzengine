/// UzEngine FPS Camera Controller Implementation
///
/// Z-up coordinate system: +X forward, +Y right, +Z up.
/// Yaw rotates around world Z axis, pitch elevates from XY plane toward +Z.
///
/// All logic operates in Z-up world space. The forward direction is computed
/// from yaw/pitch Euler angles, then passed to Camera::LookAt which handles
/// the internal Y-up conversion for DirectXMath.

#include "fps_camera_controller.h"

#include <algorithm>
#include <cmath>

namespace uz {

void FPSCameraController::InitFromCamera(const Camera& camera) {
    // GetForward returns Z-up world vector
    Vec3 fwd = camera.GetForward();
    // Yaw = angle in XY plane from +X axis
    m_yaw = std::atan2(fwd.y, fwd.x);
    // Pitch = elevation from XY plane toward +Z
    m_pitch = std::asin(std::clamp(fwd.z, -1.f, 1.f));
}

void FPSCameraController::Update(Camera& camera, const InputSystem& input, float deltaTime) {
    // ── Mouse look (only when RMB held) ──────────────────────────────────────
    if (input.IsMouseDown(MouseButton::Right)) {
        float dx = input.GetMouseDeltaX();
        float dy = input.GetMouseDeltaY();

        m_yaw += dx * m_mouseSensitivity;
        m_pitch += -dy * m_mouseSensitivity; // Mouse up = pitch up
        m_pitch = std::clamp(m_pitch, -kMaxPitch, kMaxPitch);
    }

    // ── Build forward direction from Euler angles in Z-up space ──────────────
    // Yaw rotates in XY plane, pitch elevates toward +Z.
    float cosPitch = std::cos(m_pitch);
    Vec3 forward = {
        std::cos(m_yaw) * cosPitch,  // X component
        std::sin(m_yaw) * cosPitch,  // Y component
        std::sin(m_pitch)            // Z component
    };

    // Point camera at target along computed forward direction
    Vec3 pos = camera.GetPosition();
    camera.LookAt(pos + forward, Vec3::Up());

    // ── WASD movement (in Z-up world space) ──────────────────────────────────
    Vec3 moveDir = {0.f, 0.f, 0.f};

    // Horizontal forward/right (projected onto XY ground plane)
    Vec3 flatForward = Vec3(std::cos(m_yaw), std::sin(m_yaw), 0.f);
    Vec3 flatRight   = Vec3(-std::sin(m_yaw), std::cos(m_yaw), 0.f);

    if (input.IsKeyDown(Key::W)) moveDir = moveDir + flatForward;
    if (input.IsKeyDown(Key::S)) moveDir = moveDir - flatForward;
    if (input.IsKeyDown(Key::D)) moveDir = moveDir + flatRight;
    if (input.IsKeyDown(Key::A)) moveDir = moveDir - flatRight;
    if (input.IsKeyDown(Key::Space))     moveDir = moveDir + Vec3::Up();   // +Z
    if (input.IsKeyDown(Key::LeftCtrl))  moveDir = moveDir - Vec3::Up();   // -Z

    if (moveDir.length() > 0.001f) {
        moveDir = moveDir.normalize();
    }

    float speed = m_moveSpeed;
    if (input.IsKeyDown(Key::LeftShift)) {
        speed *= m_sprintMultiplier;
    }

    pos = pos + moveDir * speed * deltaTime;
    camera.SetPosition(pos);
}

} // namespace uz
