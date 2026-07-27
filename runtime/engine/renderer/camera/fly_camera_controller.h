/// UzEngine Fly Camera Controller
///
/// Free-flight camera controller with WASD movement and mouse look.
/// Updates a `Camera` instance based on `InputSystem` state each frame.
///
/// # Controls
/// - **WASD**: Move forward/left/back/right relative to camera orientation
/// - **Space/Ctrl**: Move up/down (world Z axis)
/// - **Mouse**: Yaw (horizontal) and pitch (vertical) rotation
/// - **Shift**: Sprint multiplier
/// - **Scroll wheel**: Adjust movement speed
///
/// # Usage
/// ```cpp
/// Camera cam;
/// cam.SetPerspective(XM_PIDIV4, 16.f/9.f, 0.1f, 1000.f);
///
/// FlyCameraController controller;
/// controller.SetMoveSpeed(500.f);
///
/// // Per frame:
/// controller.Update(cam, input, deltaTime);
/// ```

#pragma once

#include "camera.h"
#include "input.h"

namespace uz {

/// Free-flight camera controller driven by keyboard and mouse input.
///
/// Applies yaw/pitch rotation (Euler angles internally, stored as quaternion
/// on the Camera) and WASD translation relative to the camera's orientation.
///
/// # Pitch Clamping
/// Pitch is clamped to [-89, +89] degrees to prevent gimbal lock at poles.
///
/// # Thread Safety
/// NOT thread-safe. Call `Update()` from the game/render thread only.
class FlyCameraController {
public:
    /// Slowest speed reachable by scrolling down (cm/s).
    static constexpr float kMinMoveSpeed = 50.f;
    /// Fastest speed reachable by scrolling up (cm/s).
    static constexpr float kMaxMoveSpeed = 5000.f;
    /// Speed delta applied per scroll-wheel notch (cm/s).
    static constexpr float kScrollSpeedStep = 50.f;

    FlyCameraController() = default;

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
    /// - Reads the scroll wheel to adjust moveSpeed (clamped to
    ///   [`kMinMoveSpeed`, `kMaxMoveSpeed`])
    /// - Applies shift for sprint multiplier
    /// - Only rotates when right mouse button is held
    void Update(Camera& camera, const InputSystem& input, float deltaTime);

    /// Sets the base movement speed in centimeters per second.
    ///
    /// Scene units are centimeters (see `.claude/rules/architecture.md`), so a
    /// usable free-flight speed is in the hundreds — 500 cm/s crosses the
    /// Sponza atrium (~2600 cm) in about five seconds.
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

    /// Marks the controller as driving an editor viewport.
    ///
    /// Movement itself is identical in both modes — WASD always flies, matching
    /// the Godot / Unity Scene view convention. The flag exists so editor-only
    /// tuning (currently none) can diverge without touching call sites.
    void SetEditorMode(bool enabled) { m_editorMode = enabled; }

    /// Returns true when the controller is driving an editor viewport.
    [[nodiscard]] bool IsEditorMode() const { return m_editorMode; }

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
    float m_moveSpeed = 500.f;  ///< cm/s — matches the Lua fly_camera default at scene scale
    float m_mouseSensitivity = 0.003f;
    float m_sprintMultiplier = 3.0f;
    float m_yaw = 0.f;    ///< Horizontal rotation (radians, around world Z)
    float m_pitch = 0.f;  ///< Vertical rotation (radians, around local Y)
    bool m_editorMode = false; ///< True while driving an editor viewport.

    static constexpr float kMaxPitch = 1.5533f;  ///< ~89 degrees in radians
};

} // namespace uz
