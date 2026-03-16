<!-- doc-meta
type: architecture_guide
title: UzEngine Coding Standards and Conventions
description: C++ coding standards, naming conventions, depth coordinate system (reversed-Z), and architectural patterns for UzEngine development.
status: active
date: 2026-03-16
author: Engineering Team
tags: [architecture, standards, conventions, coding, depth-conventions, reversed-z]
references: [docs/architecture/rhi-abstraction.md, docs/analysis/rendering/ue5-reversed-z-shadow-maps.md]
-->

# UzEngine Coding Standards and Conventions

This document establishes the coding standards, conventions, and architectural patterns that all UzEngine code must follow.

---

## Reversed-Z Depth Convention

UzEngine uses **reversed-Z for the main view** and **standard-Z for shadow maps**. This matches UE5's design and provides better depth precision for distant objects.

### Depth Ranges and Clear Values

| Context | Near Value | Far Value | Clear Value | Depth Test | Constants |
|---------|-----------|-----------|-------------|------------|-----------|
| **Main view** | 1.0 | 0.0 | `kDepthFar` (0.0) | `GreaterEqual` | `kDepthNear`, `kDepthFar` |
| **Shadow maps** | 0.0 | 1.0 | `kShadowDepthClear` (1.0) | `LessEqual` | `kShadowDepthNear`, `kShadowDepthFar` |

**Key distinctions**:
- **Main view (reversed-Z)**: Near plane = 1.0, far plane = 0.0. Clearing uses `kDepthFar` (0.0). Depth test uses `GreaterEqual` to keep larger values (closer geometry).
- **Shadow maps (standard-Z)**: Near plane = 0.0, far plane = 1.0. Clearing uses `kShadowDepthClear` (1.0, white). Depth test uses `LessEqual` to keep smaller values (closer geometry).

### Rules

1. **NEVER use raw `0.0f` or `1.0f` for depth operations** — use the named constants from `rhi_types.h`:
   ```cpp
   // ❌ WRONG
   cmd->ClearDepth(1.0f);
   cmd->ClearDepth(0.0f);

   // ✅ CORRECT
   cmd->ClearDepth(kDepthFar);       // For main view (reversed-Z)
   cmd->ClearDepth(kShadowDepthClear); // For shadow maps (standard-Z)
   ```

2. **Shadow sampling**: Scene world position is reprojected into light-space (standard-Z ortho) before comparison. Both sides of `SampleCmp` are in the same standard-Z space.
   ```cpp
   // In shadow sampling shader:
   // sceneDepth and shadowDepth are both in [0, 1] standard-Z range
   float shadow = shadowTexture.SampleCmp(shadowSampler, uv, sceneDepth);
   ```

3. **New render passes default to `clearDepth = kDepthFar`** (reversed-Z). Shadow passes must explicitly override to `kShadowDepthClear`.
   ```cpp
   // ✅ CORRECT — new pass defaults to reversed-Z
   RenderPass pass;
   pass.SetClearDepth(kDepthFar);  // Explicitly for clarity, or rely on default

   // ✅ CORRECT — shadow pass explicitly overrides
   ShadowRenderPass shadowPass;
   shadowPass.SetClearDepth(kShadowDepthClear);  // Explicit for shadow map
   ```

4. **Matrix construction**:
   - `Mat4::Perspective()` and `Mat4::OrthographicReversedZ()` produce reversed-Z matrices (for main view)
   - `Mat4::Orthographic()` produces standard-Z matrices (for shadow maps and specialized projections)
   ```cpp
   // ✅ CORRECT — use OrthographicReversedZ for main camera
   Mat4 mainViewProj = Mat4::OrthographicReversedZ(left, right, bottom, top, nearZ, farZ);

   // ✅ CORRECT — use standard Orthographic for shadows
   Mat4 shadowProj = Mat4::Orthographic(left, right, bottom, top, nearZ, farZ);
   ```

5. **Depth comparison functions**:
   - Main view passes: `RHICompareFunc::GreaterEqual` (keep if new depth > stored, because larger = closer in reversed-Z)
   - Shadow maps: `RHICompareFunc::LessEqual` (keep if new depth ≤ stored, because standard-Z)
   ```cpp
   // ✅ CORRECT — main view pipeline
   depthStencilState.SetDepthFunc(RHICompareFunc::GreaterEqual);

   // ✅ CORRECT — shadow map pipeline
   depthStencilState.SetDepthFunc(RHICompareFunc::LessEqual);
   ```

