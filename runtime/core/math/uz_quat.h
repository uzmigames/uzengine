#pragma once

/// UzEngine Quat — Quaternion wrapper over DirectXMath.

#include "uz_math_types.h"
#include "uz_mat4.h"

namespace uz {

struct Quat {
    XMVECTOR q;

    Quat() : q(XMQuaternionIdentity()) {}
    Quat(float x, float y, float z, float w) : q(XMVectorSet(x, y, z, w)) {}
    explicit Quat(XMVECTOR xmq) : q(xmq) {}

    float X() const { return XMVectorGetX(q); }
    float Y() const { return XMVectorGetY(q); }
    float Z() const { return XMVectorGetZ(q); }
    float W() const { return XMVectorGetW(q); }

    Quat operator*(Quat b) const { return Quat(XMQuaternionMultiply(q, b.q)); }
    Quat operator-() const { return Quat(XMQuaternionConjugate(q)); }

    bool operator==(Quat b) const { return XMVector4Equal(q, b.q); }
    bool operator!=(Quat b) const { return !XMVector4Equal(q, b.q); }

    Vec3 RotateVector(Vec3 v) const {
        return Vec3(XMVector3Rotate(v.v, q));
    }

    Quat Normalized() const { return Quat(XMQuaternionNormalize(q)); }
    Quat Conjugate() const { return Quat(XMQuaternionConjugate(q)); }
    Quat Inverse() const { return Quat(XMQuaternionInverse(q)); }
    float Dot(Quat b) const { return XMVectorGetX(XMVector4Dot(q, b.q)); }
    float Length() const { return XMVectorGetX(XMQuaternionLength(q)); }

    Mat4 ToMatrix() const { return Mat4(XMMatrixRotationQuaternion(q)); }

    Vec3 ToEuler() const {
        // Extract pitch (X), yaw (Y), roll (Z) from quaternion
        float x = X(), y = Y(), z = Z(), w = W();
        float sinP = 2.0f * (w * x - y * z);
        float pitch = (std::abs(sinP) >= 1.0f)
            ? std::copysign(XM_PIDIV2, sinP)
            : std::asin(sinP);
        float sinYcosP = 2.0f * (w * y + z * x);
        float cosYcosP = 1.0f - 2.0f * (x * x + y * y);
        float yaw = std::atan2(sinYcosP, cosYcosP);
        float sinRcosP = 2.0f * (w * z + x * y);
        float cosRcosP = 1.0f - 2.0f * (z * z + x * x);
        float roll = std::atan2(sinRcosP, cosRcosP);
        return Vec3(pitch, yaw, roll);
    }

    // ── Factory methods ──────────────────────────────────────────────────

    static Quat Identity() { return Quat(); }

    static Quat FromAxisAngle(Vec3 axis, float radians) {
        return Quat(XMQuaternionRotationAxis(axis.v, radians));
    }

    static Quat FromEuler(float pitch, float yaw, float roll) {
        return Quat(XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    }

    static Quat FromMatrix(const Mat4& m) {
        return Quat(XMQuaternionRotationMatrix(m.m));
    }

    static Quat Slerp(Quat a, Quat b, float t) {
        return Quat(XMQuaternionSlerp(a.q, b.q, t));
    }

    static Quat Lerp(Quat a, Quat b, float t) {
        return Quat(XMQuaternionNormalize(XMVectorLerp(a.q, b.q, t)));
    }

    static Quat LookRotation(Vec3 forward, Vec3 up) {
        Mat4 lookMat = Mat4(XMMatrixLookToLH(XMVectorZero(), forward.v, up.v));
        return Quat(XMQuaternionRotationMatrix(XMMatrixTranspose(lookMat.m)));
    }

    // ── Storage conversion ───────────────────────────────────────────────

    XMFLOAT4 ToFloat4() const {
        XMFLOAT4 f;
        XMStoreFloat4(&f, q);
        return f;
    }

    static Quat FromFloat4(const XMFLOAT4& f) {
        return Quat(XMLoadFloat4(&f));
    }
};

} // namespace uz
