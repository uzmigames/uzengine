/// UzEngine Math Library Implementation
/// SIMD-accelerated vector and matrix operations via DirectXMath

#include "engine_math.h"

#include <algorithm>
#include <cassert>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Vec2 Implementation
// ─────────────────────────────────────────────────────────────────────────────

Vec2 Vec2::normalize() const {
    float len = length();
    if (len > 0) {
        return {x / len, y / len};
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vec3 Implementation
// ─────────────────────────────────────────────────────────────────────────────

float Vec3::dot(const Vec3& other) const {
    XMVECTOR v1 = toXM();
    XMVECTOR v2 = other.toXM();
    XMVECTOR result = XMVector3Dot(v1, v2);
    return XMVectorGetX(result);
}

Vec3 Vec3::cross(const Vec3& other) const {
    XMVECTOR v1 = toXM();
    XMVECTOR v2 = other.toXM();
    XMVECTOR result = XMVector3Cross(v1, v2);
    return Vec3(result);
}

float Vec3::length() const {
    XMVECTOR v = toXM();
    XMVECTOR result = XMVector3Length(v);
    return XMVectorGetX(result);
}

Vec3 Vec3::normalize() const {
    XMVECTOR v = toXM();
    XMVECTOR result = XMVector3Normalize(v);
    return Vec3(result);
}

Vec3 Vec3::max(const Vec3& other) const {
    return {std::max(x, other.x), std::max(y, other.y), std::max(z, other.z)};
}

Vec3 Vec3::min(const Vec3& other) const {
    return {std::min(x, other.x), std::min(y, other.y), std::min(z, other.z)};
}

// ─────────────────────────────────────────────────────────────────────────────
// Vec4 Implementation
// ─────────────────────────────────────────────────────────────────────────────

float Vec4::dot(const Vec4& other) const {
    XMVECTOR v1 = toXM();
    XMVECTOR v2 = other.toXM();
    XMVECTOR result = XMVector4Dot(v1, v2);
    return XMVectorGetX(result);
}

float Vec4::length() const {
    XMVECTOR v = toXM();
    XMVECTOR result = XMVector4Length(v);
    return XMVectorGetX(result);
}

Vec4 Vec4::normalize() const {
    XMVECTOR v = toXM();
    XMVECTOR result = XMVector4Normalize(v);
    return Vec4(result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mat4 Implementation
// ─────────────────────────────────────────────────────────────────────────────

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
    return Mat4(XMMatrixPerspectiveFovLH(fov, aspect, near_plane, far_plane));
}

Mat4 Mat4::LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    return Mat4(XMMatrixLookAtLH(eye.toXM(), target.toXM(), up.toXM()));
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

// ─────────────────────────────────────────────────────────────────────────────
// Quat Implementation
// ─────────────────────────────────────────────────────────────────────────────

Quat::Quat(const Vec3& axis, float angle)
    : Quat(Quat(XMQuaternionRotationAxis(axis.toXM(), angle))) {}

Quat Quat::FromAxisAngle(const Vec3& axis, float angle) {
    return Quat(axis, angle);
}

Quat Quat::FromEuler(float pitch, float yaw, float roll) {
    XMVECTOR qx = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
    return Quat(qx);
}

Quat Quat::FromMatrix(const Mat4& m) {
    XMVECTOR q = XMQuaternionRotationMatrix(m.toXM());
    return Quat(q);
}

Quat Quat::operator*(const Quat& other) const {
    XMVECTOR q1 = toXM();
    XMVECTOR q2 = other.toXM();
    XMVECTOR result = XMQuaternionMultiply(q1, q2);
    return Quat(result);
}

Vec3 Quat::operator*(const Vec3& v) const {
    XMVECTOR q = toXM();
    XMVECTOR vec = v.toXM();
    XMVECTOR result = XMVector3Rotate(vec, q);
    return Vec3(result);
}

Quat Quat::inverse() const {
    XMVECTOR q = toXM();
    XMVECTOR result = XMQuaternionInverse(q);
    return Quat(result);
}

Quat Quat::normalize() const {
    XMVECTOR q = toXM();
    XMVECTOR result = XMQuaternionNormalize(q);
    return Quat(result);
}

Quat Quat::slerp(const Quat& other, float t) const {
    XMVECTOR q1 = toXM();
    XMVECTOR q2 = other.toXM();
    XMVECTOR result = XMQuaternionSlerp(q1, q2, t);
    return Quat(result);
}

Quat Quat::lerp(const Quat& other, float t) const {
    XMVECTOR q1 = toXM();
    XMVECTOR q2 = other.toXM();
    XMVECTOR result = XMVectorLerp(q1, q2, t);
    return Quat(result);
}

Mat4 Quat::toMatrix() const {
    XMVECTOR q = toXM();
    XMMATRIX result = XMMatrixRotationQuaternion(q);
    return Mat4(result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Transform Implementation
// ─────────────────────────────────────────────────────────────────────────────

Transform::Transform(const Mat4& m) {
    // Decompose TRS from matrix
    XMVECTOR scale_v;
    XMVECTOR rotation_v;
    XMVECTOR translation_v;

    XMMATRIX xm = m.toXM();
    XMMatrixDecompose(&scale_v, &rotation_v, &translation_v, xm);

    position = Vec3(translation_v);
    rotation = Quat(rotation_v);
    scale = Vec3(scale_v);
}

Mat4 Transform::toMatrix() const {
    Mat4 result = Mat4::Identity();
    result = result * Mat4::Scaling(scale);
    result = result * rotation.toMatrix();
    result = result * Mat4::Translation(position);
    return result;
}

Transform Transform::FromMatrix(const Mat4& m) {
    return Transform(m);
}

Transform Transform::compose(const Transform& parent) const {
    // Local to world: world = local * parent
    Transform world;
    world.scale = scale * parent.scale;
    world.rotation = rotation * parent.rotation;
    world.position = (rotation * (parent.scale * position)) + parent.position;
    return world;
}

Transform Transform::inverse() const {
    Transform inv;
    inv.scale = {1 / scale.x, 1 / scale.y, 1 / scale.z};
    inv.rotation = rotation.inverse();
    inv.position = -(inv.rotation * (inv.scale * position));
    return inv;
}

Transform Transform::translate(const Vec3& delta) const {
    return {position + delta, rotation, scale};
}

Transform Transform::rotate(const Quat& rot) const {
    return {position, rotation * rot, scale};
}

Transform Transform::scaleBy(const Vec3& s) const {
    return {position, rotation, scale * s};
}

} // namespace uz