### Depth Constant Definitions

All depth constants are defined in `runtime/src/engine/renderer/rhi/rhi_types.h`:

```cpp
// Main view (reversed-Z)
constexpr float kDepthNear = 1.0f;  // Closest (near plane)
constexpr float kDepthFar = 0.0f;   // Farthest (far plane)

// Shadow maps (standard-Z)
constexpr float kShadowDepthNear = 0.0f;   // Closest
constexpr float kShadowDepthFar = 1.0f;    // Farthest
constexpr float kShadowDepthClear = 1.0f;  // Clear value (white = "no occluder")
```

### GPU Shaders

In HLSL/GLSL shaders, respect the depth convention:

```hlsl
// Main view shader — reversed-Z (larger = closer)
float linearDepth = (depthSample - kDepthFar) / (kDepthNear - kDepthFar);

// Shadow sampling shader — standard-Z (smaller = closer)
float shadowFactor = shadowTexture.SampleCmp(
    shadowSampler,
    shadowUV,
    sceneDepth  // Both sceneDepth and shadow texture are standard-Z [0, 1]
);
```

### Critical: NDC Z Values Must Match Depth Convention

**DANGER**: Any code that constructs clip-space or NDC positions with hardcoded Z values (0.0 or 1.0) MUST use the correct convention. Using the wrong convention produces an inverted or inflated frustum that causes silent rendering failures.

#### Correct NDC Z Mapping

| Convention | Near Plane Z | Far Plane Z | Why |
|-----------|-------------|------------|-----|
| Standard-Z (shadows) | 0.0 | 1.0 | Typical D3D projection: smaller = closer |
| **Reversed-Z (main view)** | **1.0** | **0.0** | UE5 parity: larger = closer, better far precision |

#### Bug Pattern: Inverted Frustum

Using z=0 for near and z=1 for far with a reversed-Z `invViewProj` produces an inverted frustum:
- The un-projected "near" point lands at the far plane distance
- The bounding sphere becomes ~90x larger than correct
- Shadows disappear or become invisible

Example of the bug (from `shadow_cascade.cpp` prior to fix):
```cpp
// ❌ WRONG — these are standard-Z NDC corners, not reversed-Z
// When un-projected with reversed-Z invViewProj, produces inverted frustum
float3 ndcCorners[8] = {
    {-1, -1, 0}, {+1, -1, 0},  // near plane (WRONG: z=0)
    {-1, +1, 0}, {+1, +1, 0},
    {-1, -1, 1}, {+1, -1, 1},  // far plane
    {-1, +1, 1}, {+1, +1, 1}
};
```

#### Fix: Use Correct Reversed-Z NDC Z Values

```cpp
// ✅ CORRECT — reversed-Z NDC corners
float3 ndcCorners[8] = {
    {-1, -1, 1}, {+1, -1, 1},  // near plane (z=1 in reversed-Z)
    {-1, +1, 1}, {+1, +1, 1},
    {-1, -1, 0}, {+1, -1, 0},  // far plane (z=0 in reversed-Z)
    {-1, +1, 0}, {+1, +1, 0}
};
```

#### Known Affected Code Paths

All of these have been verified or fixed:
- **Frustum corner computation** (`ComputeFrustumCorners`) — **FIXED in shadow_cascade.cpp:78-87**
- **Sky shader depth output** — verified correct
- **Depth linearization helpers** — verified correct
- **Hi-Z pyramid (future)** — must use min() not max() for farthest value

#### Migration Checklist

When porting or adapting rendering code from other engines:

```
☐ Verify projection matrix type (reversed-Z vs standard-Z)
☐ Check depth clear values (main view vs shadow context)
☐ Verify depth comparison function (GreaterEqual vs LessEqual)
☐ Check comparison sampler function (Less for shadows)
☐ **☐ Check any hardcoded NDC Z values (0.0 or 1.0) match convention**
☐ Update depth normalization formulas if present
☐ Test with a demo and verify visually (no black/white depth artifacts)
```

