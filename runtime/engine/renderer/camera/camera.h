/// UzEngine Camera System
///
/// Provides perspective and orthographic cameras with position + quaternion
/// orientation. Outputs a combined ViewProj matrix suitable for binding
/// to `PerFrameCB::viewProj`.
///
/// # Coordinate System
/// Left-handed, Z-up: +X forward, +Y right, +Z up (matching UE convention).
///
/// # Usage
/// ```cpp
/// Camera cam;
/// cam.SetPerspective(Camera::kDefaultFovY, 16.f/9.f, 10.f, 100000.f);
/// cam.SetPosition({0, 2, -5});
/// cam.LookAt({0, 0, 0});
///
/// // Per frame:
/// perFrameCB.viewProj = cam.GetViewProjection();
/// perFrameCB.cameraPos = Vec4(cam.GetPosition(), 0.f);
/// ```

#pragma once

#include "quaternion.h"
#include "mat4.h"
#include "rhi_types.h"

namespace uz {

/// Projection mode for the camera.
enum class ProjectionMode : uint8_t {
    Perspective,  ///< Perspective projection (3D scenes)
    Orthographic, ///< Orthographic projection (2D/UI/shadow maps)
};

/// Camera with position, quaternion orientation, and configurable projection.
///
/// The view matrix is computed from position and orientation quaternion:
/// `V = R^T * T(-position)` where R is the rotation matrix from the
/// orientation quaternion.
///
/// The projection matrix is either perspective (FOV-based) or orthographic.
/// Both use left-handed conventions with depth range [0, 1].
///
/// # Thread Safety
/// NOT thread-safe. Use from the render/game thread only.
class Camera {
public:
    Camera() = default;

    // ─────────────────────────────────────────────────────────────────────────
    // Projection Configuration
    // ─────────────────────────────────────────────────────────────────────────

    /// Configures perspective projection.
    ///
    /// # Arguments
    /// * `fovY` — Vertical field of view in radians (e.g., XM_PIDIV4 = 45 deg).
    /// * `aspectRatio` — Width / height ratio.
    /// * `nearPlane` — Near clip plane distance (must be > 0).
    /// * `farPlane` — Far clip plane distance (must be > nearPlane).
    void SetPerspective(float fovY, float aspectRatio, float nearPlane, float farPlane);

    /// Configures orthographic projection.
    ///
    /// # Arguments
    /// * `width` — View volume width in world units.
    /// * `height` — View volume height in world units.
    /// * `nearPlane` — Near clip plane distance.
    /// * `farPlane` — Far clip plane distance (must be > nearPlane).
    void SetOrthographic(float width, float height, float nearPlane, float farPlane);

    /// Updates the aspect ratio (e.g., on window resize) without changing FOV.
    ///
    /// Only affects perspective projection. No-op for orthographic.
    ///
    /// # Arguments
    /// * `aspectRatio` — Width / height ratio (e.g., 1920.0f / 1080.0f).
    void SetAspectRatio(float aspectRatio);

    // ─────────────────────────────────────────────────────────────────────────
    // Position & Orientation
    // ─────────────────────────────────────────────────────────────────────────

    /// Sets the camera's world-space position.
    void SetPosition(const Vec3& pos) { m_position = pos; m_viewDirty = true; }

    /// Returns the camera's world-space position.
    [[nodiscard]] const Vec3& GetPosition() const { return m_position; }

    /// Sets the camera's orientation from a unit quaternion.
    ///
    /// The quaternion defines the camera's rotation from the default orientation
    /// (looking down +X with +Z up).
    void SetOrientation(const Quat& orientation) { m_orientation = orientation; m_viewDirty = true; }

    /// Returns the camera's orientation quaternion.
    [[nodiscard]] const Quat& GetOrientation() const { return m_orientation; }

    /// Orients the camera to look at a target point from its current position.
    ///
    /// # Arguments
    /// * `target` — World-space point to look at.
    /// * `up` — World-space up vector (default: +Z up).
    ///
    /// # Behavior
    /// Computes a LookAt view matrix, decomposes the rotation, and stores
    /// it as the orientation quaternion.
    void LookAt(const Vec3& target, const Vec3& up = Vec3::Up());

    /// Configures perspective projection from a horizontal FOV in degrees.
    ///
    /// Internally converts horizontal FOV to vertical FOV using:
    /// `vFOV = 2 * atan(tan(hFOV/2) / aspect)`.
    ///
    /// # Arguments
    /// * `hFovDeg` — Horizontal field of view in degrees (e.g., 90).
    /// * `aspectRatio` — Width / height ratio.
    /// * `nearPlane` — Near clip distance (must be > 0).
    /// * `farPlane` — Far clip distance (must be > nearPlane).
    void SetHorizontalFOV(float hFovDeg, float aspectRatio, float nearPlane, float farPlane);

