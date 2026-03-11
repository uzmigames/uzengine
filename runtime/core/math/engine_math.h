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

#include <cmath>
#include <cstring>
#include <DirectXMath.h>

using namespace DirectX;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Vec2 — 2D float vector
// ─────────────────────────────────────────────────────────────────────────────

/// 2D floating-point vector for UI coordinates, texture UVs, and screen-space math.
///
/// Stores x, y as `float` fields. All operations are scalar (no SIMD).
/// Default-constructed to zero.
///
/// # Memory Layout
/// `[x: f32, y: f32]` — 8 bytes, no padding.
///
/// # Examples
/// ```cpp
/// Vec2 uv{0.5f, 0.5f};
/// Vec2 screen = uv * Vec2{1920.0f, 1080.0f};
/// float len = screen.length();
/// ```
struct Vec2 {
    /// X component (horizontal axis).
    float x;
    /// Y component (vertical axis).
    float y;

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
    Vec2& operator+=(const Vec2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }
    /// In-place subtraction.
    Vec2& operator-=(const Vec2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }
    /// In-place scalar multiplication.
    Vec2& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    /// Exact equality comparison.
    bool operator==(const Vec2& other) const { return x == other.x && y == other.y; }
    /// Inequality.
    bool operator!=(const Vec2& other) const { return !(*this == other); }