---

## Coordinate System

UzEngine uses **Z-up, X-forward, left-handed** coordinates, matching UE5.

| Axis | Direction | Notes |
|------|-----------|-------|
| **X** | Forward | Forward direction for camera and actors |
| **Y** | Right | Right direction (perpendicular to X and Z) |
| **Z** | Up | Vertical axis (opposite of Y-up engines like most game dev tutorials) |

### Vector Constants

```cpp
Vec3::Forward()  = (1, 0, 0)   // +X direction
Vec3::Right()    = (0, 1, 0)   // +Y direction
Vec3::Up()       = (0, 0, 1)   // +Z direction (vertical)
```

### Matrix Layout

- **Column-major matrices** (DirectXMath compatible)
- Transforms are applied right-to-left: `position' = position * Scale * Rotation * Translation`

---

## Naming Conventions

### Classes, Structs, and Type Aliases

- **PascalCase** for public types
- **Prefixes**:
  - `I` for abstract interfaces: `IRenderable`, `ISerializable`
  - `F` for POD/data types (legacy UE5 convention, not mandatory): `FLinearColor`, `FMatrix`
  - `C` for classes with methods: `CScene`, `CCamera`, `CInputManager`

Examples:
```cpp
class CInputSystem { ... };
struct STransform { ... };
class IRenderable { virtual void Render() = 0; };
using Handle = uint32_t;
```

### Functions and Methods

- **snake_case** for public C functions
- **PascalCase** for C++ methods
- **Prefix conventions**:
  - `Get` for read-only queries that return by value: `GetPosition()`
  - `GetRef` for queries returning references: `GetRefTransform()`
  - `Set` for mutations: `SetPosition()`
  - `Is`/`Has` for boolean queries: `IsVisible()`, `HasComponent()`
  - `On` for event handlers: `OnMouseDown()`, `OnEntityDestroyed()`

Examples:
```cpp
Vec3 GetPosition() const;
Transform& GetRefTransform();
void SetPosition(const Vec3& pos);
bool IsVisible() const;
void OnEntitySpawned(EntityId id);
```

### Variables and Constants

- **snake_case** for local variables and parameters
- **kPascalCase** for compile-time constants
- **SCREAMING_SNAKE_CASE** for preprocessor defines (rare, prefer constants)

Examples:
```cpp
Vec3 world_position = transform.GetPosition();
const float kGravity = -9.81f;
const float kAirResistance = 0.95f;

#define ENABLE_PROFILING 1  // Only for compile-time feature flags
```

### File Names

- **snake_case** for filenames
- Organize by domain: `src/core/logging/uz_log.h`, `src/renderer/pbr/pbr_material.cpp`
- One public type per file (with rare exceptions for tightly-coupled POD types)

---

## Error Handling

### Assertions and Checks

- `UZ_CHECK(condition)` — for invariants that must never be violated; crashes in debug, optimized away in release
- `UZ_ENSURE(condition)` — for recoverable runtime violations; logs error and continues in both debug and release
- `UZ_ASSERT_MSG(condition, format, ...)` — for debug-only checks with custom messages

Examples:
```cpp
// Invariant: Entity must be valid
UZ_CHECK(entity.IsValid());

// Recovery: Handle missing component gracefully
if (!world.HasComponent<Transform>(entity)) {
    UZ_ENSURE(false);  // Log the unexpected state
    return default_value;
}
```

### Function Result Types

Use `Result<T>` for fallible operations:

```cpp
Result<Texture> LoadTexture(const std::string& path) {
    if (!file_exists(path)) {
        return Err(TextureLoadError::NotFound);
    }
    return Ok(texture);
}
```

---

## Memory Management

### Ownership Rules

1. **Unique ownership** (`std::unique_ptr<T>`): Single owner, automatic cleanup
2. **Shared ownership** (`std::shared_ptr<T>`): Multiple owners, reference-counted cleanup (rare in hot paths)
3. **Stack ownership** (`T` value type): Automatic when variable goes out of scope
4. **Arena allocation** (custom `Arena` allocator): For frame-scoped allocations with batch cleanup

### Rules

