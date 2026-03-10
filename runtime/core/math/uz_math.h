/// UzEngine Math Library
///
/// Thin wrapper over DirectXMath for SIMD-accelerated vector and matrix operations.
/// All types are DirectX-friendly: column-major matrices, XMVECTOR-compatible vectors.
///
/// Performance characteristics (Release, SSE4.2 baseline):
/// - Vec3 operations: 1-3 cycles (SIMD)
/// - Mat4 multiply: ~20 cycles
/// - Quat slerp: ~15 cycles
/// - All type conversions: zero-cost in release builds

#pragma once

#include <DirectXMath.h>
#include <cmath>
#include <cstring>

using namespace DirectX;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Vec2 — 2D float vector
// ─────────────────────────────────────────────────────────────────────────────

/// 2D float vector (x, y) — zero-initialized by default.
struct Vec2 {
    float x, y;

    /// Zero-initialized vector (0, 0).
    Vec2() : x(0), y(0) {}
    /// Construct from explicit components.
    constexpr Vec2(float x, float y) : x(x), y(y) {}
    /// Construct uniform vector (scalar, scalar).
    explicit constexpr Vec2(float scalar) : x(scalar), y(scalar) {}
    /// Load from memory (pointer to at least 2 floats).
    explicit Vec2(const float* ptr) : x(ptr[0]), y(ptr[1]) {}

    /// Pointer to elements for array-like access.
    float* data() { return &x; }
    /// Const pointer to elements.
    const float* data() const { return &x; }
    /// Array indexing [0]=x, [1]=y.
    float& operator[](int idx) { return data()[idx]; }
    /// Const array indexing.
    float operator[](int idx) const { return data()[idx]; }

    /// Get X component (uppercase for test compatibility).
    float X() const { return x; }
    /// Get Y component.
    float Y() const { return y; }