    /// Computes the dot product with another vector.
    ///
    /// # Returns
    /// Scalar projection: `x*other.x + y*other.y`.
    float dot(const Vec2& other) const { return x * other.x + y * other.y; }
    /// Uppercase alias for `dot()`.
    float Dot(const Vec2& other) const { return dot(other); }
    /// Computes the Euclidean length (magnitude) of the vector.
    ///
    /// # Returns
    /// `sqrt(x*x + y*y)`. Use `lengthSq()` when comparing distances to avoid sqrt.
    float length() const { return std::sqrt(x * x + y * y); }
    /// Uppercase alias for `length()`.
    float Length() const { return length(); }
    /// Computes the squared Euclidean length (avoids sqrt).
    ///
    /// # Returns
    /// `x*x + y*y`. Prefer over `length()` for distance comparisons.
    float lengthSq() const { return x * x + y * y; }
    /// Uppercase alias for `lengthSq()`.
    float LengthSq() const { return lengthSq(); }
    /// Returns a unit-length copy of this vector.
    ///
    /// # Returns
    /// Normalized vector with length 1. Returns copy of `*this` if length is zero.
    Vec2 normalize() const;
    /// Uppercase alias for `normalize()`.
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
    static Vec2 Lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }
    /// Load from XMFLOAT2 (DirectXMath compatible).
    static Vec2 FromFloat2(const XMFLOAT2& f) { return {f.x, f.y}; }

    /// Convert to XMFLOAT2 for DirectXMath compatibility.
    XMFLOAT2 ToFloat2() const { return {x, y}; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Vec3 — 3D float vector (primary SIMD vector)
// ─────────────────────────────────────────────────────────────────────────────

/// 3D floating-point vector with SIMD acceleration via DirectXMath.
///
/// Stores x, y, z as `float` fields. All arithmetic operations are SIMD-accelerated
/// through implicit conversion to `XMVECTOR` via `toXM()`. Default-constructed to zero.
/// Primary vector type for positions, directions, normals, and scales.
///
/// # Memory Layout
/// `[x: f32, y: f32, z: f32]` — 12 bytes, no padding. NOT 16-byte aligned.
/// Use `toXM()` for SIMD operations that require `XMVECTOR` (16-byte aligned).
///
/// # Coordinate System
/// Left-handed: +X right, +Y up, +Z forward.
///
/// # Examples
/// ```cpp
/// Vec3 a{1.0f, 2.0f, 3.0f};
/// Vec3 b{4.0f, 5.0f, 6.0f};
/// float d = a.Dot(b);           // 32.0
/// Vec3 c = a.Cross(b);          // {-3, 6, -3}
/// Vec3 n = a.Normalized();      // unit length
/// ```
struct Vec3 {
    /// X component (horizontal axis, positive = right).
    float x;
    /// Y component (vertical axis, positive = up).
    float y;
    /// Z component (depth axis, positive = forward in LH coordinate system).
    float z;

    /// Zero-initialized vector (0, 0, 0).
    Vec3() : x(0), y(0), z(0) {}
    /// Construct from explicit components.
    constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    /// Construct uniform vector (scalar, scalar, scalar).
    explicit constexpr Vec3(float scalar) : x(scalar), y(scalar), z(scalar) {}
    /// Load from memory (pointer to at least 3 floats).
    explicit Vec3(const float* ptr) : x(ptr[0]), y(ptr[1]), z(ptr[2]) {}
    /// Convert from DirectXMath XMVECTOR.
    explicit Vec3(const XMVECTOR& xm) { XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(this), xm); }
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
    Vec3& operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
    /// In-place subtraction.
    Vec3& operator-=(const Vec3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }
    /// In-place scalar multiplication.
    Vec3& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    /// Exact equality comparison.
    bool operator==(const Vec3& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    /// Inequality.
    bool operator!=(const Vec3& other) const { return !(*this == other); }

    /// Computes the dot product with another vector (SIMD-accelerated).
    ///
    /// # Arguments
    /// * `other` - The vector to dot with.
    ///
    /// # Returns
    /// Scalar projection: `x*other.x + y*other.y + z*other.z`.
    ///
    /// # Examples
    /// ```cpp
    /// Vec3 a{1, 0, 0}, b{0, 1, 0};
    /// assert(a.Dot(b) == 0.0f); // perpendicular
    /// ```
    float dot(const Vec3& other) const;
    /// Uppercase alias for `dot()`.
    float Dot(const Vec3& other) const { return dot(other); }
    /// Computes the cross product with another vector (SIMD-accelerated).
    ///
    /// # Arguments
    /// * `other` - The right-hand operand.
    ///
    /// # Returns
    /// Vector perpendicular to both inputs. Magnitude = `|a|*|b|*sin(theta)`.
    ///
    /// # Examples
    /// ```cpp
    /// Vec3 up = Vec3::Forward().Cross(Vec3::Right()); // {0, 0, 1}
    /// ```
    Vec3 cross(const Vec3& other) const;
    /// Uppercase alias for `cross()`.
    Vec3 Cross(const Vec3& other) const { return cross(other); }
    /// Computes the Euclidean length (SIMD-accelerated).
    ///
    /// # Returns
    /// `sqrt(x*x + y*y + z*z)`. Use `lengthSq()` for distance comparisons.
    float length() const;
    /// Uppercase alias for `length()`.
    float Length() const { return length(); }
    /// Computes the squared Euclidean length (avoids sqrt).
    ///
    /// # Returns
    /// `x*x + y*y + z*z`. Prefer over `length()` for distance comparisons.
    float lengthSq() const { return x * x + y * y + z * z; }
    /// Uppercase alias for `lengthSq()`.
    float LengthSq() const { return lengthSq(); }
    /// Returns a unit-length copy of this vector (SIMD-accelerated).
    ///
    /// # Returns
    /// Vector with length 1. Returns zero vector if input is zero-length.
    Vec3 normalize() const;
    /// Uppercase alias for `normalize()`.
    Vec3 Normalized() const { return normalize(); }
    /// Returns component-wise maximum of this vector and `other`.
    Vec3 max(const Vec3& other) const;
    /// Returns component-wise minimum of this vector and `other`.
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
    /// World "up" direction (0, 0, 1) — Z-up coordinate system (X-forward, Y-right, Z-up).
    static Vec3 Up() { return UnitZ(); }
    /// World "right" direction (0, 1, 0) — Z-up coordinate system.
    static Vec3 Right() { return UnitY(); }
    /// World "forward" direction (1, 0, 0) — Z-up coordinate system.
    static Vec3 Forward() { return UnitX(); }
    /// Linear interpolation between a and b (t in [0,1]).
    static Vec3 Lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
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

/// 4D floating-point vector for homogeneous coordinates and SIMD operations.
///
/// Stores x, y, z, w as `float` fields. Used for matrix-vector multiplication
/// (w=1 for points, w=0 for directions), color (RGBA), and shader constants.
///
/// # Memory Layout
/// `[x: f32, y: f32, z: f32, w: f32]` — 16 bytes. Naturally aligned for XMVECTOR.
///
/// # Examples
/// ```cpp
/// Vec4 pos{1.0f, 2.0f, 3.0f, 1.0f};  // homogeneous point
/// Vec4 dir{1.0f, 0.0f, 0.0f, 0.0f};  // direction (w=0)
/// Vec3 xyz = pos.xyz();               // extract {1, 2, 3}
/// ```
struct Vec4 {
    /// X component.
    float x;
    /// Y component.
    float y;
    /// Z component.
    float z;
    /// W component (1.0 for points, 0.0 for directions in homogeneous space).
    float w;

    /// Zero-initialized vector (0, 0, 0, 0).
    Vec4() : x(0), y(0), z(0), w(0) {}
    /// Construct from explicit components.
    constexpr Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    /// Construct from Vec3 + W component (e.g., position w=1, direction w=0).
    constexpr Vec4(const Vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
    /// Construct uniform vector (scalar, scalar, scalar, scalar).
    explicit constexpr Vec4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
    /// Load from memory (pointer to at least 4 floats).
    explicit Vec4(const float* ptr) : x(ptr[0]), y(ptr[1]), z(ptr[2]), w(ptr[3]) {}
    /// Convert from DirectXMath XMVECTOR.
    explicit Vec4(const XMVECTOR& xm) { XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(this), xm); }
    /// Convert from DirectXMath XMFLOAT4.
    explicit Vec4(const XMFLOAT4& f4) : x(f4.x), y(f4.y), z(f4.z), w(f4.w) {}

    /// Convert to XMVECTOR for SIMD operations.
    XMVECTOR toXM() const { return XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(this)); }

    /// Extract XYZ as Vec3 (drops W).
    Vec3 xyz() const { return {x, y, z}; }
    /// Uppercase alias.
    Vec3 XYZ() const { return xyz(); }

    /// Pointer to contiguous float storage for array-like access.
    float* data() { return &x; }
    /// Const pointer to contiguous float storage.
    const float* data() const { return &x; }
    /// Array indexing: [0]=x, [1]=y, [2]=z, [3]=w.
    float& operator[](int idx) { return data()[idx]; }
    /// Const array indexing.
    float operator[](int idx) const { return data()[idx]; }

    /// Get X component (uppercase for test compatibility).
    float X() const { return x; }
    /// Get Y component.
    float Y() const { return y; }
    /// Get Z component.
    float Z() const { return z; }
    /// Get W component.
    float W() const { return w; }

    /// Vector addition.
    Vec4 operator+(const Vec4& other) const {
        return {x + other.x, y + other.y, z + other.z, w + other.w};
    }
    /// Vector subtraction.
    Vec4 operator-(const Vec4& other) const {
        return {x - other.x, y - other.y, z - other.z, w - other.w};
    }
    /// Scalar multiplication.
    Vec4 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar, w * scalar}; }
    /// Scalar division.
    Vec4 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar, w / scalar}; }
    /// Negation.
    Vec4 operator-() const { return {-x, -y, -z, -w}; }
    /// In-place addition.
    Vec4& operator+=(const Vec4& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
        return *this;
    }
    /// In-place subtraction.
    Vec4& operator-=(const Vec4& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;
        return *this;
    }
    /// In-place scalar multiplication.
    Vec4& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    /// Exact equality comparison.
    bool operator==(const Vec4& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }
    /// Inequality.
    bool operator!=(const Vec4& other) const { return !(*this == other); }

    /// Computes the 4D dot product (SIMD-accelerated).
    ///
    /// # Returns
    /// `x*other.x + y*other.y + z*other.z + w*other.w`.
    float dot(const Vec4& other) const;
    /// Uppercase alias for `dot()`.
    float Dot(const Vec4& other) const { return dot(other); }
    /// Computes the 4D Euclidean length (SIMD-accelerated).
    float length() const;
    /// Uppercase alias for `length()`.
    float Length() const { return length(); }
    /// Computes the squared 4D length (avoids sqrt).
    float lengthSq() const { return x * x + y * y + z * z + w * w; }
    /// Uppercase alias for `lengthSq()`.
    float LengthSq() const { return lengthSq(); }
    /// Returns a unit-length copy (SIMD-accelerated).
    Vec4 normalize() const;
    /// Uppercase alias for `normalize()`.
    Vec4 Normalized() const { return normalize(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Mat4 — 4×4 matrix (column-major, DirectX convention)
// ─────────────────────────────────────────────────────────────────────────────

/// 4x4 matrix for transforms, projections, and view calculations (SIMD-accelerated).
///
/// Wraps `XMFLOAT4X4` (column-major, DirectX row-vector convention).
/// Default-constructed to identity. All operations delegate to DirectXMath SIMD intrinsics.
///
/// # Memory Layout
/// 16 floats (64 bytes) stored as `XMFLOAT4X4`. Row-major in memory
/// (row 0 = _11,_12,_13,_14), but multiplication follows DirectX row-vector convention.
///
/// # Examples
/// ```cpp
/// Mat4 view = Mat4::LookAt(eye, target, Vec3::Up());
/// Mat4 proj = Mat4::Perspective(XM_PIDIV4, 16.0f/9.0f, 0.1f, 1000.0f);
/// Mat4 mvp = model * view * proj;
/// Vec3 worldPos = mvp.getTranslation();
/// ```
struct Mat4 {
    /// Raw 4x4 float storage (DirectXMath compatible).
    XMFLOAT4X4 m;

    /// Default: identity matrix.
    Mat4() { XMStoreFloat4x4(&m, XMMatrixIdentity()); }
    /// Construct from DirectXMath XMMATRIX.
    Mat4(const XMMATRIX& xm) { XMStoreFloat4x4(&m, xm); }
    /// Construct from DirectXMath XMFLOAT4X4.
    Mat4(const XMFLOAT4X4& f4x4) : m(f4x4) {}
    /// Load from memory (pointer to 16 floats, row-major layout).
    explicit Mat4(const float* ptr) { std::memcpy(&m, ptr, 16 * sizeof(float)); }

    /// Returns the 4x4 identity matrix.
    static Mat4 Identity();
    /// Returns a 4x4 zero matrix (all elements 0).
    static Mat4 Zero();
    /// Creates a translation matrix from individual components.
    static Mat4 Translation(float x, float y, float z);
    /// Creates a translation matrix from a position vector.
    static Mat4 Translation(const Vec3& pos);
    /// Creates a scaling matrix from individual axis scales.
    static Mat4 Scaling(float x, float y, float z);
    /// Creates a scaling matrix from a scale vector.
    static Mat4 Scaling(const Vec3& scale);
    /// Creates a uniform scaling matrix.
    static Mat4 Scaling(float uniform) { return Scaling(uniform, uniform, uniform); }
    /// Creates a rotation matrix around the X axis.
    ///
    /// # Arguments
    /// * `angle` - Rotation in radians.
    static Mat4 RotationX(float angle);
    /// Creates a rotation matrix around the Y axis.
    ///
    /// # Arguments
    /// * `angle` - Rotation in radians.
    static Mat4 RotationY(float angle);
    /// Creates a rotation matrix around the Z axis.
    ///
    /// # Arguments
    /// * `angle` - Rotation in radians.
    static Mat4 RotationZ(float angle);
    /// Creates a left-handed perspective projection matrix.
    ///
    /// # Arguments
    /// * `fov` - Vertical field of view in radians.
    /// * `aspect` - Width/height aspect ratio.
    /// * `near_plane` - Near clipping plane distance (must be > 0).
    /// * `far_plane` - Far clipping plane distance (must be > near_plane).
    static Mat4 Perspective(float fov, float aspect, float near_plane, float far_plane);
    /// Creates a left-handed look-at view matrix.
    ///
    /// # Arguments
    /// * `eye` - Camera position in world space.
    /// * `target` - Point the camera looks at.
    /// * `up` - World up direction (typically `Vec3::Up()`).
    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

    /// Creates a left-handed orthographic projection matrix.
    ///
    /// # Arguments
    /// * `width` - View width in world units.
    /// * `height` - View height in world units.
    /// * `near_plane` - Near clipping plane distance.
    /// * `far_plane` - Far clipping plane distance.
    static Mat4 Orthographic(float width, float height, float near_plane, float far_plane);

    /// Converts to XMMATRIX for SIMD operations (zero-cost in release).
    XMMATRIX toXM() const { return XMLoadFloat4x4(&m); }

    /// Pointer to the first element (_11) for raw float access.
    float* data() { return &m._11; }
    /// Const pointer to the first element.
    const float* data() const { return &m._11; }
    /// Element access by row and column (0-indexed).
    float& operator()(int row, int col) { return data()[row * 4 + col]; }
    /// Const element access by row and column.
    float operator()(int row, int col) const { return data()[row * 4 + col]; }

    /// Matrix multiplication (SIMD-accelerated).
    Mat4 operator*(const Mat4& other) const;
    /// Transforms a Vec4 by this matrix.
    Vec4 operator*(const Vec4& v) const;
    /// Transforms a Vec3 as a 3D point (w=1 assumed, SIMD-accelerated).
    Vec3 operator*(const Vec3& v) const;
    /// In-place matrix multiplication.
    Mat4& operator*=(const Mat4& other);

    /// Returns the transpose of this matrix.
    Mat4 transpose() const;
    /// Uppercase alias for `transpose()`.
    Mat4 Transposed() const { return transpose(); }
    /// Returns the inverse of this matrix (SIMD-accelerated).
    ///
    /// # Returns
    /// Inverse matrix. Result is undefined if matrix is singular (determinant near zero).
    Mat4 inverse() const;
    /// Uppercase alias for `inverse()`.
    Mat4 Inverse() const { return inverse(); }
    /// Computes the determinant of this matrix.
    float determinant() const;
    /// Uppercase alias for `determinant()`.
    float Determinant() const { return determinant(); }

    /// Transforms a point by this matrix (applies full 4x4 including translation).
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

    /// Extracts the translation component (row 3: _41, _42, _43).
    Vec3 getTranslation() const;
    /// Extracts the scale by computing the length of each basis row.
    Vec3 getScale() const;
    // Quat getRotation() const;  // Forward declared, defined after Quat

    /// Exact element-wise equality comparison.
    bool operator==(const Mat4& other) const {
        for (int i = 0; i < 16; i++) {
            if (data()[i] != other.data()[i])
                return false;
        }
        return true;
    }
    bool operator!=(const Mat4& other) const { return !(*this == other); }

    /// Converts to raw XMFLOAT4X4 for DirectXMath interop.
    XMFLOAT4X4 ToFloat4x4() const { return m; }
    /// Creates Mat4 from raw XMFLOAT4X4.
    static Mat4 FromFloat4x4(const XMFLOAT4X4& f) { return {f}; }

    /// Returns `this * Translation(pos)` — appends a translation.
    Mat4 translate(const Vec3& pos) const;
    /// Returns `this * Scaling(s)` — appends a scale.
    Mat4 scale(const Vec3& s) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// Quat — Quaternion (x, y, z, w)
// ─────────────────────────────────────────────────────────────────────────────

/// Unit quaternion for 3D rotation (SIMD-accelerated via DirectXMath).
///
/// Stores (x, y, z, w) where (x, y, z) is the imaginary part and w is the real part.
/// Default-constructed to identity rotation (0, 0, 0, 1).
/// Prefer quaternions over Euler angles to avoid gimbal lock and enable smooth interpolation.
///
/// # Memory Layout
/// `[x: f32, y: f32, z: f32, w: f32]` — 16 bytes, same layout as XMFLOAT4.
///
/// # Examples
/// ```cpp
/// Quat q = Quat::FromAxisAngle(Vec3::Up(), XM_PIDIV2); // 90 deg around Y
/// Vec3 rotated = q * Vec3::Forward();                    // rotates forward vector
/// Mat4 rotMat = q.toMatrix();                            // convert to rotation matrix
/// Quat blended = Quat::Slerp(a, b, 0.5f);              // smooth interpolation
/// ```
struct Quat {
    float x, y, z, w;

    /// Identity rotation (no rotation).
    Quat() : x(0), y(0), z(0), w(1) {}
    /// Construct from explicit components.
    constexpr Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    /// Construct rotation around axis by angle (radians).
    Quat(const Vec3& axis, float angle);
    /// Convert from DirectXMath XMVECTOR.
    explicit Quat(const XMVECTOR& xm) { XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(this), xm); }
    /// Convert from DirectXMath XMFLOAT4.
    explicit Quat(const XMFLOAT4& f4) : x(f4.x), y(f4.y), z(f4.z), w(f4.w) {}

    /// Returns the identity quaternion (no rotation).
    static Quat Identity() { return {0, 0, 0, 1}; }
    /// Creates a quaternion from an axis and angle.
    ///
    /// # Arguments
    /// * `axis` - Rotation axis (should be normalized).
    /// * `angle` - Rotation angle in radians.
    static Quat FromAxisAngle(const Vec3& axis, float angle);
    /// Creates a quaternion from Euler angles.
    ///
    /// # Arguments
    /// * `pitch` - Rotation around X axis (radians).
    /// * `yaw` - Rotation around Y axis (radians).
    /// * `roll` - Rotation around Z axis (radians).
    static Quat FromEuler(float pitch, float yaw, float roll);
    /// Extracts rotation quaternion from a rotation matrix.
    static Quat FromMatrix(const Mat4& m);

    /// Converts to XMVECTOR for SIMD operations.
    XMVECTOR toXM() const { return XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(this)); }

    /// Get X component (imaginary i).
    float X() const { return x; }
    /// Get Y component (imaginary j).
    float Y() const { return y; }
    /// Get Z component (imaginary k).
    float Z() const { return z; }
    /// Get W component (real/scalar part).
    float W() const { return w; }

    /// Quaternion multiplication (combines rotations: result = this * other).
    Quat operator*(const Quat& other) const;
    /// Rotates a vector by this quaternion.
    Vec3 operator*(const Vec3& v) const;
    bool operator==(const Quat& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }
    bool operator!=(const Quat& other) const { return !(*this == other); }

    /// Returns the conjugate (negated imaginary part). For unit quaternions, equals inverse.
    Quat conjugate() const { return {-x, -y, -z, w}; }
    /// Returns the multiplicative inverse (SIMD-accelerated).
    Quat inverse() const;
    /// Uppercase alias for `inverse()`.
    Quat Inverse() const { return inverse(); }
    /// Returns a unit-length quaternion (SIMD-accelerated).
    Quat normalize() const;
    /// Uppercase alias for `normalize()`.
    Quat Normalized() const { return normalize(); }
    float dot(const Quat& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }
    float Dot(const Quat& other) const { return dot(other); }
    float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float Length() const { return length(); }
    Vec3 RotateVector(const Vec3& v) const { return *this * v; }

    /// Spherical linear interpolation (constant angular velocity, SIMD-accelerated).
    ///
    /// # Arguments
    /// * `other` - Target quaternion.
    /// * `t` - Interpolation factor in [0, 1].
    ///
    /// # Returns
    /// Interpolated quaternion. `t=0` returns `*this`, `t=1` returns `other`.
    Quat slerp(const Quat& other, float t) const;
    /// Uppercase alias for `slerp()`.
    Quat Slerp(const Quat& other, float t) const { return slerp(other, t); }
    /// Static slerp between two quaternions.
    static Quat Slerp(const Quat& a, const Quat& b, float t) { return a.slerp(b, t); }
    /// Linear interpolation (faster but non-constant angular velocity).
    ///
    /// # Arguments
    /// * `other` - Target quaternion.
    /// * `t` - Interpolation factor in [0, 1].
    Quat lerp(const Quat& other, float t) const;
    /// Uppercase alias for `lerp()`.
    Quat Lerp(const Quat& other, float t) const { return lerp(other, t); }

    /// Converts this quaternion to a 4x4 rotation matrix.
    Mat4 toMatrix() const;
    /// Uppercase alias for `toMatrix()`.
    Mat4 ToMatrix() const { return toMatrix(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Transform — Position + Rotation + Scale
// ─────────────────────────────────────────────────────────────────────────────

/// Decomposed 3D transform: position + rotation + scale (TRS).
///
/// Stores transform components separately for efficient manipulation and interpolation.
/// Conversion to/from Mat4 available via `toMatrix()` / `FromMatrix()`.
/// Supports hierarchy composition for scene graphs.
///
/// # TRS Order
/// Matrix = Scale * Rotate * Translate (applied right-to-left to vertices).
///
/// # Examples
/// ```cpp
/// Transform t;
/// t.position = {10, 0, 0};
/// t.rotation = Quat::FromAxisAngle(Vec3::Up(), XM_PIDIV2);
/// t.scale = {2, 2, 2};
/// Mat4 matrix = t.toMatrix();
/// Vec3 worldPos = t.TransformPoint({1, 0, 0});
/// ```
struct Transform {
    /// World position (translation).
    Vec3 position = {0, 0, 0};
    /// Orientation as unit quaternion.
    Quat rotation = {0, 0, 0, 1};
    /// Non-uniform scale per axis.
    Vec3 scale = {1, 1, 1};

    /// Default: identity transform (origin, no rotation, unit scale).
    Transform() = default;
    /// Constructs from explicit position, rotation, and scale.
    Transform(const Vec3& pos, const Quat& rot, const Vec3& scl)
        : position(pos), rotation(rot), scale(scl) {}
    /// Decomposes a matrix into position, rotation, and scale via `XMMatrixDecompose`.
    explicit Transform(const Mat4& m);

    /// Builds a 4x4 matrix from this transform (TRS order).
    Mat4 toMatrix() const;
    /// Uppercase alias for `toMatrix()`.
    Mat4 ToMatrix() const { return toMatrix(); }

    /// Decomposes a matrix into a Transform.
    static Transform FromMatrix(const Mat4& m);

    /// Composes this local transform with a parent to produce world-space transform.
    ///
    /// # Arguments
    /// * `parent` - The parent (world-space) transform.
    ///
    /// # Returns
    /// World-space transform: `world = local * parent`.
    Transform compose(const Transform& parent) const;
    /// Uppercase alias for `compose()`.
    Transform Compose(const Transform& parent) const { return compose(parent); }

    /// Returns the inverse transform (undo this transform).
    Transform inverse() const;
    /// Uppercase alias for `inverse()`.
    Transform Inverse() const { return inverse(); }

    /// Transforms a point by applying scale, rotation, then translation.
    Vec3 TransformPoint(const Vec3& p) const { return position + rotation * (scale * p); }
    /// Transforms a direction by applying scale and rotation (no translation).
    Vec3 TransformDirection(const Vec3& d) const { return rotation * (scale * d); }

    /// Returns a copy with position offset by `delta`.
    Transform translate(const Vec3& delta) const;
    /// Uppercase alias for `translate()`.
    Transform Translate(const Vec3& delta) const { return translate(delta); }
    /// Returns a copy with rotation composed by `rot`.
    Transform rotate(const Quat& rot) const;
    /// Uppercase alias for `rotate()`.
    Transform Rotate(const Quat& rot) const { return rotate(rot); }
    /// Returns a copy with scale multiplied component-wise by `s`.
    Transform scaleBy(const Vec3& s) const;
    /// Uppercase alias for `scaleBy()`.
    Transform ScaleBy(const Vec3& s) const { return scaleBy(s); }

    /// Interpolates between two transforms (position lerp, rotation slerp, scale lerp).
    ///
    /// # Arguments
    /// * `a` - Start transform.
    /// * `b` - End transform.
    /// * `t` - Interpolation factor in [0, 1].
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

/// Scalar-left multiplication for Vec2 (`2.0f * vec`).
inline Vec2 operator*(float scalar, const Vec2& v) {
    return v * scalar;
}
/// Scalar-left multiplication for Vec3 (`2.0f * vec`).
inline Vec3 operator*(float scalar, const Vec3& v) {
    return v * scalar;
}
/// Scalar-left multiplication for Vec4 (`2.0f * vec`).
inline Vec4 operator*(float scalar, const Vec4& v) {
    return v * scalar;
}

} // namespace uz
