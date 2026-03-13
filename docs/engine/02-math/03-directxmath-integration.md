# Math — DirectXMath Integration

> **Status**: Done (Milestone 1.1.0a)
> **Source**: `runtime/engine/math/` (split headers: vector2.h, vector3.h, vector4.h, quaternion.h, mat4.h, transform.h)
> **Depends on**: DirectXMath (Microsoft, header-only, MIT)

## Why DirectXMath

| Option | Pros | Cons |
|--------|------|------|
| **DirectXMath** (chosen) | DX12-native, always SIMD, header-only, MIT, maintained by Microsoft | Windows-centric (SSE on x86, NEON on ARM) |
| **GLM** | Popular, easy API | Not SIMD by default, needs extension macros |
| **Eigen** | Powerful linear algebra | Overkill for game math, heavy |
| **Custom** | Full control | Reinventing the wheel, bugs, maintenance burden |

We're DX12-first. DirectXMath is the native math library with zero impedance mismatch to the GPU API. Every operation uses SIMD intrinsics by default (SSE2 on x86, NEON on ARM), not as an opt-in mode.

## Conversion Pattern: `toXM()` / `fromXM()`

UzEngine types (`Vec3`, `Vec4`, `Mat4`, `Quat`) store data as plain floats for easy debugging, serialization, and ECS storage. When SIMD operations are needed, they convert to DirectXMath's `XMVECTOR`/`XMMATRIX` types:

```
Vec3 (12 bytes, plain floats)
  │
  ├── toXM() ──→ XMVECTOR (128-bit SIMD register)
  │                 │
  │                 ├── XMVector3Dot()
  │                 ├── XMVector3Cross()
  │                 ├── XMVector3Normalize()
  │                 └── ...
  │                 │
  │                 ▼
  └── Vec3(XMVECTOR) ──→ back to Vec3 (store result)
```

### Zero-Cost Conversion

In release builds with optimizations, the compiler inlines `toXM()` and `Vec3(XMVECTOR)` to register loads/stores. The entire chain (load → operate → store) compiles to a handful of SSE instructions with no function call overhead.

```cpp
// User code:
Vec3 n = a.Cross(b);

// Compiler output (conceptual):
// MOVSS xmm0, [a.x]       ; load a
// MOVSS xmm1, [b.x]       ; load b
// ... SSE cross product ...
// MOVSS [n.x], xmm2       ; store result
```

## Key Types Mapping

| UzEngine | DirectXMath | Storage | SIMD |
|----------|-----------|---------|------|
| `Vec2` | `XMFLOAT2` | 8 bytes | No (scalar) |
| `Vec3` | `XMFLOAT3` → `XMVECTOR` | 12 bytes | Yes via `toXM()` |
| `Vec4` | `XMFLOAT4` → `XMVECTOR` | 16 bytes | Yes via `toXM()` |
| `Mat4` | `XMFLOAT4X4` → `XMMATRIX` | 64 bytes | Yes via `toXM()` |
| `Quat` | `XMFLOAT4` → `XMVECTOR` | 16 bytes | Yes via `toXM()` |

## DirectXMath Functions Used

| UzEngine Method | DirectXMath Function |
|----------------|---------------------|
| `Vec3::Dot()` | `XMVector3Dot()` |
| `Vec3::Cross()` | `XMVector3Cross()` |
| `Vec3::Normalized()` | `XMVector3Normalize()` |
| `Vec3::Length()` | `XMVector3Length()` |
| `Mat4::operator*` | `XMMatrixMultiply()` |
| `Mat4::Inverse()` | `XMMatrixInverse()` |
| `Mat4::LookAt()` | `XMMatrixLookAtLH()` |
| `Mat4::Perspective()` | `XMMatrixPerspectiveFovLH()` |
| `Quat::Slerp()` | `XMQuaternionSlerp()` |
| `Quat::ToMatrix()` | `XMMatrixRotationQuaternion()` |
| `Quat::FromAxisAngle()` | `XMQuaternionRotationAxis()` |

## Struct-Return ABI (MinGW/Clang Compatibility)

DirectXMath's `directx-headers` package handles the struct-return ABI difference between MSVC and MinGW/Clang:

```cpp
#if defined(_MSC_VER) || !defined(_WIN32)
    // MSVC: return XMMATRIX directly (passes in registers)
#else
    // MinGW/Clang: use out-parameter (struct return via stack)
#endif
```

This was a specific compatibility issue discovered during Zig (Clang) builds. The `directx-headers` SDK (v614) handles it transparently. We use `directx-headers`, NOT Zig's built-in MinGW d3d12.h (v602, which lacks this fix).

## Rules

1. **ALWAYS** use `Vec3`/`Mat4`/`Quat` wrapper types in engine code — never raw `XMVECTOR`/`XMMATRIX`
2. **NEVER** store `XMVECTOR`/`XMMATRIX` in structs or containers — they have strict alignment requirements that break in arrays and ECS storage
3. Use `toXM()` only within function bodies where SIMD operations are needed
4. Use `directx-headers` (SDK 614), not Zig's built-in MinGW headers
