# Math Core Specification

## Overview
SIMD-accelerated math primitives using DirectXMath as the computation layer. All types are header-only or minimal .cpp implementation to enable inlining.

## Vector Types

### Vec2
```cpp
struct Vec2 {
    float x = 0, y = 0;

    /// Constructors
    Vec2();
    Vec2(float x, float y);
    explicit Vec2(float scalar);  // (scalar, scalar)

    /// Component access
    float X() const;
    float Y() const;

    /// Operators
    Vec2 operator+(const Vec2& other) const;
    Vec2 operator-(const Vec2& other) const;
    Vec2 operator*(float scalar) const;
    Vec2 operator/(float scalar) const;
    bool operator==(const Vec2& other) const;

    /// Operations
    float dot(const Vec2& other) const;
    float length() const;
    float lengthSquared() const;
    Vec2 normalize() const;

    /// Static constructors
    static Vec2 Zero();
    static Vec2 One();
    static Vec2 UnitX();
    static Vec2 UnitY();
    static Vec2 Lerp(const Vec2& a, const Vec2& b, float t);
};
```

### Vec3
Same as Vec2 but with `z` component. Additional operations:
```cpp
Vec3 cross(const Vec3& other) const;  // Cross product (SIMD)
XMVECTOR toXM() const;  // Convert to DirectXMath for complex ops
```

### Vec4
Same as Vec3 but with `w` component (used for homogeneous coordinates).

## Matrix Type

### Mat4
Column-major (DirectX convention, XMFLOAT4X4).
```cpp
struct Mat4 {
    XMFLOAT4X4 m = identity;

    /// Constructors
    Mat4();  // Identity
    explicit Mat4(const XMMATRIX& xm);

    /// Operators
    Mat4 operator*(const Mat4& other) const;  // Matrix multiply
    Vec4 operator*(const Vec4& v) const;  // Transform point
    Vec3 operator*(const Vec3& v) const;  // Transform direction

    /// Operations
    Mat4 inverse() const;
    Mat4 transpose() const;
    Vec3 transformPoint(const Vec3& p) const;  // (w=1)
    Vec3 transformDirection(const Vec3& d) const;  // (w=0)

    /// Decomposition
    Vec3 getTranslation() const;
    Quat getRotation() const;
    Vec3 getScale() const;

    /// Static constructors
    static Mat4 Identity();
    static Mat4 Translation(const Vec3& t);
    static Mat4 Scale(const Vec3& s);
    static Mat4 Perspective(float fov, float aspect, float near, float far);
    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);
};
```

## Quaternion Type

### Quat
```cpp
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;  // (x, y, z, w) = (axis*sin(θ/2), cos(θ/2))

    /// Constructors
    Quat();  // Identity
    Quat(float x, float y, float z, float w);
    explicit Quat(const XMVECTOR& xm);

    /// Static constructors
    static Quat FromAxisAngle(const Vec3& axis, float angle);
    static Quat FromMatrix(const Mat4& m);

    /// Operators
    Quat operator*(const Quat& other) const;  // Quaternion multiply

    /// Operations
    Quat conjugate() const;
    Quat normalize() const;
    Vec3 rotate(const Vec3& v) const;  // Rotate vector by quaternion

    /// Interpolation
    Quat slerp(const Quat& other, float t) const;  // Spherical interpolation
    Quat lerp(const Quat& other, float t) const;  // Linear interpolation (unnormalized)

    /// Conversion
    Mat4 toMatrix() const;
};
```

## Transform Type

### Transform
```cpp
struct Transform {
    Vec3 position;
    Quat rotation;
    Vec3 scale = Vec3(1, 1, 1);

    /// Constructors
    Transform();  // Identity
    Transform(const Vec3& pos, const Quat& rot, const Vec3& scale);

    /// Conversion
    Mat4 toMatrix() const;  // position * rotation * scale
    static Transform fromMatrix(const Mat4& m);

    /// Hierarchy
    Transform compose(const Transform& parent) const;  // this relative to parent → world
    Transform inverse() const;

    /// Interpolation
    static Transform lerp(const Transform& a, const Transform& b, float t);
};
```

## Scenario: Vector Arithmetic

**Given** a Vec3 `a = (1, 2, 3)` and `b = (4, 5, 6)`
**When** computing `c = a + b`
**Then** `c = (5, 7, 9)`
**And** disassembly shows `paddd` (SIMD add) instruction

## Scenario: Frustum Culling

**Given** a Frustum extracted from view-projection matrix
**And** an AABB representing a bounding box
**When** testing frustum intersection
**Then** conservative culling result is correct (false positives okay, false negatives = bug)

## Design Decisions

### Why Zero-Initialization?
- Default-constructed vectors are (0,0,0), not garbage
- Prevents silent bugs with uninitialized positions
- Cost: none (value semantics, stack allocated)

### Why Column-Major Mat4?
- DirectX/HLSL convention
- Simplifies shader constant buffer layout
- Matches DirectXMath XMFLOAT4X4

### Why Point vs Direction Transform?
- Points need translation (w=1): `p' = M * p`
- Directions ignore translation (w=0): `d' = M * d` (ignores M[3][0:3])
- Prevents accidental translation of velocity/normal vectors

### Why Quat Slerp?
- Smooth rotation interpolation (preserves angular velocity)
- Critical for skeletal animation blending
- Must use normalize() after lerp if using linear interpolation

## SIMD Requirements

All operations on Vec2/Vec3/Vec4 MUST use SIMD when possible:
- `dot()`: `_mm_dp_ps` (SSE4.1) or equivalent
- `cross()`: Direct SIMD implementation
- `length()`: `_mm_length_ps`
- `normalize()`: `_mm_normalize3_ps`

Verify via `zig build` in ReleaseFast mode and inspect disassembly.

## Testing Requirements

### Math Tests
- All constructors initialize correctly
- Arithmetic operators: a + b, a - b, a * scalar
- Vector operations: dot, cross, length, normalize
- Matrix operations: multiply, inverse, transpose
- Quaternion operations: slerp, matrix conversion
- Round-trip conversions: matrix → quat → matrix (numerical stability)

### Edge Cases
- Zero-length vector normalize (should not NaN)
- Singular matrix inverse (graceful handling)
- Very small rotations in slerp (numerical stability)

### Coverage
95%+ line coverage on all math types.
