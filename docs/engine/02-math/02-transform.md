# Math — Transform & Hierarchy

> **Status**: Done (Milestone 1.1.0a)
> **Source**: `runtime/engine/math/transform.h` (Transform struct)
> **Depends on**: Vec3, Quat, Mat4

## Purpose

The `Transform` type represents a position + rotation + scale in 3D space, with support for parent-child hierarchy composition. It's the bridge between the math library and the scene graph — every entity with spatial presence has a Transform.

## API

```cpp
struct Transform {
    Vec3 position = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 scale    = Vec3::One();

    /// Computes the local-to-world 4x4 matrix: Scale → Rotate → Translate.
    Mat4 toMatrix() const;

    /// Composes this transform with a parent: child * parent.
    /// Used for hierarchy: world = local.compose(parent_world).
    Transform compose(const Transform& parent) const;

    /// Returns the inverse transform (undo this transform).
    Transform inverse() const;
};
```

### Matrix Decomposition

`toMatrix()` builds the TRS (Translation × Rotation × Scale) matrix:

```cpp
Transform t;
t.position = {10.0f, 0.0f, 5.0f};
t.rotation = Quat::FromAxisAngle(Vec3::Up(), 45.0f);
t.scale    = {2.0f, 2.0f, 2.0f};

Mat4 m = t.toMatrix();
// m = Translation(10,0,5) * RotationY(45°) * Scale(2,2,2)
```

### Hierarchy Composition

In a scene graph, a child's world transform is computed by composing with its parent:

```cpp
Transform parent;
parent.position = {100.0f, 0.0f, 0.0f};  // parent at x=100

Transform child;
child.position = {5.0f, 0.0f, 0.0f};     // 5 units from parent

Transform world = child.compose(parent);
// world.position ≈ {105, 0, 0}  (child offset in parent space)
```

The composition order matches UE5's convention: child's local transform is applied first, then parent's.

### Inverse

```cpp
Transform t = ...;
Transform inv = t.inverse();
// inv.compose(t) ≈ Transform::Identity()
```

Used for view matrices (camera inverse), world-to-local conversions, and undo operations.

## Mat4 Convenience Methods

The `Mat4` class provides static factory methods to eliminate common matrix construction patterns duplicated across demos and render passes.

### NormalMatrix

```cpp
/// Compute the normal transformation matrix (for correct lighting).
///
/// Returns transpose(inverse(model)) — used in fragment shaders to transform
/// world-space normals when a model matrix has non-uniform scale or shear.
/// Necessary because normal vectors are dual-space (1-forms) and transform
/// differently than position vectors.
///
/// # Mathematical Background
/// If M is a model matrix, then a normal n should be transformed as:
///   n' = normalize(transpose(inverse(M)) * n)
///
/// This is automatically computed from the model matrix to avoid CPU-GPU
/// synchronization of a separate normal matrix.
///
/// # Returns
/// The 4×4 normal transformation matrix (applies to Vec3 normals via
/// multiplication with the last row [0, 0, 0, 1]).
///
/// # Example
/// ```cpp
/// Mat4 model_matrix = Mat4::TRS(position, rotation, scale);
/// Mat4 normal_matrix = model_matrix.NormalMatrix();
/// // In shader: normal = normalize(normal_matrix * input_normal).xyz
/// ```
///
/// # Performance Note
/// Computing the inverse is O(n³) for 4×4 matrices. Cache the result
/// if applying to many vertices. Consider baking into per-object constant buffer.
Mat4 NormalMatrix() const;
```

**Current Duplication**: Both `pbr_test_demo.cpp` and `deferred_test_demo.cpp` have local functions:

```cpp
// In pbr_test_demo.cpp
Mat4 ComputeNormalMatrix(const Mat4& model) {
    return glm::transpose(glm::inverse(model));
}

