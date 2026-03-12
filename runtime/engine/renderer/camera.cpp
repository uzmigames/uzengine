/// UzEngine Camera Implementation
///
/// Z-up coordinate system: +X forward, +Y right, +Z up (left-handed).
///
/// The view matrix converts Z-up world positions to Y-up clip space by
/// applying a coordinate swap before the standard DX LookTo matrix.
/// Swap: world(x,y,z) → view(y,z,x) which maps Y-right→screenX, Z-up→screenY, X-fwd→depth.

#include "camera.h"

#include <DirectXMath.h>

using namespace DirectX;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Projection Configuration
// ─────────────────────────────────────────────────────────────────────────────

void Camera::SetPerspective(float fovY, float aspectRatio, float nearPlane, float farPlane) {
    m_projMode = ProjectionMode::Perspective;
    m_fovY = fovY;
    m_aspectRatio = aspectRatio;
    m_nearPlane = nearPlane;
    m_farPlane = farPlane;
    m_projDirty = true;
}

void Camera::SetOrthographic(float width, float height, float nearPlane, float farPlane) {
    m_projMode = ProjectionMode::Orthographic;
    m_orthoWidth = width;
    m_orthoHeight = height;
    m_nearPlane = nearPlane;
    m_farPlane = farPlane;
    m_projDirty = true;
}

void Camera::SetAspectRatio(float aspectRatio) {
    if (m_projMode == ProjectionMode::Perspective) {
        m_aspectRatio = aspectRatio;
        m_projDirty = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Orientation
// ─────────────────────────────────────────────────────────────────────────────

/// Helper: convert Z-up vector to Y-up for DirectXMath.
/// Z-up(x,y,z) → Y-up(y,z,x)
static XMVECTOR ToYUp(const Vec3& v) {
    return XMVectorSet(v.y, v.z, v.x, 0.f);
}

void Camera::LookAt(const Vec3& target, const Vec3& up) {
    Vec3 forward = (target - m_position).normalize();

    // Build orientation using Y-up DirectXMath, then convert back.
    // LookToLH builds view matrix; inverse rotation = camera orientation.
    XMMATRIX viewMat = XMMatrixLookToLH(XMVectorZero(), ToYUp(forward), ToYUp(up));
    XMMATRIX camRot = XMMatrixTranspose(viewMat);

    // camRot is in Y-up space. We need to convert the quaternion to Z-up.
    // The orientation quaternion will be applied to Z-up basis vectors,
    // so we wrap with the swap: Q_zup = SwapInv * Q_yup * Swap
    // But since we reconstruct from GetForward/GetUp in UpdateView,
    // we can store the Y-up quaternion and convert in GetForward/GetRight/GetUp.
    // Actually simpler: just store the raw Y-up quat and do the swap in UpdateView.

    // Store the Y-up orientation directly — UpdateView handles the swap.
    m_orientation = Quat(XMQuaternionRotationMatrix(camRot));
    m_viewDirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Derived Vectors
// ─────────────────────────────────────────────────────────────────────────────

/// Helper: convert Y-up vector back to Z-up.
/// Y-up(x,y,z) → Z-up(z,x,y)
static Vec3 FromYUp(const Vec3& v) {
    return {v.z, v.x, v.y};
}

Vec3 Camera::GetForward() const {
    // Orientation is stored in Y-up space. Y-up forward = (0,0,1).
    Vec3 fwdYUp = m_orientation * Vec3(0, 0, 1);
    return FromYUp(fwdYUp);
}

Vec3 Camera::GetRight() const {
    // Y-up right = (1,0,0)
    Vec3 rightYUp = m_orientation * Vec3(1, 0, 0);
    return FromYUp(rightYUp);
}

Vec3 Camera::GetUp() const {
    // Y-up up = (0,1,0)
    Vec3 upYUp = m_orientation * Vec3(0, 1, 0);
    return FromYUp(upYUp);
}

// ─────────────────────────────────────────────────────────────────────────────
// Matrix Outputs
// ─────────────────────────────────────────────────────────────────────────────

const Mat4& Camera::GetView() const {
    if (m_viewDirty) {
        UpdateView();
    }
    return m_view;
}

const Mat4& Camera::GetProjection() const {
    if (m_projDirty) {
        UpdateProjection();
    }
    return m_projection;
}

Mat4 Camera::GetViewProjection() const {
    return GetView() * GetProjection();
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Updates
// ─────────────────────────────────────────────────────────────────────────────

/// Builds the view matrix: ViewProj = Swap * LookTo * Proj
///
/// The swap matrix converts Z-up world positions to Y-up positions
/// that DirectXMath's LookToLH can process. The swap and LookTo are
/// multiplied together into a single view matrix.
///
/// Swap: Z-up(x,y,z) → Y-up(y,z,x)
/// Using row-vector convention (pos * M), the swap matrix is:
///   [0 0 1 0]   // X input → Z output (depth)
///   [1 0 0 0]   // Y input → X output (screen right)
///   [0 1 0 0]   // Z input → Y output (screen up)
///   [0 0 0 1]
void Camera::UpdateView() const {
    // Orientation is stored in Y-up space
    Vec3 fwdYUp = m_orientation * Vec3(0, 0, 1);
    Vec3 upYUp  = m_orientation * Vec3(0, 1, 0);

    // Position must also be swapped to Y-up for LookTo
    XMVECTOR eyeYUp = ToYUp(m_position);
    XMMATRIX lookTo = XMMatrixLookToLH(eyeYUp, fwdYUp.toXM(), upYUp.toXM());

    // Swap matrix (row-vector): input Z-up → output Y-up
    XMMATRIX swap = XMMatrixSet(
        0, 0, 1, 0,  // Row 0: world X → Y-up Z (depth)
        1, 0, 0, 0,  // Row 1: world Y → Y-up X (screen right)
        0, 1, 0, 0,  // Row 2: world Z → Y-up Y (screen up)
        0, 0, 0, 1   // Row 3: W
    );

    // Combined: pos_zup * swap * lookTo
    XMMATRIX view = XMMatrixMultiply(swap, lookTo);
    m_view = Mat4(view);
    m_viewDirty = false;
}

/// Builds the projection matrix.
/// Standard DX left-handed perspective/ortho — works regardless of world up
/// since the view matrix already handles the coordinate system conversion.
void Camera::UpdateProjection() const {
    if (m_projMode == ProjectionMode::Perspective) {
        m_projection = Mat4::Perspective(m_fovY, m_aspectRatio, m_nearPlane, m_farPlane);
    } else {
        m_projection = Mat4::Orthographic(m_orthoWidth, m_orthoHeight, m_nearPlane, m_farPlane);
    }
    m_projDirty = false;
}

} // namespace uz
