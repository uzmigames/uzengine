# Math Library

UzEngine's math library wraps DirectXMath for SIMD-accelerated operations,
exposed through ergonomic C++ types.

> **Header**: `runtime/engine/math/engine_math.h`
> **API Reference**: [Math Reference](../../reference/math/index.md)

## Types

| Type | Description |
|------|-------------|
| `Vec2` | 2D float vector (x, y) |
| `Vec3` | 3D float vector — primary SIMD vector (x, y, z) |
| `Vec4` | 4D float vector (x, y, z, w) |
| `Mat4` | 4×4 column-major matrix (XMFLOAT4X4 storage) |
| `Quat` | Quaternion (x, y, z, w) with slerp/lerp |
| `Transform` | Position (Vec3) + rotation (Quat) + scale (Vec3) |
| `LinearColor` | HDR float4 color (linear RGB for rendering) |
| `sRGBColor` | LDR uint8 color (sRGB for storage/UI) |
| `Plane` | Half-space for frustum clipping |
| `AABB` | Axis-aligned bounding box |
| `Frustum` | 6-plane view frustum for culling |

## SIMD Under the Hood

All vector/matrix operations use DirectXMath SIMD intrinsics internally.
The conversion is zero-cost in release builds:

```cpp
Vec3 a(1.0f, 2.0f, 3.0f);
Vec3 b(4.0f, 5.0f, 6.0f);

float d = a.Dot(b);           // XMVector3Dot (SSE)
Vec3 c = a.Cross(b);          // XMVector3Cross (SSE)
Vec3 n = a.Normalized();      // XMVector3Normalize (SSE)
float len = a.Length();        // XMVector3Length (SSE)
```

## Matrix Operations

`Mat4` uses column-major storage (DirectX convention):

```cpp
Mat4 model = Mat4::Identity();
Mat4 view = Mat4::LookAt(eye, target, up);
Mat4 proj = Mat4::PerspectiveFov(fovY, aspect, nearZ, farZ);
Mat4 mvp = model * view * proj;

// Transform points and directions
Vec3 worldPos = mvp.TransformPoint(localPos);
Vec3 worldDir = mvp.TransformDirection(localDir);  // ignores translation

// Decomposition
Vec3 pos, scl;
Quat rot;
mvp.Decompose(pos, rot, scl);
```

## Quaternions

```cpp
Quat q = Quat::FromAxisAngle(Vec3(0, 1, 0), radians);
Quat identity = Quat::Identity();

// Rotate a vector
Vec3 rotated = q.Rotate(direction);

// Interpolation
Quat blended = Quat::Slerp(a, b, t);  // spherical
Quat quick = Quat::Lerp(a, b, t);     // linear (faster, less accurate)

// Convert to/from matrix
Mat4 rotMatrix = q.ToMatrix();
Quat fromMat = Quat::FromMatrix(rotMatrix);
```

## Transform Composition

```cpp
Transform parent;
parent.position = Vec3(10, 0, 0);
parent.rotation = Quat::FromAxisAngle(Vec3::UnitY(), 1.57f);

Transform child;
child.position = Vec3(0, 5, 0);

// World transform = parent × child
Transform world = parent.Compose(child);

// Interpolation (for animation)
Transform blended = Transform::Lerp(a, b, 0.5f);
```

## Color Conversions

```cpp
// Linear (rendering) ↔ sRGB (storage)
LinearColor linear(1.0f, 0.0f, 0.0f, 1.0f);
sRGBColor srgb = linear.ToSRGB();         // gamma-correct conversion
LinearColor back = srgb.ToLinear();

// From hex
sRGBColor red = sRGBColor::FromHex(0xFF0000FF);
```

## Frustum Culling

```cpp
Mat4 viewProj = view * proj;
Frustum frustum = Frustum::FromMatrix(viewProj);

AABB objectBounds(minCorner, maxCorner);
if (frustum.Contains(objectBounds)) {
    // Object is visible — render it
}
```

## Cross-Platform

DirectXMath compiles on all platforms (Windows, Linux, macOS) via a sal.h
compatibility stub that defines Microsoft SAL annotations as no-ops on non-MSVC
compilers. SSE intrinsics are available on all x86_64 targets.

## Related Pages

- [API: Math Reference](../../reference/math/index.md) — Every type and method
- [Overview](../../getting-started/overview.md) — Engine architecture
