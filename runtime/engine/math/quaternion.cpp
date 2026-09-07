/// Quat implementation — SIMD-accelerated via DirectXMath.

#include "quaternion.h"
#include "mat4.h"

namespace uz {
using namespace DirectX;

Quat::Quat(const Vec3& axis, float angle)
    : Quat(Quat(XMQuaternionRotationAxis(axis.toXM(), angle))) {}

Quat Quat::FromAxisAngle(const Vec3& axis, float angle) {
    return Quat(axis, angle);
}

Quat Quat::FromEuler(float pitch, float yaw, float roll) {
    // UE5-parity Z-up convention (matches Unreal Engine 4/5 rotation semantics):
    //   Pitch = elevation from horizon, around the Y-right axis.
    //           Pitch = -90° points up (noon/zenith), +90° points down.
    //   Yaw   = compass bearing, around the Z-up axis.
    //           Yaw = 0° faces East/+X, +90° faces North/+Y.
    //   Roll  = barrel roll, around the X-forward axis.
    //
    // Application order: Roll(X) → Pitch(Y) → Yaw(Z)  (extrinsic ZYX / intrinsic XYZ).
    // @ref Shoemake 1994 "Euler Angle Conversion" — Graphic Gems IV, ZYX order
    const XMVECTOR kX = XMVectorSet(1.F, 0.F, 0.F, 0.F);
    const XMVECTOR kY = XMVectorSet(0.F, 1.F, 0.F, 0.F);
    const XMVECTOR kZ = XMVectorSet(0.F, 0.F, 1.F, 0.F);
    XMVECTOR qRoll  = XMQuaternionRotationAxis(kX, roll);
    XMVECTOR qPitch = XMQuaternionRotationAxis(kY, pitch);
    XMVECTOR qYaw   = XMQuaternionRotationAxis(kZ, yaw);
    // DirectXMath returns Q2*Q1, applying Q1 before Q2: roll, then pitch, then yaw.
    XMVECTOR q = XMQuaternionMultiply(XMQuaternionMultiply(qRoll, qPitch), qYaw);
    return Quat(q);
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

Mat4 Quat::ToMatrix() const { return toMatrix(); }

Quat Quat::log() const {
    float v_len = std::sqrt(x * x + y * y + z * z);
    if (v_len < Math::EPSILON) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    float theta = std::acos(Math::clamp(w, -1.0f, 1.0f));
    float scale = theta / v_len;
    return {x * scale, y * scale, z * scale, 0.0f};
}

Quat Quat::exp() const {
    float v_len = std::sqrt(x * x + y * y + z * z);
    if (v_len < Math::EPSILON) {
        return Quat::Identity();
    }
    float s = std::sin(v_len) / v_len;
    return {x * s, y * s, z * s, std::cos(v_len)};
}

float Quat::angle_to(const Quat& other) const {
    float d = Math::clamp(dot(other), -1.0f, 1.0f);
    return 2.0f * std::acos(std::abs(d));
}

Vec3 Quat::get_euler() const {
    // ZYX decomposition matching FromEuler UE5 convention:
    //   Pitch = around Y-right axis  → returned as Vec3.x
    //   Yaw   = around Z-up axis     → returned as Vec3.y
    //   Roll  = around X-forward axis → returned as Vec3.z
    // @ref Shoemake 1994 "Euler Angle Conversion" — Graphic Gems IV, ZYX order
    float roll_x  = std::atan2(2.0f * (w * x + y * z), 1.0f - 2.0f * (x * x + y * y));
    float pitch_y = std::asin(Math::clamp(2.0f * (w * y - z * x), -1.0f, 1.0f));
    float yaw_z   = std::atan2(2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z));
    // Return as (pitch, yaw, roll) = (Y-rot, Z-rot, X-rot) to match parameter order of FromEuler.
    return {pitch_y, yaw_z, roll_x};
}

Vec3 Quat::get_axis() const {
    float v_len = std::sqrt(x * x + y * y + z * z);
    if (v_len < Math::EPSILON) {
        return Vec3::UnitX();
    }
    return {x / v_len, y / v_len, z / v_len};
}

float Quat::get_angle() const {
    return 2.0f * std::acos(Math::clamp(std::abs(w), 0.0f, 1.0f));
}

Quat Quat::spherical_cubic_interpolate(const Quat& b, const Quat& pre_a, const Quat& post_b, float t) const {
    // Ensure shortest-path by flipping if needed.
    Quat ba = b;
    if (dot(ba) < 0.0f) {
        ba = {-ba.x, -ba.y, -ba.z, -ba.w};
    }

    Quat pre = pre_a;
    if (dot(pre) < 0.0f) {
        pre = {-pre.x, -pre.y, -pre.z, -pre.w};
    }

    Quat post = post_b;
    if (ba.dot(post) < 0.0f) {
        post = {-post.x, -post.y, -post.z, -post.w};
    }

    // SQUAD: inner control points.
    // s_i = q_i * exp( -(log(q_i^-1 * q_{i+1}) + log(q_i^-1 * q_{i-1})) / 4 )
    Quat this_inv = inverse();
    Quat ln1 = (this_inv * ba).normalize().log();
    Quat ln2 = (this_inv * pre).normalize().log();
    Quat inner_a_q = {-(ln1.x + ln2.x) * 0.25f, -(ln1.y + ln2.y) * 0.25f,
                      -(ln1.z + ln2.z) * 0.25f, 0.0f};
    Quat s_a = *this * inner_a_q.exp();

    Quat ba_inv = ba.inverse();
    Quat ln3 = (ba_inv * post).normalize().log();
    Quat ln4 = (ba_inv * *this).normalize().log();
    Quat inner_b_q = {-(ln3.x + ln4.x) * 0.25f, -(ln3.y + ln4.y) * 0.25f,
                      -(ln3.z + ln4.z) * 0.25f, 0.0f};
    Quat s_b = ba * inner_b_q.exp();

    // SQUAD(a, b, s_a, s_b, t) = slerp(slerp(a, b, t), slerp(s_a, s_b, t), 2t(1-t))
    Quat r1 = slerp(ba, t);
    Quat r2 = s_a.slerp(s_b, t);
    return r1.slerp(r2, 2.0f * t * (1.0f - t));
}

} // namespace uz
