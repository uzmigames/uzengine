/// CameraComponent — view and projection matrix provider.
///
/// Cameras observe the scene; they do not contribute geometry or lighting.
/// This component inherits directly from `SceneComponent` — NOT from
/// `PrimitiveComponent` — to avoid creating a render proxy for a camera.
///
/// Only one camera should be active (`isActive == true`) per world at a time.
/// The rendering system queries the active camera for view/projection matrices
/// at the start of each frame.

#pragma once

#include "scene_component.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CameraComponent
// ─────────────────────────────────────────────────────────────────────────────

/// Provides view and projection matrices for a camera in the scene.
///
/// Transform (position, rotation) is inherited from `SceneComponent`. The
/// view matrix is the inverse of the world matrix. The projection matrix is
/// computed from the camera parameters below.
///
/// # Coordinate Convention
/// The engine uses DirectXMath left-handed convention. `Mat4::Perspective`
/// and `Mat4::Orthographic` produce left-handed clip matrices.
///
/// # Examples
/// ```cpp
/// CameraComponent cam;
/// cam.SetPosition({0.f, 0.f, -5.f});
/// cam.fov         = 60.f;
/// cam.aspectRatio = 16.f / 9.f;
/// cam.isActive    = true;
///
/// Mat4 vp = cam.GetViewProjection();
/// ```
class CameraComponent : public SceneComponent {
public:
    // ── Projection Parameters ─────────────────────────────────────────────────

    /// Vertical field of view in degrees (perspective only). Default: 60°.
    ///
    /// Converted to radians internally when computing the projection matrix.
    float fov = 60.f;

    /// Near clipping plane distance in world units. Default: 0.1.
    ///
    /// Must be > 0. Objects closer than this are clipped.
    float nearPlane = 0.1f;

    /// Far clipping plane distance in world units. Default: 1000.
    ///
    /// Objects farther than this are clipped. Must be > `nearPlane`.
    float farPlane = 1000.f;

    /// Viewport width / height ratio. Default: 16:9.
    ///
    /// Typically set to `(float)viewportWidth / viewportHeight` each frame.
    float aspectRatio = 16.f / 9.f;

    // ── Orthographic Parameters ───────────────────────────────────────────────

    /// When true, use orthographic projection instead of perspective.
    bool isOrtho = false;

    /// Half-height of the orthographic view volume in world units. Default: 10.
    ///
    /// Full orthographic height = `orthoSize * 2`. Width = `orthoSize * 2 * aspectRatio`.
    /// Only used when `isOrtho` is true.
    float orthoSize = 10.f;

    // ── Activation ───────────────────────────────────────────────────────────

    /// True if this is the currently active camera for the render pass.
    ///
    /// The rendering system uses the first active camera it finds. Only one
    /// camera should be active at a time per world.
    bool isActive = false;

    /// When true, the renderer uses `lookTarget` instead of the quaternion
    /// rotation to orient the camera. Set by `uz.camera.look_at()`.
    bool hasLookTarget = false;

    /// World-space look-at target point. Used when `hasLookTarget` is true.
    /// Avoids Z-up ↔ Y-up quaternion conversion issues — the Camera::LookAt
    /// method handles the coordinate system conversion internally.
    Vec3 lookTarget{0.f, 0.f, 0.f};

    // ── Matrix Queries ────────────────────────────────────────────────────────

    /// Returns the view matrix (inverse of the world transform).
    ///
    /// Recomputes the world matrix if dirty, then inverts it. Cost is
    /// O(1) when the transform has not changed since the last call.
    ///
    /// # Returns
    /// Left-handed view matrix. Result is undefined if the camera has a
    /// singular world matrix (e.g., zero scale).
    [[nodiscard]] Mat4 GetViewMatrix() const;

    /// Returns the projection matrix based on the current camera parameters.
    ///
    /// Uses `Mat4::Perspective()` for perspective mode (left-handed, FOV in
    /// radians converted from `fov` in degrees). Uses `Mat4::Orthographic()`
    /// for orthographic mode (width = `orthoSize * 2 * aspectRatio`,
    /// height = `orthoSize * 2`).
    ///
    /// # Returns
    /// Left-handed clip-space projection matrix.
    [[nodiscard]] Mat4 GetProjectionMatrix() const;

    /// Returns the combined view-projection matrix (view * projection).
    ///
    /// Equivalent to `GetViewMatrix() * GetProjectionMatrix()`.
    /// DirectXMath uses row-vector convention: `p' = p * M`, so the
    /// multiplication order is view first, then projection.
    ///
    /// # Returns
    /// Combined view-projection matrix for use in MVP transforms.
    [[nodiscard]] Mat4 GetViewProjection() const;
};

} // namespace uz
