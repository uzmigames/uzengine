# Math Module Reference

SIMD-accelerated math types via DirectXMath.

> **Header**: `runtime/engine/math/engine_math.h`
> **Guide**: [Math Library](../../guides/math/overview.md)

## Vec2

2D float vector. Zero-initialized by default.

```cpp
Vec2();                        // (0, 0)
Vec2(float x, float y);
explicit Vec2(float scalar);   // (s, s)
explicit Vec2(const float* ptr);
```

| Method | Description |
|--------|-------------|
| `Dot(Vec2)` | Dot product |
| `Length()` | Euclidean length |
| `LengthSquared()` | Squared length (no sqrt) |
| `Normalized()` | Unit vector (new Vec2) |
| `Lerp(Vec2, float t)` | Linear interpolation |

Operators: `+`, `-`, `*`, `/`, `-` (unary), `+=`, `-=`, `*=`, `==`, `!=`

---

## Vec3

3D float vector — primary SIMD vector. Operations use `XMVector3*` intrinsics.

```cpp
Vec3();                         // (0, 0, 0)
Vec3(float x, float y, float z);
explicit Vec3(float scalar);    // (s, s, s)
```

| Method | Description |
|--------|-------------|
| `Dot(Vec3)` | Dot product (SIMD) |
| `Cross(Vec3)` | Cross product (SIMD) |
| `Length()` | Euclidean length (SIMD) |
| `LengthSquared()` | Squared length |
| `Normalized()` | Unit vector |
| `Lerp(Vec3, float t)` | Linear interpolation |
| `Distance(Vec3)` | Distance between points |
| `toXM()` | Convert to `XMVECTOR` |

Static constants: `Zero()`, `One()`, `UnitX()`, `UnitY()`, `UnitZ()`

---

## Vec4

4D float vector (x, y, z, w).

Same interface as Vec3 with the addition of `w` component.

---

## Mat4

4×4 column-major matrix. Storage: `XMFLOAT4X4`.

```cpp
Mat4();                  // Identity matrix
Mat4(const XMFLOAT4X4&);
```

| Method | Description |
|--------|-------------|
| `TransformPoint(Vec3)` | Multiply as position (w=1) |
| `TransformDirection(Vec3)` | Multiply as direction (w=0) |
| `Inverse()` | Matrix inverse |
| `Transpose()` | Matrix transpose |
| `Determinant()` | Scalar determinant |
| `Decompose(Vec3&, Quat&, Vec3&)` | Extract position, rotation, scale |

| Static | Description |
|--------|-------------|
| `Identity()` | 4×4 identity |
| `Translation(Vec3)` | Translation matrix |
| `RotationX/Y/Z(float)` | Axis rotation |
| `Scaling(Vec3)` | Scale matrix |
| `LookAt(Vec3 eye, Vec3 target, Vec3 up)` | View matrix |
| `PerspectiveFov(fov, aspect, near, far)` | Projection matrix |
| `Ortho(w, h, near, far)` | Orthographic projection |

Operators: `*` (matrix multiply), `==`, `!=`

---

## Quat

Quaternion (x, y, z, w). Default: identity (0, 0, 0, 1).

| Method | Description |
|--------|-------------|
| `Rotate(Vec3)` | Rotate a vector by this quaternion |
| `ToMatrix()` | Convert to rotation Mat4 |
| `Inverse()` | Conjugate (unit quaternion inverse) |
| `Dot(Quat)` | Quaternion dot product |
| `Length()` | Quaternion magnitude |
| `Normalized()` | Unit quaternion |

| Static | Description |
|--------|-------------|
| `Identity()` | (0, 0, 0, 1) |
| `FromAxisAngle(Vec3, float)` | Create from axis + angle (radians) |
| `FromMatrix(Mat4)` | Extract from rotation matrix |
| `Slerp(Quat, Quat, float)` | Spherical interpolation |
| `Lerp(Quat, Quat, float)` | Linear interpolation |

---

## Transform

Position (Vec3) + rotation (Quat) + scale (Vec3).

```cpp
Transform t;
t.position = Vec3(10, 0, 0);
t.rotation = Quat::FromAxisAngle(Vec3::UnitY(), 1.57f);
t.scale = Vec3(1, 1, 1);
```

| Method | Description |
|--------|-------------|
| `ToMatrix()` | Compose into 4×4 matrix |
| `Compose(Transform)` | Parent × child composition |
| `Inverse()` | Inverse transform |
| `Lerp(Transform, Transform, float)` | Interpolation |

---

## Geometry Types (`runtime/engine/math/geometry.h`)

### LinearColor

HDR float4 color (linear RGB, for rendering).

| Method | Description |
|--------|-------------|
| `ToSRGB()` | Convert to sRGBColor (gamma-correct) |
| `FromHex(uint32_t)` | Create from 0xRRGGBBAA |

### sRGBColor

LDR uint8 color (sRGB, for storage/UI).

| Method | Description |
|--------|-------------|
| `ToLinear()` | Convert to LinearColor |
| `FromHex(uint32_t)` | Create from 0xRRGGBBAA |

### Plane

Half-space (normal + distance).

| Method | Description |
|--------|-------------|
| `Distance(Vec3)` | Signed distance from point |

### AABB

Axis-aligned bounding box (min, max corners).

| Method | Description |
|--------|-------------|
| `Contains(Vec3)` | Point containment test |
| `Intersects(AABB)` | Box-box intersection |
| `Expand(Vec3)` | Grow to include point |
| `Center()` | Box center |
| `Extents()` | Half-size |

### Frustum

6-plane view frustum for conservative culling.

| Method | Description |
|--------|-------------|
| `FromMatrix(Mat4)` | Extract from view-projection matrix |
| `Contains(AABB)` | Test if box is inside/intersecting |