- **NO raw `new`/`delete`** in new code — use `std::unique_ptr` or stack allocation
- **Arena allocators for temporary data** — frame-scoped vectors, strings, and intermediate buffers
- **GPU resources are always `std::unique_ptr<T>`** — RHI handles cleanup in destructor

Examples:
```cpp
// ✅ CORRECT — unique ownership
std::unique_ptr<CScene> scene = std::make_unique<CScene>();

// ✅ CORRECT — stack allocation for temporary data
std::vector<Entity> visible_entities;
visible_entities.reserve(1000);

// ✅ CORRECT — arena allocation for frame-scoped data
Arena frame_arena(64 * 1024);  // 64 KB arena
auto temp_data = frame_arena.Allocate<float>(1000);
```

---

## Logging

All logging must use the `UZ_LOG` macro with a declared category:

```cpp
// Declare once per translation unit
UZ_DECLARE_LOG_CATEGORY(MySystem);

// Log with category
UZ_LOG(MySystem, Info, "Loaded asset: {}", path);
UZ_LOG(MySystem, Warning, "Missing component on entity {}", entity_id);
UZ_LOG(MySystem, Error, "Failed to initialize: {}", error_reason);
```

### Log Categories

Every subsystem must declare its own category:

```cpp
// In header
UZ_DECLARE_LOG_CATEGORY(Asset);
UZ_DECLARE_LOG_CATEGORY(Render);
UZ_DECLARE_LOG_CATEGORY(Physics);
UZ_DECLARE_LOG_CATEGORY(Script);

// In implementation file
UZ_DEFINE_LOG_CATEGORY(Asset);
```

**NEVER use**:
- `printf`, `cout`, `std::cerr`
- Direct `spdlog` calls
- Custom logging functions

---

## Code Organization

### File Structure

Each file should have this structure:

```cpp
// File: src/core/transform/transform.h

#pragma once

#include <core/math/vec3.h>

namespace uz {

/// 3D spatial transform with position, rotation, and scale.
/// Uses Z-up coordinates and quaternion rotation.
class CTransform {
public:
    /// Returns the local-to-world matrix.
    Mat4 GetWorldMatrix() const;

    /// Sets world position.
    void SetWorldPosition(const Vec3& pos);

private:
    Vec3 position_;
    Quat rotation_;
    Vec3 scale_;
};

}  // namespace uz
```

### Sections Within Files

Use section markers to organize large implementation files:

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// Initialization & Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void CScene::Init() {
    // ...
}

// ─────────────────────────────────────────────────────────────────────────────
// Entity Management
// ─────────────────────────────────────────────────────────────────────────────

Entity CScene::CreateEntity() {
    // ...
}
```

---

## Function Documentation

All public functions must have Rust-style documentation comments (`///`):

```cpp
/// Loads a texture from disk into the texture cache.
///
/// The path is resolved relative to the assets root directory.
/// If the texture is already cached, returns the cached handle.
///
/// # Parameters
/// - `path`: Virtual path to the texture (e.g., "textures/pbr/stone.uzasset")
/// - `flags`: Load flags (async, mipgen, compression, etc.)
///
/// # Returns
/// - Non-zero handle on success
/// - Zero (invalid handle) on failure; logs `Asset::Error` with details
///
/// # Side Effects
/// - Increments texture cache ref count
/// - May trigger asynchronous GPU uploads depending on `flags`
///
/// # Example
/// ```cpp
/// Handle tex = asset_manager->LoadTexture("textures/pbr/stone.uzasset", TextureLoadFlags::GenerateMips);
/// if (tex) {
///     material->SetTexture(tex);
/// }
/// ```
Handle LoadTexture(
    const std::string& path,
    TextureLoadFlags flags = TextureLoadFlags::None
);
```

**Rules**:
- Every public function/method/type must have docs
- Include parameters, return values, side effects, and examples
- For override methods, document differences from the base class
- Use code blocks with language tags: ` ```cpp `, ` ```hlsl `

---

## SOLID Principles

### S — Single Responsibility

Each class/function does one thing:

```cpp
// ❌ WRONG — CRenderer does rendering, memory management, and profiling
class CRenderer {
    void Render();
    void* Allocate(size_t size);
    void ProfileFrame();
};