// In deferred_test_demo.cpp (identical)
Mat4 ComputeNormalMatrix(const Mat4& model) {
    return glm::transpose(glm::inverse(model));
}
```

### TRS (Uniform Scale)

```cpp
/// Build model-to-world matrix from translation and uniform scale.
///
/// Computes M = Translation(t) * Rotation(identity) * Scale(s, s, s).
/// Rotation is identity (no rotation).
///
/// # Arguments
/// * translation - World position
/// * scale - Uniform scale factor applied to all axes
///
/// # Returns
/// A 4×4 transformation matrix ready for vertex shader MVP multiplication.
///
/// # Example
/// ```cpp
/// Mat4 model = Mat4::TRS({5, 0, -3}, 2.0f);  // At (5,0,-3), scaled 2× uniformly
/// ```
static Mat4 TRS(Vec3 translation, float uniformScale);
```

### TRS (Non-Uniform Scale)

```cpp
/// Build model-to-world matrix from translation and non-uniform scale.
///
/// Computes M = Translation(t) * Rotation(identity) * Scale(s.x, s.y, s.z).
/// Rotation is identity (no rotation).
///
/// # Arguments
/// * translation - World position
/// * scale - Per-axis scale factors (e.g., {2, 1, 0.5} for anisotropic scaling)
///
/// # Returns
/// A 4×4 transformation matrix. Note: normals need NormalMatrix() for correct
/// lighting when scale is non-uniform.
///
/// # Example
/// ```cpp
/// Mat4 model = Mat4::TRS({0, 0, 0}, {2, 1, 0.5f});  // Stretched 2× on X, squashed 0.5× on Z
/// ```
static Mat4 TRS(Vec3 translation, Vec3 scale);
```

### TRS (Full Rotation + Scale)

```cpp
/// Build model-to-world matrix from translation, rotation, and non-uniform scale.
///
/// Computes M = Translation(t) * Rotation(R) * Scale(s.x, s.y, s.z).
/// This is the full transformation used for most 3D objects.
///
/// # Arguments
/// * translation - World position
/// * rotation - Quaternion (e.g., Quat::FromAxisAngle(Vec3::Up(), angle_deg))
/// * scale - Per-axis scale factors
///
/// # Returns
/// A 4×4 transformation matrix.
///
/// # Example
/// ```cpp
/// Quat rot = Quat::FromAxisAngle(Vec3::Up(), 45.0f);
/// Mat4 model = Mat4::TRS({10, 2, -5}, rot, {1, 2, 1});
/// // Object at (10,2,-5), rotated 45° around Y, scaled 2× on Y axis
/// ```
static Mat4 TRS(Vec3 translation, Quat rotation, Vec3 scale);
```

**Current Duplication**: Demos manually assemble matrices:

```cpp
// In pbr_test_demo.cpp (multiple instances)
Mat4 model_matrix = ...;
model_matrix = glm::translate(model_matrix, position);
model_matrix = glm::rotate(model_matrix, glm::radians(angle), glm::vec3(0, 1, 0));
model_matrix = glm::scale(model_matrix, glm::vec3(scale, scale, scale));

// In deferred_test_demo.cpp (same pattern, repeated)
// ...
```

## Usage Examples

### Before (Manual Matrix Math)

```cpp
// In pbr_pass.cpp — typical pass setup
Mat4 model = Mat4::Identity();
model = glm::translate(model, object_pos);
model = glm::rotate(model, glm::radians(object_yaw), Vec3::Up());
model = glm::scale(model, Vec3(object_scale, object_scale, object_scale));

Mat4 normal_mat = glm::transpose(glm::inverse(model));  // Compute normals separately

// Upload to GPU
per_object_cb.model_matrix = model;
per_object_cb.normal_matrix = normal_mat;
```

### After (Convenience Methods)

```cpp
// In pbr_pass.cpp — same setup, fewer lines
Mat4 model = Mat4::TRS(object_pos,
                       Quat::FromAxisAngle(Vec3::Up(), object_yaw),
                       {object_scale, object_scale, object_scale});

Mat4 normal_mat = model.NormalMatrix();  // One-liner

// Upload to GPU
per_object_cb.model_matrix = model;
per_object_cb.normal_matrix = normal_mat;
```

## Rules

1. **NEVER compute `transpose(inverse(model))` manually in passes** — ALWAYS use `Mat4::NormalMatrix()`
2. **NEVER assemble TRS matrices via `translate() * rotate() * scale()` calls** — ALWAYS use `Mat4::TRS()`
3. **NEVER use `glm::` functions directly in engine code** — ALWAYS use `uz::Mat4` equivalents

## Engine Comparison

| Engine | Normal Matrix API | TRS Factory |
|--------|------------------|-------------|
| **UE5** | `FMatrix::Inverse().Transpose()` | Built into FTransform → FMatrix |
| **Godot** | `Basis::inverse().transposed()` | Transform3D → Basis + origin |
| **Unity** | Manual in shader via `_NormalMatrix` parameter | Transform component (.localToWorldMatrix) |
| **UzEngine** | `Mat4::NormalMatrix()` | `Mat4::TRS()` static methods |

## Phase Roadmap

| Phase | What Ships |
|-------|-----------|
| **1.5** | `NormalMatrix()`, `TRS(translation, scale)` variants |
| **2.0** | `TRS(translation, rotation, scale)` with Quat, integrate into render passes |

## Engine Comparison

| Engine | Transform Type | Composition | Dirty Tracking |
|--------|---------------|-------------|----------------|
| **UE5** | `FTransform` (Vec, Quat, Scale) | `GetRelativeTransform()` | `bWorldToComponentUpdated` flag |
| **Godot** | `Transform3D` (Basis + origin) | `Transform3D * Transform3D` | Internal `xform_change_list` |
| **Unity** | `Transform` component (position, rotation, localScale) | Automatic on parent change | Dirty flag + cached world matrix |
| **UzEngine** | `Transform` struct | `compose(parent)` | Future: dirty flag in SceneComponent |

UzEngine's `Transform` is a plain data struct, not a component. The dirty flag system will live in `SceneComponent` (the entity/component layer), not in the math layer. This keeps the math library free of ECS dependencies.

## Additional Math Utilities

### Color

```cpp
struct Color {
    float r, g, b, a;
    static Color White();
    static Color Black();
    static Color Red();
    // ... etc.
    Vec4 ToVec4() const;
};
```

### Geometry (Bounds, Planes, Rays)

Defined in `runtime/engine/math/geometry.h`:
- `AABB` — axis-aligned bounding box (min/max Vec3)
- `Sphere` — center + radius
- `Plane` — normal + distance
- `Ray` — origin + direction
- Intersection tests: ray-AABB, ray-sphere, frustum-AABB
