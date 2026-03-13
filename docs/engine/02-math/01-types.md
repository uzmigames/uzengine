# Math — Core Types

> **Status**: Done (Milestone 1.1.0a)
> **Source**: `runtime/engine/math/` (split headers)
> **Depends on**: DirectXMath (third-party, header-only)

## Purpose

Unified math library with thin wrappers over DirectXMath for SIMD-accelerated vector, matrix, and quaternion operations. All types are DirectX-friendly: left-handed coordinate system, column-major matrices, XMVECTOR-compatible via `toXM()`.

**Header files** (includes these automatically):
- `math_defs.h` — constants (π, τ, e, √2, √3) and conversion functions
- `math_funcs.h` — scalar utilities (clamp, lerp, ease, smoothstep, etc.)
- `vector2.h` — 2D float vector
- `vector3.h` — 3D float vector with SIMD
- `vector4.h` — 4D float vector (homogeneous coordinates)
- `quaternion.h` — Unit quaternion for 3D rotations
- `mat4.h` — 4x4 matrix for transforms and projections
- `transform.h` — Decomposed TRS (position + rotation + scale)

## Coordinate System

**Left-handed, Z-up**: +X forward, +Y right, +Z up. This matches Unreal Engine's coordinate system. DirectXMath uses Y-up natively, so our `LookAt`/`Perspective` helpers account for the axis remapping internally.

## Types

### Vec2

2D float vector for UI coordinates, texture UVs, and screen-space math. **Scalar operations (no SIMD)** — 2D math doesn't benefit from SIMD at this scale.

```cpp
Vec2 uv{0.5f, 0.5f};
Vec2 screen = uv * Vec2{1920.0f, 1080.0f};
float len = screen.Length();
```

Memory: `[x: f32, y: f32]` — 8 bytes, no padding.

### Vec3

**Primary 3D vector** for positions, directions, normals, scales. SIMD-accelerated via DirectXMath through `toXM()` conversion.

```cpp
Vec3 a{1.0f, 2.0f, 3.0f};
Vec3 b{4.0f, 5.0f, 6.0f};
float d = a.Dot(b);           // SIMD: XMVector3Dot
Vec3 c = a.Cross(b);          // SIMD: XMVector3Cross
Vec3 n = a.Normalized();      // SIMD: XMVector3Normalize
float dist = (b - a).Length(); // SIMD: XMVector3Length
```

Memory: `[x: f32, y: f32, z: f32]` — 12 bytes, NOT 16-byte aligned. Use `toXM()` for SIMD operations requiring `XMVECTOR`.

Key operations:
- `Dot(v)`, `Cross(v)`, `Length()`, `LengthSq()`, `Normalized()`
- `Distance(a, b)` — static
- `Lerp(a, b, t)`, `Reflect(dir, normal)` — static
- Statics: `Zero()`, `One()`, `Up()`, `Right()`, `Forward()`, `UnitX/Y/Z()`

### Vec4

4D float vector for homogeneous coordinates, colors (RGBA), and shader constant packing. SIMD-accelerated.

```cpp
Vec4 color{1.0f, 0.0f, 0.0f, 1.0f};  // red, opaque
Vec4 clip = viewProj * Vec4{pos.x, pos.y, pos.z, 1.0f};
```

Memory: `[x: f32, y: f32, z: f32, w: f32]` — 16 bytes, naturally XMVECTOR-aligned.

### Mat4

4x4 matrix stored as `XMFLOAT4X4` (column-major, DirectX convention). All operations use SIMD via DirectXMath.

```cpp
Mat4 view = Mat4::LookAt(eye, target, Vec3::Up());
Mat4 proj = Mat4::Perspective(60.0f, 16.0f/9.0f, 0.1f, 1000.0f);
Mat4 vp = view * proj;  // SIMD: XMMatrixMultiply
Mat4 inv = vp.Inverse(); // SIMD: XMMatrixInverse
```

Key operations:
- `operator*` — matrix multiplication (SIMD)
- `Inverse()`, `Transpose()`, `Determinant()`
- `TransformPoint(Vec3)`, `TransformDirection(Vec3)`
- Statics: `Identity()`, `Translation(v)`, `RotationX/Y/Z(angle)`, `Scaling(v)`
- `LookAt(eye, target, up)`, `Perspective(fov, aspect, near, far)`, `Ortho(...)`

