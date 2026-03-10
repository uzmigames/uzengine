#pragma once

/// UzEngine Vector & Scalar Math Types
///
/// SIMD-optimized wrapper types over DirectX Math (XMVECTOR).
/// Provides Vec2, Vec3, Vec4 with operator overloads for clean algebra.
///
/// # Design
/// - **Hardware-accelerated**: All operations use SSE/SSE2 intrinsics via DirectXMath
/// - **Aligned storage**: XMVECTOR is 16-byte aligned (SIMD-friendly)
/// - **Thin wrappers**: Minimal overhead, inline operators
/// - **Right-handed**: Standard graphics convention (+X right, +Y up, +Z out)
///
/// # Example
/// ```cpp
/// Vec3 pos = Vec3(1.0f, 2.0f, 3.0f);
/// Vec3 dir = (target - pos).Normalized();
/// float dist = (pos - camera).Length();
/// Vec3 lerp = Lerp(start, end, 0.5f);
/// ```

#include <DirectXMath.h>
#include <cmath>

using namespace DirectX;

namespace uz {

// ──────────────────────────────────────────────────────────────────────────────
/// Vec2 — 2D vector (x, y)
///
/// Typically used for:
/// - Texture coordinates (UV)
/// - 2D positions and directions
/// - Screen space coordinates
// ──────────────────────────────────────────────────────────────────────────────

struct Vec2 {
    XMVECTOR v;  ///< Internal SIMD vector (16-byte aligned)

    /// Default constructor: zero vector.
    Vec2() : v(XMVectorZero()) {}

    /// Construct from x, y components.
    Vec2(float x, float y) : v(XMVectorSet(x, y, 0.0f, 0.0f)) {}

    /// Construct from raw XMVECTOR (internal use).
    explicit Vec2(XMVECTOR xmv) : v(xmv) {}

    /// Broadcast single scalar to all components.
    explicit Vec2(float scalar) : v(XMVectorReplicate(scalar)) {}

    /// Get X component.
    float X() const { return XMVectorGetX(v); }

    /// Get Y component.
    float Y() const { return XMVectorGetY(v); }

    Vec2 operator+(Vec2 b) const { return Vec2(XMVectorAdd(v, b.v)); }
    Vec2 operator-(Vec2 b) const { return Vec2(XMVectorSubtract(v, b.v)); }
    Vec2 operator*(Vec2 b) const { return Vec2(XMVectorMultiply(v, b.v)); }
    Vec2 operator*(float s) const { return Vec2(XMVectorScale(v, s)); }
    Vec2 operator/(float s) const { return Vec2(XMVectorScale(v, 1.0f / s)); }
    Vec2 operator-() const { return Vec2(XMVectorNegate(v)); }

    bool operator==(Vec2 b) const { return XMVector2Equal(v, b.v); }
    bool operator!=(Vec2 b) const { return !XMVector2Equal(v, b.v); }

    float Length() const { return XMVectorGetX(XMVector2Length(v)); }
    float LengthSq() const { return XMVectorGetX(XMVector2LengthSq(v)); }
    Vec2 Normalized() const { return Vec2(XMVector2Normalize(v)); }
    float Dot(Vec2 b) const { return XMVectorGetX(XMVector2Dot(v, b.v)); }

    static Vec2 Zero() { return Vec2(); }
    static Vec2 One() { return Vec2(1.0f, 1.0f); }
    static Vec2 UnitX() { return Vec2(1.0f, 0.0f); }
    static Vec2 UnitY() { return Vec2(0.0f, 1.0f); }

    static Vec2 Lerp(Vec2 a, Vec2 b, float t) {
        return Vec2(XMVectorLerp(a.v, b.v, t));
    }

    XMFLOAT2 ToFloat2() const {
        XMFLOAT2 f;
        XMStoreFloat2(&f, v);
        return f;
    }

    static Vec2 FromFloat2(const XMFLOAT2& f) {
        return Vec2(XMLoadFloat2(&f));
    }
};

inline Vec2 operator*(float s, Vec2 v) { return v * s; }

// ── Vec3 ─────────────────────────────────────────────────────────────────────

struct Vec3 {
    XMVECTOR v;

    Vec3() : v(XMVectorZero()) {}
    Vec3(float x, float y, float z) : v(XMVectorSet(x, y, z, 0.0f)) {}
    explicit Vec3(XMVECTOR xmv) : v(xmv) {}
    explicit Vec3(float scalar) : v(XMVectorReplicate(scalar)) {}

    float X() const { return XMVectorGetX(v); }
    float Y() const { return XMVectorGetY(v); }
    float Z() const { return XMVectorGetZ(v); }

    Vec3 operator+(Vec3 b) const { return Vec3(XMVectorAdd(v, b.v)); }
    Vec3 operator-(Vec3 b) const { return Vec3(XMVectorSubtract(v, b.v)); }
    Vec3 operator*(Vec3 b) const { return Vec3(XMVectorMultiply(v, b.v)); }
    Vec3 operator*(float s) const { return Vec3(XMVectorScale(v, s)); }
    Vec3 operator/(float s) const { return Vec3(XMVectorScale(v, 1.0f / s)); }
    Vec3 operator-() const { return Vec3(XMVectorNegate(v)); }