// ✅ CORRECT — responsibilities are separated
class CRenderer {
    void Render();
};
class CMemoryPool {
    void* Allocate(size_t size);
};
class CProfiler {
    void ProfileFrame();
};
```

### O — Open/Closed

Extend via new types, not by modifying existing ones:

```cpp
// ❌ WRONG — modify RenderPass to add new behavior
class RenderPass {
    void Execute() {
        if (pass_type == Lighting) { /* ... */ }
        else if (pass_type == Shadows) { /* ... */ }
        else if (pass_type == Transparency) { /* ... */ }
    }
};

// ✅ CORRECT — use virtual interfaces
class IRenderPass {
    virtual void Execute() = 0;
};
class LightingPass : public IRenderPass {
    void Execute() override { /* lighting logic */ }
};
class ShadowPass : public IRenderPass {
    void Execute() override { /* shadow logic */ }
};
```

### L — Liskov Substitution

Any subclass must be substitutable for its base:

```cpp
// ✅ CORRECT — DX12Backend can replace VulkanBackend
class IRHIBackend {
    virtual void UploadBuffer(...) = 0;
};
class DX12Backend : public IRHIBackend {
    void UploadBuffer(...) override { /* DX12 impl */ }
};
class VulkanBackend : public IRHIBackend {
    void UploadBuffer(...) override { /* Vulkan impl */ }
};

// Use interchangeably
IRHIBackend* backend = /* choose at runtime */;
backend->UploadBuffer(...);  // Works with either backend
```

### I — Interface Segregation

Don't force classes to depend on methods they don't use:

```cpp
// ❌ WRONG — IGameObject forces all objects to serialize
class IGameObject {
    virtual void Update() = 0;
    virtual void Serialize(Archive& ar) = 0;  // Not all objects serialize
    virtual void Render() = 0;                 // Not all objects render
};

// ✅ CORRECT — segregate into focused interfaces
class IGameObject {
    virtual void Update() = 0;
};
class ISerializable {
    virtual void Serialize(Archive& ar) = 0;
};
class IRenderable {
    virtual void Render() = 0;
};

// Implement only what's needed
class Player : public IGameObject, public ISerializable, public IRenderable {};
class Physics : public IGameObject { /* no serialization or rendering */ };
```

### D — Dependency Inversion

Depend on abstractions, not concrete implementations:

```cpp
// ❌ WRONG — CRenderer depends on concrete DX12Device
class CRenderer {
    DX12Device* device_;  // Hard-coded DX12
};

// ✅ CORRECT — CRenderer depends on IRHIDevice abstraction
class CRenderer {
    IRHIDevice* device_;  // Works with any RHI backend
};
```

---

## Performance Expectations

### Profiling

Every function that performs non-trivial work must have profiling markers:

```cpp
void CRenderer::Render() {
    UZ_PROFILE_SCOPE("Render");

    {
        UZ_PROFILE_SCOPE("Clear Depth");
        cmd->ClearDepth(kDepthFar);
    }

    {
        UZ_PROFILE_SCOPE("Render Passes");
        RenderGBuffer();
        RenderLighting();
    }
}
```

### Optimization Rules

1. **Profile before optimizing** — use Tracy to find actual bottlenecks
2. **Avoid premature optimization** — simple code is often fast enough
3. **Cache-friendly data layouts** — structure of arrays beats array of structures for bulk operations
4. **SIMD for hot paths** — use DirectXMath for vector/matrix operations

---

## Testing

### Coverage Requirement

All new code must have **95%+ unit test coverage**. See `docs/guides/testing-guide.md` for patterns.

### Test File Organization

Mirror source structure:

```
src/core/math/vec3.h                → tests/core/test_vec3.cpp
src/renderer/pbr/pbr_material.cpp   → tests/renderer/test_pbr_material.cpp
```

---

## References

- **Depth conventions**: [UE5 Reversed-Z Shadow Map Implementation](../analysis/rendering/ue5-reversed-z-shadow-maps.md)
- **RHI abstraction**: [RHI Abstraction Patterns](rhi-abstraction.md)
- **Testing patterns**: [UzEngine Testing Guide](../guides/testing-guide.md)
- **SOLID principles**: Clean Code by Robert C. Martin (recommended reading)