    /// Configures perspective projection from a vertical FOV in degrees.
    ///
    /// Convenience wrapper that converts degrees to radians before calling
    /// `SetPerspective()`.
    ///
    /// # Arguments
    /// * `vFovDeg` — Vertical field of view in degrees (e.g., 60).
    /// * `aspectRatio` — Width / height ratio.
    /// * `nearPlane` — Near clip distance (must be > 0).
    /// * `farPlane` — Far clip distance (must be > nearPlane).
    void SetPerspectiveDegrees(float vFovDeg, float aspectRatio, float nearPlane, float farPlane);

    /// Sets position and orients the camera from Euler angles (yaw/pitch).
    ///
    /// Computes a forward direction vector from yaw and pitch, then calls
    /// `LookAt(position + forward)`. Eliminates manual trigonometry in demos.
    ///
    /// # Arguments
    /// * `pos` — World-space camera position.
    /// * `yawDeg` — Horizontal rotation in degrees (0 = +X forward, 90 = +Y).
    /// * `pitchDeg` — Vertical rotation in degrees (0 = horizontal, +90 = up).
    void SetLookFromAngles(const Vec3& pos, float yawDeg, float pitchDeg);

    // ─────────────────────────────────────────────────────────────────────────
    // Derived Vectors
    // ─────────────────────────────────────────────────────────────────────────

    /// Returns the camera's forward direction (+X in camera space, rotated to world).
    [[nodiscard]] Vec3 GetForward() const;

    /// Returns the camera's right direction (+Y in camera space, rotated to world).
    [[nodiscard]] Vec3 GetRight() const;

    /// Returns the camera's up direction (+Z in camera space, rotated to world).
    [[nodiscard]] Vec3 GetUp() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Matrix Outputs
    // ─────────────────────────────────────────────────────────────────────────

    /// Returns the view matrix (world → camera space).
    ///
    /// Computed lazily and cached until position or orientation changes.
    [[nodiscard]] const Mat4& GetView() const;

    /// Returns the projection matrix (camera → clip space).
    ///
    /// Computed lazily and cached until projection parameters change.
    [[nodiscard]] const Mat4& GetProjection() const;

    /// Returns the combined view-projection matrix.
    ///
    /// This is the matrix that should be uploaded to `PerFrameCB::viewProj`.
    [[nodiscard]] Mat4 GetViewProjection() const;

    /// Returns the current projection mode.
    [[nodiscard]] ProjectionMode GetProjectionMode() const { return m_projMode; }

    /// Returns the vertical FOV in radians (perspective mode only).
    [[nodiscard]] float GetFovY() const { return m_fovY; }

    /// Returns the near clip plane distance.
    [[nodiscard]] float GetNearPlane() const { return m_nearPlane; }

    /// Returns the far clip plane distance.
    [[nodiscard]] float GetFarPlane() const { return m_farPlane; }

    /// Returns the aspect ratio.
    [[nodiscard]] float GetAspectRatio() const { return m_aspectRatio; }

    // ─────────────────────────────────────────────────────────────────────────
    // Render Target Override (optional — for RTT cameras)
    // ─────────────────────────────────────────────────────────────────────────

    /// Optional render target for render-to-texture cameras.
    /// When valid, the scene is rendered to this texture instead of the swapchain.
    /// Default: invalid (renders to swapchain).
    RHITextureHandle renderTarget;

private:
    /// Recomputes the view matrix from position and orientation.
    void UpdateView() const;

    /// Recomputes the projection matrix from current parameters.
    void UpdateProjection() const;

    // ── State ────────────────────────────────────────────────────────────────
    Vec3 m_position = {0.f, 0.f, 0.f};
    Quat m_orientation = Quat::Identity();

    // ── Projection parameters ────────────────────────────────────────────────
    // ── Default projection constants ────────────────────────────────────────
    static constexpr float kDefaultFovY       = 0.7854f;  ///< XM_PIDIV4 (~45 degrees)
    static constexpr float kDefaultAspect     = 16.f / 9.f;
    static constexpr float kDefaultNearPlane  = 10.0f;     ///< 10 cm (standard: 1 unit = 1 cm)
    static constexpr float kDefaultFarPlane   = 100000.f;  ///< 100000 cm = 1 km (standard)
    static constexpr float kDefaultOrthoSize  = 1000.f;  ///< 10m in cm (standard)

    ProjectionMode m_projMode = ProjectionMode::Perspective;
    float m_fovY = kDefaultFovY;
    float m_aspectRatio = kDefaultAspect;
    float m_nearPlane = kDefaultNearPlane;
    float m_farPlane = kDefaultFarPlane;
    float m_orthoWidth = kDefaultOrthoSize;
    float m_orthoHeight = kDefaultOrthoSize;

    // ── Cached matrices ──────────────────────────────────────────────────────
    mutable Mat4 m_view;
    mutable Mat4 m_projection;
    mutable bool m_viewDirty = true;
    mutable bool m_projDirty = true;
};

} // namespace uz
