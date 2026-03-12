# Math — Transform & Hierarchy

> **Status**: Done (Milestone 1.1.0a)
> **Source**: `runtime/core/math/engine_math.h` (Transform struct)
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

Defined in `runtime/core/math/geometry.h`:
- `AABB` — axis-aligned bounding box (min/max Vec3)
- `Sphere` — center + radius
- `Plane` — normal + distance
- `Ray` — origin + direction
- Intersection tests: ray-AABB, ray-sphere, frustum-AABB
