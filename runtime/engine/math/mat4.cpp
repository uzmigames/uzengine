/// Mat4 implementation — SIMD-accelerated via DirectXMath.

#include "mat4.h"
#include "quaternion.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace uz {
using namespace DirectX;

Mat4 Mat4::Identity() {
    return Mat4(XMMatrixIdentity());
}

Mat4 Mat4::Zero() {
    Mat4 result;
    std::fill(result.data(), result.data() + 16, 0.0f);
    return result;
}

Mat4 Mat4::Translation(float x, float y, float z) {
    return Mat4(XMMatrixTranslation(x, y, z));
}

Mat4 Mat4::Translation(const Vec3& pos) {
    return Translation(pos.x, pos.y, pos.z);
}

Mat4 Mat4::Scaling(float x, float y, float z) {
    return Mat4(XMMatrixScaling(x, y, z));
}

Mat4 Mat4::Scaling(const Vec3& scale) {
    return Scaling(scale.x, scale.y, scale.z);
}

Mat4 Mat4::RotationX(float angle) {
    return Mat4(XMMatrixRotationX(angle));
}

Mat4 Mat4::RotationY(float angle) {
    return Mat4(XMMatrixRotationY(angle));
}

Mat4 Mat4::RotationZ(float angle) {
    return Mat4(XMMatrixRotationZ(angle));
}

Mat4 Mat4::Perspective(float fov, float aspect, float near_plane, float far_plane) {
    // Reversed-Z perspective (UE5 parity): near maps to 1.0, far maps to 0.0.
    // Source: UE5 FReversedZPerspectiveMatrix in PerspectiveMatrix.h.
    const float h = 1.0f / std::tan(fov * 0.5f);
    const float w = h / aspect;
    const float range = far_plane - near_plane;

    Mat4 result{};
    result.m._11 = w;
    result.m._22 = h;
    result.m._33 = -near_plane / range;                // Reversed-Z: maps near→1, far→0
    result.m._43 = (near_plane * far_plane) / range;   // Reversed-Z: positive offset
    result.m._34 = 1.0f;                               // Perspective divide (w = z)
    return result;
}

Mat4 Mat4::LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    return Mat4(XMMatrixLookAtLH(eye.toXM(), target.toXM(), up.toXM()));
}

Mat4 Mat4::Orthographic(float width, float height, float near_plane, float far_plane) {
    return Mat4(XMMatrixOrthographicLH(width, height, near_plane, far_plane));
}

Mat4 Mat4::operator*(const Mat4& other) const {
    XMMATRIX m1 = toXM();
    XMMATRIX m2 = other.toXM();
    XMMATRIX result = XMMatrixMultiply(m1, m2);
    return Mat4(result);
}

Vec4 Mat4::operator*(const Vec4& v) const {
    XMMATRIX m = toXM();
    XMVECTOR vec = v.toXM();
    XMVECTOR result = XMVector4Transform(vec, m);
    return Vec4(result);
}

Vec3 Mat4::operator*(const Vec3& v) const {
    XMMATRIX m = toXM();
    XMVECTOR vec = v.toXM();
    XMVECTOR result = XMVector3Transform(vec, m);
    return Vec3(result);
}

Mat4& Mat4::operator*=(const Mat4& other) {
    *this = *this * other;
    return *this;
}

Mat4 Mat4::transpose() const {
    return Mat4(XMMatrixTranspose(toXM()));
}

Mat4 Mat4::inverse() const {
    XMMATRIX xm = toXM();
    XMVECTOR det;
    XMMATRIX inv = XMMatrixInverse(&det, xm);
    return Mat4(inv);
}

float Mat4::determinant() const {
    XMMATRIX xm = toXM();
    XMVECTOR det = XMMatrixDeterminant(xm);
    return XMVectorGetX(det);
}

Vec3 Mat4::getTranslation() const {
    return {m._41, m._42, m._43};
}

/// Extracts the per-axis scale factors from this matrix.
///
/// Scale is the Euclidean length (L2 norm) of each row of the upper-left 3x3
/// rotation-scale block.  For a TRS matrix built by `Scaling * Rotation *
/// Translation`, this recovers the original scale exactly.  Shear or non-uniform
/// negative scale may produce unexpected results.
///
/// # Returns
/// `Vec3{|row0|, |row1|, |row2|}` — always non-negative.
Vec3 Mat4::getScale() const {
    Vec3 scale;
    scale.x = std::sqrt(m._11 * m._11 + m._12 * m._12 + m._13 * m._13);
    scale.y = std::sqrt(m._21 * m._21 + m._22 * m._22 + m._23 * m._23);
    scale.z = std::sqrt(m._31 * m._31 + m._32 * m._32 + m._33 * m._33);
    return scale;
}

Mat4 Mat4::translate(const Vec3& pos) const {
    return *this * Translation(pos);
}

Mat4 Mat4::scale(const Vec3& s) const {
    return *this * Scaling(s);
}

Mat4 Mat4::TRS(const Vec3& pos, const Quat& rot, const Vec3& scl) {
    return Scaling(scl) * rot.toMatrix() * Translation(pos);
}

Mat4 Mat4::TRS(const Vec3& pos, float uniformScale) {
    return Scaling(uniformScale) * Translation(pos);
}

Mat4 Mat4::TRS(const Vec3& pos, const Vec3& scl) {
    return Scaling(scl) * Translation(pos);
}

Mat4 Mat4::NormalMatrix() const {
    return inverse().transpose();
}

} // namespace uz