### Quat

Quaternion for 3D rotations. Stored as `[x, y, z, w]`. SIMD-accelerated slerp/lerp.

```cpp
Quat a = Quat::FromAxisAngle(Vec3::Up(), 45.0f);  // 45 degrees around Z (up)
Quat b = Quat::FromAxisAngle(Vec3::Right(), 30.0f);
Quat c = Quat::Slerp(a, b, 0.5f);  // SIMD: XMQuaternionSlerp
Mat4 m = c.ToMatrix();               // SIMD: XMMatrixRotationQuaternion
```

Key operations:
- `Slerp(a, b, t)`, `Lerp(a, b, t)` (unnormalized lerp)
- `ToMatrix()` — convert to rotation matrix
- `FromAxisAngle(axis, degrees)` — static constructor
- `FromEuler(pitch, yaw, roll)` — static constructor
- `Identity()` — no rotation

### Math Scalar Functions (`uz::Math::`)

Common scalar utilities via `runtime/engine/math/math_funcs.h`. All inline, zero overhead.

```cpp
// Clamping and comparison
bool is_zero_approx(real_t val);           // Within EPSILON?
bool is_equal_approx(real_t a, real_t b);  // Approximately equal?
real_t clamp(real_t val, real_t min_val, real_t max_val);

// Interpolation and easing
real_t lerp(real_t a, real_t b, real_t t);           // Linear interpolation
real_t inverse_lerp(real_t a, real_t b, real_t val); // Solve for t
real_t smoothstep(real_t from, real_t to, real_t val); // Hermite smoothing
real_t ease(real_t val, real_t curve);               // Ease-in/ease-out

// Curves and wrapping
real_t cubic_interpolate(real_t p0, real_t p1, real_t p2, real_t p3, real_t t);
real_t remap(real_t val, real_t istart, real_t istop, real_t ostart, real_t ostop);
real_t snap(real_t val, real_t step);
real_t fposmod(real_t val, real_t length);  // Wrap to [0, length)
int posmod(int val, int length);

// Bit operations and tests
real_t sign(real_t val);     // -1, 0, or 1
real_t fract(real_t val);    // Fractional part [0, 1)
real_t step(real_t edge, real_t val); // 0 or 1

// Audio
real_t linear_to_db(real_t linear);
real_t db_to_linear(real_t db);
```

## Engine Comparison

| Engine | Math Library | SIMD | Coordinate System |
|--------|-------------|------|------------------|
| **UE5** | Custom (`TVector<T>`, `TQuat<T>`, `TMatrix<T>`) | Conditional via `VectorRegister` | Left-handed |
| **Godot** | Custom (`Vector3`, `Basis`, `Quaternion`) | No explicit SIMD (`real_t` typedef) | Right-handed (Y-up) |
| **Unity** | Unity.Mathematics (`float3`, `quaternion`) | Burst maps to SIMD | Left-handed (Y-up) |
| **UzEngine** | DirectXMath wrapper (`Vec3`, `Quat`, `Mat4`) | Always SIMD (SSE2/SSE4/AVX2) | Left-handed (Z-up) |

**Why DirectXMath:** We're DX12-first — DirectXMath is the native math lib with zero impedance mismatch. Every operation is SIMD-accelerated by default, not conditionally. Header-only, MIT, maintained by Microsoft.

## Performance (Release, SSE4.2)

| Operation | Approx. Cycles |
|-----------|---------------|
| Vec3 dot/cross/normalize | 1-3 |
| Mat4 multiply | ~20 |
| Quat slerp | ~15 |
| toXM() / fromXM() conversion | 0 (optimized away) |

## SIMD Integration Pattern

All types store data as plain floats (12 or 16 bytes). When an operation needs SIMD, it converts via `toXM()`:

```cpp
// Internal implementation of Vec3::Dot
float Vec3::Dot(const Vec3& other) const {
    XMVECTOR va = toXM();           // load 3 floats → 128-bit register
    XMVECTOR vb = other.toXM();
    XMVECTOR result = XMVector3Dot(va, vb);  // SIMD dot product
    return XMVectorGetX(result);    // extract scalar
}
```

The `toXM()` call compiles to a single `MOVSS`/`MOVSD` instruction (or nothing, if the compiler inlines and keeps values in registers). In release builds, the entire conversion chain is optimized away.
