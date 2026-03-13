/// UzEngine FPS Camera Controller
///
/// First-person camera controller with WASD movement and mouse look.
/// Updates a `Camera` instance based on `InputSystem` state each frame.
///
/// # Controls
/// - **WASD**: Move forward/left/back/right relative to camera orientation
/// - **Space/Ctrl**: Move up/down (world Z axis)
/// - **Mouse**: Yaw (horizontal) and pitch (vertical) rotation
/// - **Shift**: Sprint multiplier
///
/// # Usage
/// ```cpp
/// Camera cam;
/// cam.SetPerspective(XM_PIDIV4, 16.f/9.f, 0.1f, 1000.f);
///
/// FPSCameraController controller;
/// controller.SetMoveSpeed(5.f);
///
/// // Per frame:
/// controller.Update(cam, input, deltaTime);
/// ```

#pragma once

#include "camera.h"
#include "input.h"

namespace uz {

/// First-person camera controller driven by keyboard and mouse input.
///
/// Applies yaw/pitch rotation (Euler angles internally, stored as quaternion
/// on the Camera) and WASD translation relative to the camera's orientation.
///
/// # Pitch Clamping
/// Pitch is clamped to [-89, +89] degrees to prevent gimbal lock at poles.
///
/// # Thread Safety
/// NOT thread-safe. Call `Update()` from the game/render thread only.
class FPSCameraController {
public:
    FPSCameraController() = default;

    /// Updates the camera's position and orientation from input state.
    ///
    /// # Arguments
    /// * `camera` — Camera to modify (position + orientation updated).
    /// * `input` — Input system to read key/mouse state from.
    /// * `deltaTime` — Frame delta time in seconds.
    ///
    /// # Behavior
    /// - Reads WASD/Space/Ctrl for movement (scaled by moveSpeed * deltaTime)
    /// - Reads mouse delta for yaw/pitch (scaled by mouseSensitivity)
    /// - Applies shift for sprint multiplier
    /// - Only rotates when right mouse button is held
    void Update(Camera& camera, const InputSystem& input, float deltaTime);

    /// Sets the base movement speed in world units per second.
    void SetMoveSpeed(float speed) { m_moveSpeed = speed; }

    /// Returns the current base movement speed.
    [[nodiscard]] float GetMoveSpeed() const { return m_moveSpeed; }

    /// Sets the mouse look sensitivity (radians per pixel of mouse delta).
    void SetMouseSensitivity(float sensitivity) { m_mouseSensitivity = sensitivity; }

    /// Returns the current mouse sensitivity.
    [[nodiscard]] float GetMouseSensitivity() const { return m_mouseSensitivity; }

    /// Sets the sprint speed multiplier (applied when Shift is held).
    void SetSprintMultiplier(float multiplier) { m_sprintMultiplier = multiplier; }

    /// Returns the current sprint multiplier.
    [[nodiscard]] float GetSprintMultiplier() const { return m_sprintMultiplier; }

    /// Initializes yaw/pitch from the camera's current forward direction.
    ///
    /// Call after `Camera::LookAt()` so the controller starts at the
    /// correct orientation instead of resetting to yaw=0/pitch=0.
    void InitFromCamera(const Camera& camera);

    /// Returns the current yaw angle in radians.
    [[nodiscard]] float GetYaw() const { return m_yaw; }

    /// Returns the current pitch angle in radians.
    [[nodiscard]] float GetPitch() const { return m_pitch; }

private:
    float m_moveSpeed = 5.0f;
    float m_mouseSensitivity = 0.003f;
    float m_sprintMultiplier = 3.0f;
    float m_yaw = 0.f;    ///< Horizontal rotation (radians, around world Z)
    float m_pitch = 0.f;  ///< Vertical rotation (radians, around local Y)

    static constexpr float kMaxPitch = 1.5533f;  ///< ~89 degrees in radians
};

} // namespace uz