    bool operator==(Vec3 b) const { return XMVector3Equal(v, b.v); }
    bool operator!=(Vec3 b) const { return !XMVector3Equal(v, b.v); }

    float Length() const { return XMVectorGetX(XMVector3Length(v)); }
    float LengthSq() const { return XMVectorGetX(XMVector3LengthSq(v)); }
    Vec3 Normalized() const { return Vec3(XMVector3Normalize(v)); }
    float Dot(Vec3 b) const { return XMVectorGetX(XMVector3Dot(v, b.v)); }
    Vec3 Cross(Vec3 b) const { return Vec3(XMVector3Cross(v, b.v)); }

    static Vec3 Zero() { return Vec3(); }
    static Vec3 One() { return Vec3(1.0f, 1.0f, 1.0f); }
    static Vec3 UnitX() { return Vec3(1.0f, 0.0f, 0.0f); }
    static Vec3 UnitY() { return Vec3(0.0f, 1.0f, 0.0f); }
    static Vec3 UnitZ() { return Vec3(0.0f, 0.0f, 1.0f); }
    static Vec3 Up() { return UnitY(); }
    static Vec3 Forward() { return Vec3(0.0f, 0.0f, 1.0f); }
    static Vec3 Right() { return UnitX(); }

    static Vec3 Lerp(Vec3 a, Vec3 b, float t) {
        return Vec3(XMVectorLerp(a.v, b.v, t));
    }

    static Vec3 Min(Vec3 a, Vec3 b) { return Vec3(XMVectorMin(a.v, b.v)); }
    static Vec3 Max(Vec3 a, Vec3 b) { return Vec3(XMVectorMax(a.v, b.v)); }

    XMFLOAT3 ToFloat3() const {
        XMFLOAT3 f;
        XMStoreFloat3(&f, v);
        return f;
    }

    static Vec3 FromFloat3(const XMFLOAT3& f) {
        return Vec3(XMLoadFloat3(&f));
    }
};

inline Vec3 operator*(float s, Vec3 v) { return v * s; }

// ── Vec4 ─────────────────────────────────────────────────────────────────────

struct Vec4 {
    XMVECTOR v;

    Vec4() : v(XMVectorZero()) {}
    Vec4(float x, float y, float z, float w) : v(XMVectorSet(x, y, z, w)) {}
    explicit Vec4(XMVECTOR xmv) : v(xmv) {}
    explicit Vec4(float scalar) : v(XMVectorReplicate(scalar)) {}
    Vec4(Vec3 xyz, float w) : v(XMVectorSetW(xyz.v, w)) {}

    float X() const { return XMVectorGetX(v); }
    float Y() const { return XMVectorGetY(v); }
    float Z() const { return XMVectorGetZ(v); }
    float W() const { return XMVectorGetW(v); }

    Vec4 operator+(Vec4 b) const { return Vec4(XMVectorAdd(v, b.v)); }
    Vec4 operator-(Vec4 b) const { return Vec4(XMVectorSubtract(v, b.v)); }
    Vec4 operator*(Vec4 b) const { return Vec4(XMVectorMultiply(v, b.v)); }
    Vec4 operator*(float s) const { return Vec4(XMVectorScale(v, s)); }
    Vec4 operator/(float s) const { return Vec4(XMVectorScale(v, 1.0f / s)); }
    Vec4 operator-() const { return Vec4(XMVectorNegate(v)); }

    bool operator==(Vec4 b) const { return XMVector4Equal(v, b.v); }
    bool operator!=(Vec4 b) const { return !XMVector4Equal(v, b.v); }

    float Length() const { return XMVectorGetX(XMVector4Length(v)); }
    float LengthSq() const { return XMVectorGetX(XMVector4LengthSq(v)); }
    Vec4 Normalized() const { return Vec4(XMVector4Normalize(v)); }
    float Dot(Vec4 b) const { return XMVectorGetX(XMVector4Dot(v, b.v)); }

    Vec3 XYZ() const { return Vec3(v); }

    static Vec4 Zero() { return Vec4(); }
    static Vec4 One() { return Vec4(1.0f, 1.0f, 1.0f, 1.0f); }

    static Vec4 Lerp(Vec4 a, Vec4 b, float t) {
        return Vec4(XMVectorLerp(a.v, b.v, t));
    }

    XMFLOAT4 ToFloat4() const {
        XMFLOAT4 f;
        XMStoreFloat4(&f, v);
        return f;
    }

    static Vec4 FromFloat4(const XMFLOAT4& f) {
        return Vec4(XMLoadFloat4(&f));
    }
};

inline Vec4 operator*(float s, Vec4 v) { return v * s; }

} // namespace uz
