#pragma once

/// UzEngine Mat4 — 4x4 matrix wrapper over DirectXMath XMMATRIX.

#include "uz_math_types.h"

namespace uz {

struct Quat; // forward declaration

struct Mat4 {
    XMMATRIX m;

    Mat4() : m(XMMatrixIdentity()) {}
    explicit Mat4(XMMATRIX xmm) : m(xmm) {}

    Mat4 operator*(const Mat4& b) const { return Mat4(XMMatrixMultiply(m, b.m)); }

    bool operator==(const Mat4& b) const {
        for (int i = 0; i < 4; ++i) {
            if (!XMVector4Equal(m.r[i], b.m.r[i])) return false;
        }
        return true;
    }
    bool operator!=(const Mat4& b) const { return !(*this == b); }

    Vec3 TransformPoint(Vec3 p) const {
        return Vec3(XMVector3TransformCoord(p.v, m));
    }

    Vec3 TransformDirection(Vec3 d) const {
        return Vec3(XMVector3TransformNormal(d.v, m));
    }

    Vec4 TransformVec4(Vec4 v) const {
        return Vec4(XMVector4Transform(v.v, m));
    }

    Mat4 Transposed() const { return Mat4(XMMatrixTranspose(m)); }

    Mat4 Inverse() const {
        XMVECTOR det;
        return Mat4(XMMatrixInverse(&det, m));
    }

    float Determinant() const {
        return XMVectorGetX(XMMatrixDeterminant(m));
    }

    // ── Factory methods ──────────────────────────────────────────────────

    static Mat4 Identity() { return Mat4(); }

    static Mat4 Translation(Vec3 t) {
        return Mat4(XMMatrixTranslationFromVector(t.v));
    }

    static Mat4 Translation(float x, float y, float z) {
        return Mat4(XMMatrixTranslation(x, y, z));
    }

    static Mat4 Scaling(Vec3 s) {
        return Mat4(XMMatrixScalingFromVector(s.v));
    }

    static Mat4 Scaling(float x, float y, float z) {
        return Mat4(XMMatrixScaling(x, y, z));
    }

    static Mat4 Scaling(float uniform) {
        return Mat4(XMMatrixScaling(uniform, uniform, uniform));
    }

    static Mat4 RotationX(float radians) {
        return Mat4(XMMatrixRotationX(radians));
    }

    static Mat4 RotationY(float radians) {
        return Mat4(XMMatrixRotationY(radians));
    }

    static Mat4 RotationZ(float radians) {
        return Mat4(XMMatrixRotationZ(radians));
    }

    static Mat4 RotationAxis(Vec3 axis, float radians) {
        return Mat4(XMMatrixRotationAxis(axis.v, radians));
    }

    static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) {
        return Mat4(XMMatrixLookAtLH(eye.v, target.v, up.v));
    }

    static Mat4 Perspective(float fovY, float aspect, float nearZ, float farZ) {
        return Mat4(XMMatrixPerspectiveFovLH(fovY, aspect, nearZ, farZ));
    }

    static Mat4 Orthographic(float width, float height, float nearZ, float farZ) {
        return Mat4(XMMatrixOrthographicLH(width, height, nearZ, farZ));
    }

    // ── Storage conversion ───────────────────────────────────────────────

    XMFLOAT4X4 ToFloat4x4() const {
        XMFLOAT4X4 f;
        XMStoreFloat4x4(&f, m);
        return f;
    }

    static Mat4 FromFloat4x4(const XMFLOAT4X4& f) {
        return Mat4(XMLoadFloat4x4(&f));
    }
};

} // namespace uz