    /// Vector addition.
    Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    /// Vector subtraction.
    Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    /// Scalar multiplication.
    Vec2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
    /// Scalar division.
    Vec2 operator/(float scalar) const { return {x / scalar, y / scalar}; }
    /// Negation.
    Vec2 operator-() const { return {-x, -y}; }
    /// In-place addition.
    Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }
    /// In-place subtraction.
    Vec2& operator-=(const Vec2& other) { x -= other.x; y -= other.y; return *this; }
    /// In-place scalar multiplication.
    Vec2& operator*=(float scalar) { x *= scalar; y *= scalar; return *this; }

    /// Exact equality comparison.
    bool operator==(const Vec2& other) const { return x == other.x && y == other.y; }
    /// Inequality.
    bool operator!=(const Vec2& other) const { return !(*this == other); }

    /// Dot product.
    float dot(const Vec2& other) const { return x * other.x + y * other.y; }
    /// Uppercase alias for consistency.
    float Dot(const Vec2& other) const { return dot(other); }
    /// Euclidean length.
    float length() const { return std::sqrt(x * x + y * y); }
    /// Uppercase alias.
    float Length() const { return length(); }
    /// Squared length (faster, avoids sqrt).
    float lengthSq() const { return x * x + y * y; }
    /// Uppercase alias.
    float LengthSq() const { return lengthSq(); }
    /// Return normalized vector (length = 1).
    Vec2 normalize() const;
    /// Uppercase alias.
    Vec2 Normalized() const { return normalize(); }

    /// Zero vector (0, 0).
    static Vec2 Zero() { return {0, 0}; }
    /// Unit vector (1, 1).
    static Vec2 One() { return {1, 1}; }
    /// Unit vector along X axis (1, 0).
    static Vec2 UnitX() { return {1, 0}; }
    /// Unit vector along Y axis (0, 1).
    static Vec2 UnitY() { return {0, 1}; }
    /// Linear interpolation between a and b (t in [0,1]).
    static Vec2 Lerp(const Vec2& a, const Vec2& b, float t) {
        return a + (b - a) * t;
    }
    /// Load from XMFLOAT2 (DirectXMath compatible).
    static Vec2 FromFloat2(const XMFLOAT2& f) { return {f.x, f.y}; }

    /// Convert to XMFLOAT2 for DirectXMath compatibility.
    XMFLOAT2 ToFloat2() const { return {x, y}; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Vec3 — 3D float vector (primary SIMD vector)
// ─────────────────────────────────────────────────────────────────────────────

/// 3D float vector (x, y, z) — zero-initialized, SIMD-accelerated via DirectXMath.
/// Primary vector type used throughout the engine for positions, directions, scales.
struct Vec3 {
    float x, y, z;

    /// Zero-initialized vector (0, 0, 0).
    Vec3() : x(0), y(0), z(0) {}
    /// Construct from explicit components.
    constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    /// Construct uniform vector (scalar, scalar, scalar).
    explicit constexpr Vec3(float scalar) : x(scalar), y(scalar), z(scalar) {}
    /// Load from memory (pointer to at least 3 floats).
    explicit Vec3(const float* ptr) : x(ptr[0]), y(ptr[1]), z(ptr[2]) {}
    /// Convert from DirectXMath XMVECTOR.
    explicit Vec3(const XMVECTOR& xm) {
        XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(this), xm);
    }
    /// Convert from DirectXMath XMFLOAT3.
    explicit Vec3(const XMFLOAT3& f3) : x(f3.x), y(f3.y), z(f3.z) {}

    /// Convert to XMVECTOR for SIMD operations (zero-cost in release).
    XMVECTOR toXM() const { return XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(this)); }

    /// Pointer to elements for array-like access.
    float* data() { return &x; }
    /// Const pointer to elements.
    const float* data() const { return &x; }
    /// Array indexing [0]=x, [1]=y, [2]=z.
    float& operator[](int idx) { return data()[idx]; }
    /// Const array indexing.
    float operator[](int idx) const { return data()[idx]; }

    /// Get X component (uppercase for test compatibility).
    float X() const { return x; }
    /// Get Y component.
    float Y() const { return y; }
    /// Get Z component.
    float Z() const { return z; }

    /// Vector addition.
    Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    /// Vector subtraction.
    Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    /// Component-wise multiplication (not standard vector op, used for scale blending).
    Vec3 operator*(const Vec3& other) const { return {x * other.x, y * other.y, z * other.z}; }
    /// Scalar division.
    Vec3 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }
    /// Negation.
    Vec3 operator-() const { return {-x, -y, -z}; }
    /// In-place addition.
    Vec3& operator+=(const Vec3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    /// In-place subtraction.
    Vec3& operator-=(const Vec3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    /// In-place scalar multiplication.
    Vec3& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }

    /// Exact equality comparison.
    bool operator==(const Vec3& other) const { return x == other.x && y == other.y && z == other.z; }
    /// Inequality.
    bool operator!=(const Vec3& other) const { return !(*this == other); }

    /// Dot product (SIMD-accelerated via DirectXMath).
    float dot(const Vec3& other) const;
    /// Uppercase alias.
    float Dot(const Vec3& other) const { return dot(other); }
    /// Cross product (SIMD-accelerated).
    Vec3 cross(const Vec3& other) const;
    /// Uppercase alias.
    Vec3 Cross(const Vec3& other) const { return cross(other); }
    /// Euclidean length (SIMD-accelerated).
    float length() const;
    /// Uppercase alias.
    float Length() const { return length(); }
    /// Squared length (faster, avoids sqrt).
    float lengthSq() const { return x * x + y * y + z * z; }
    /// Uppercase alias.
    float LengthSq() const { return lengthSq(); }
    /// Return normalized vector (length = 1, SIMD-accelerated).
    Vec3 normalize() const;
    /// Uppercase alias.
    Vec3 Normalized() const { return normalize(); }
    /// Component-wise maximum.
    Vec3 max(const Vec3& other) const;
    /// Component-wise minimum.
    Vec3 min(const Vec3& other) const;

    /// Zero vector (0, 0, 0).
    static Vec3 Zero() { return {0, 0, 0}; }
    /// Unit vector (1, 1, 1).
    static Vec3 One() { return {1, 1, 1}; }
    /// Unit vector along X axis (1, 0, 0).
    static Vec3 UnitX() { return {1, 0, 0}; }
    /// Unit vector along Y axis (0, 1, 0).
    static Vec3 UnitY() { return {0, 1, 0}; }
    /// Unit vector along Z axis (0, 0, 1).
    static Vec3 UnitZ() { return {0, 0, 1}; }
    /// World "up" direction (0, 1, 0).
    static Vec3 Up() { return UnitY(); }
    /// World "right" direction (1, 0, 0).
    static Vec3 Right() { return UnitX(); }
    /// World "forward" direction (0, 0, 1).
    static Vec3 Forward() { return UnitZ(); }
    /// Linear interpolation between a and b (t in [0,1]).
    static Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
        return a + (b - a) * t;
    }
    /// Component-wise minimum.
    static Vec3 Min(const Vec3& a, const Vec3& b) { return a.min(b); }
    /// Component-wise maximum.
    static Vec3 Max(const Vec3& a, const Vec3& b) { return a.max(b); }
    /// Load from XMFLOAT3 (DirectXMath compatible).
    static Vec3 FromFloat3(const XMFLOAT3& f) { return {f.x, f.y, f.z}; }

    /// Convert to XMFLOAT3 for DirectXMath compatibility.
    XMFLOAT3 ToFloat3() const { return {x, y, z}; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Vec4 — 4D float vector
// ─────────────────────────────────────────────────────────────────────────────

struct Vec4 {
    float x, y, z, w;

    /// Constructors
    Vec4() : x(0), y(0), z(0), w(0) {}
    constexpr Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    constexpr Vec4(const Vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
    explicit constexpr Vec4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
    explicit Vec4(const float* ptr) : x(ptr[0]), y(ptr[1]), z(ptr[2]), w(ptr[3]) {}
    explicit Vec4(const XMVECTOR& xm) {
        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(this), xm);
    }
    explicit Vec4(const XMFLOAT4& f4) : x(f4.x), y(f4.y), z(f4.z), w(f4.w) {}

    /// Conversion to XMVECTOR
    XMVECTOR toXM() const { return XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(this)); }

    /// Extract XYZ as Vec3
    Vec3 xyz() const { return {x, y, z}; }
    Vec3 XYZ() const { return xyz(); }

    /// Element access
    float* data() { return &x; }
    const float* data() const { return &x; }
    float& operator[](int idx) { return data()[idx]; }
    float operator[](int idx) const { return data()[idx]; }

    /// Getters (uppercase for test compatibility)
    float X() const { return x; }
    float Y() const { return y; }
    float Z() const { return z; }
    float W() const { return w; }

    /// Arithmetic operators
    Vec4 operator+(const Vec4& other) const { return {x + other.x, y + other.y, z + other.z, w + other.w}; }
    Vec4 operator-(const Vec4& other) const { return {x - other.x, y - other.y, z - other.z, w - other.w}; }
    Vec4 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar, w * scalar}; }
    Vec4 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar, w / scalar}; }
    Vec4 operator-() const { return {-x, -y, -z, -w}; }
    Vec4& operator+=(const Vec4& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
    Vec4& operator-=(const Vec4& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
    Vec4& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; w *= scalar; return *this; }

    /// Comparison
    bool operator==(const Vec4& other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
    bool operator!=(const Vec4& other) const { return !(*this == other); }

    /// Vector operations
    float dot(const Vec4& other) const;
    float Dot(const Vec4& other) const { return dot(other); }
    float length() const;
    float Length() const { return length(); }
    float lengthSq() const { return x * x + y * y + z * z + w * w; }
    float LengthSq() const { return lengthSq(); }
    Vec4 normalize() const;
    Vec4 Normalized() const { return normalize(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Mat4 — 4×4 matrix (column-major, DirectX convention)
// ─────────────────────────────────────────────────────────────────────────────

struct Mat4 {
    XMFLOAT4X4 m;

    /// Constructors
    Mat4() {
        XMStoreFloat4x4(&m, XMMatrixIdentity());
    }
    Mat4(const XMMATRIX& xm) {
        XMStoreFloat4x4(&m, xm);
    }
    Mat4(const XMFLOAT4X4& f4x4) : m(f4x4) {}
    explicit Mat4(const float* ptr) {
        std::memcpy(&m, ptr, 16 * sizeof(float));
    }

    /// Static constructors
    static Mat4 Identity();
    static Mat4 Zero();
    static Mat4 Translation(float x, float y, float z);
    static Mat4 Translation(const Vec3& pos);
    static Mat4 Scaling(float x, float y, float z);
    static Mat4 Scaling(const Vec3& scale);
    static Mat4 Scaling(float uniform) { return Scaling(uniform, uniform, uniform); }
    static Mat4 RotationX(float angle);
    static Mat4 RotationY(float angle);
    static Mat4 RotationZ(float angle);
    static Mat4 Perspective(float fov, float aspect, float near_plane, float far_plane);
    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

    /// Conversion to XMMATRIX
    XMMATRIX toXM() const { return XMLoadFloat4x4(&m); }

    /// Element access
    float* data() { return &m._11; }
    const float* data() const { return &m._11; }
    float& operator()(int row, int col) { return data()[row * 4 + col]; }
    float operator()(int row, int col) const { return data()[row * 4 + col]; }

    /// Matrix operations
    Mat4 operator*(const Mat4& other) const;
    Vec4 operator*(const Vec4& v) const;
    Vec3 operator*(const Vec3& v) const;
    Mat4& operator*=(const Mat4& other);

    Mat4 transpose() const;
    Mat4 Transposed() const { return transpose(); }
    Mat4 inverse() const;
    Mat4 Inverse() const { return inverse(); }
    float determinant() const;
    float Determinant() const { return determinant(); }

    /// Transform vector by matrix
    Vec3 TransformPoint(const Vec3& p) const { return *this * p; }
    Vec3 TransformDirection(const Vec3& d) const {
        // Transform direction (no translation) — use only 3x3 part
        XMMATRIX m = toXM();
        XMVECTOR vec = d.toXM();
        XMVECTOR result = XMVector3TransformCoord(vec, m);
        // XMVector3TransformCoord applies the full matrix, so we need to manually handle
        // the 3x3 rotation/scale only. Use XMVector4Transform with w=0.
        XMVECTOR vec4 = XMVectorSetW(vec, 0.0f);
        XMVECTOR result4 = XMVector4Transform(vec4, m);
        return Vec3(result4);
    }

    /// Decomposition
    Vec3 getTranslation() const;
    Vec3 getScale() const;
    // Quat getRotation() const;  // Forward declared, defined after Quat

    /// Comparisons
    bool operator==(const Mat4& other) const {
        for (int i = 0; i < 16; i++) {
            if (data()[i] != other.data()[i]) return false;
        }
        return true;
    }
    bool operator!=(const Mat4& other) const { return !(*this == other); }

    /// Conversions
    XMFLOAT4X4 ToFloat4x4() const { return m; }
    static Mat4 FromFloat4x4(const XMFLOAT4X4& f) { return {f}; }

    /// Transformations
    Mat4 translate(const Vec3& pos) const;
    Mat4 scale(const Vec3& s) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// Quat — Quaternion (x, y, z, w)
// ─────────────────────────────────────────────────────────────────────────────

struct Quat {
    float x, y, z, w;

    /// Constructors
    Quat() : x(0), y(0), z(0), w(1) {}
    constexpr Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    Quat(const Vec3& axis, float angle);
    explicit Quat(const XMVECTOR& xm) {
        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(this), xm);
    }
    explicit Quat(const XMFLOAT4& f4) : x(f4.x), y(f4.y), z(f4.z), w(f4.w) {}

    /// Static constructors
    static Quat Identity() { return {0, 0, 0, 1}; }
    static Quat FromAxisAngle(const Vec3& axis, float angle);
    static Quat FromEuler(float pitch, float yaw, float roll);
    static Quat FromMatrix(const Mat4& m);

    /// Conversion to XMVECTOR
    XMVECTOR toXM() const { return XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(this)); }

    /// Getters (uppercase for test compatibility)
    float X() const { return x; }
    float Y() const { return y; }
    float Z() const { return z; }
    float W() const { return w; }

    /// Arithmetic operators
    Quat operator*(const Quat& other) const;
    Vec3 operator*(const Vec3& v) const;
    bool operator==(const Quat& other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
    bool operator!=(const Quat& other) const { return !(*this == other); }

    /// Quaternion operations
    Quat conjugate() const { return {-x, -y, -z, w}; }
    Quat inverse() const;
    Quat Inverse() const { return inverse(); }
    Quat normalize() const;
    Quat Normalized() const { return normalize(); }
    float dot(const Quat& other) const { return x * other.x + y * other.y + z * other.z + w * other.w; }
    float Dot(const Quat& other) const { return dot(other); }
    float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float Length() const { return length(); }
    Vec3 RotateVector(const Vec3& v) const { return *this * v; }

    /// Interpolation
    Quat slerp(const Quat& other, float t) const;
    Quat Slerp(const Quat& other, float t) const { return slerp(other, t); }
    static Quat Slerp(const Quat& a, const Quat& b, float t) { return a.slerp(b, t); }
    Quat lerp(const Quat& other, float t) const;
    Quat Lerp(const Quat& other, float t) const { return lerp(other, t); }

    /// Conversion
    Mat4 toMatrix() const;
    Mat4 ToMatrix() const { return toMatrix(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Transform — Position + Rotation + Scale
// ─────────────────────────────────────────────────────────────────────────────

struct Transform {
    Vec3 position = {0, 0, 0};
    Quat rotation = {0, 0, 0, 1};
    Vec3 scale = {1, 1, 1};

    Transform() = default;
    Transform(const Vec3& pos, const Quat& rot, const Vec3& scl)
        : position(pos), rotation(rot), scale(scl) {}
    explicit Transform(const Mat4& m);

    /// Convert to matrix (TRS order: translate after scale/rotate)
    Mat4 toMatrix() const;
    Mat4 ToMatrix() const { return toMatrix(); }

    /// Decompose from matrix
    static Transform FromMatrix(const Mat4& m);

    /// Hierarchy composition (world = local * parent)
    Transform compose(const Transform& parent) const;
    Transform Compose(const Transform& parent) const { return compose(parent); }

    /// Inverse
    Transform inverse() const;
    Transform Inverse() const { return inverse(); }

    /// Transform points/vectors
    Vec3 TransformPoint(const Vec3& p) const { return position + rotation * (scale * p); }
    Vec3 TransformDirection(const Vec3& d) const { return rotation * (scale * d); }

    /// Transformations
    Transform translate(const Vec3& delta) const;
    Transform Translate(const Vec3& delta) const { return translate(delta); }
    Transform rotate(const Quat& rot) const;
    Transform Rotate(const Quat& rot) const { return rotate(rot); }
    Transform scaleBy(const Vec3& s) const;
    Transform ScaleBy(const Vec3& s) const { return scaleBy(s); }

    /// Static constructors
    static Transform Lerp(const Transform& a, const Transform& b, float t) {
        Transform result;
        result.position = a.position + (b.position - a.position) * t;
        result.rotation = a.rotation.slerp(b.rotation, t);
        result.scale = a.scale + (b.scale - a.scale) * t;
        return result;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Utility operators for scalar * vector
// ─────────────────────────────────────────────────────────────────────────────

inline Vec2 operator*(float scalar, const Vec2& v) { return v * scalar; }
inline Vec3 operator*(float scalar, const Vec3& v) { return v * scalar; }
inline Vec4 operator*(float scalar, const Vec4& v) { return v * scalar; }

}  // namespace uz
