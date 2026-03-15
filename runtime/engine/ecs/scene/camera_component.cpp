/// CameraComponent implementation — view and projection matrix computation.
///
/// # Matrix Convention
/// DirectXMath uses row-vector convention: `p' = p * M`.
/// View = inverse(worldMatrix).
/// ViewProj = view * proj (left-to-right composition).

#include "camera_component.h"

#include <DirectXMath.h>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CameraComponent
// ─────────────────────────────────────────────────────────────────────────────

Mat4 CameraComponent::GetViewMatrix() const {
    // The view matrix is the inverse of the camera's world transform.
    // GetWorldMatrix() recomputes lazily if the transform is dirty.
    return GetWorldMatrix().Inverse();
}

Mat4 CameraComponent::GetProjectionMatrix() const {
    if (isOrtho) {
        // Full ortho extent from the half-height parameter:
        //   width  = orthoSize * 2 * aspectRatio
        //   height = orthoSize * 2
        const float orthoWidth  = orthoSize * 2.f * aspectRatio;
        const float orthoHeight = orthoSize * 2.f;
        return Mat4::Orthographic(orthoWidth, orthoHeight, nearPlane, farPlane);
    }

    // Mat4::Perspective expects FOV in radians; `fov` is stored in degrees.
    const float fovRadians = fov * (DirectX::XM_PI / 180.f);
    return Mat4::Perspective(fovRadians, aspectRatio, nearPlane, farPlane);
}

Mat4 CameraComponent::GetViewProjection() const {
    // Row-vector convention: transform = position * view * proj.
    // Composed left-to-right so view comes before proj.
    return GetViewMatrix() * GetProjectionMatrix();
}

} // namespace uz
