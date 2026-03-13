# Proposal: math-library-split

## Why
The monolithic `engine_math.h/cpp` (927+408 lines) violates single-responsibility and makes it hard to include only what you need. Godot's math library uses one file per type — we should follow the same pattern while keeping DirectXMath as our SIMD backend. Additionally, Godot has many utility methods and types (Plane, AABB, Rect2, Color, Basis, Geometry2D/3D, math_funcs) that we're missing.

## What Changes

### Phase 1: Split existing types into separate files
- `math_defs.h` — constants, epsilon, typedefs
- `math_funcs.h` — `uz::Math::` namespace utility functions
- `vector2.h/.cpp` — `Vec2`
- `vector3.h/.cpp` — `Vec3` (SIMD)
- `vector4.h/.cpp` — `Vec4`
- `quaternion.h/.cpp` — `Quat`
- `mat4.h/.cpp` — `Mat4`
- `transform.h/.cpp` — `Transform`
- `engine_math.h` becomes umbrella include (backward compat)

### Phase 2: Add missing methods to existing types
- Vec2/Vec3: distance_to, angle_to, slerp, cubic_interpolate, rotated, slide, bounce, reflect, abs, floor, ceil, round, clamp, snap, sign, is_zero_approx, is_equal_approx
- Vec3: signed_angle_to, octahedron_encode/decode, get_any_perpendicular
- Quat: log, exp, angle_to, get_euler, get_axis, get_angle, spherical_cubic_interpolate

### Phase 3: New types
- `basis.h/.cpp` — `Basis` (3x3 rotation/scale matrix)
- `plane.h/.cpp` — `Plane`
- `aabb.h/.cpp` — `AABB`
- `rect2.h/.cpp` — `Rect2`
- `color.h/.cpp` — `Color` (HSV, sRGB/linear, blend)

### Phase 4: Geometry utilities
- `geometry2d.h/.cpp` — `Geometry2D::` (intersections, triangulation, convex hull)
- `geometry3d.h/.cpp` — `Geometry3D::` (ray/segment/triangle/sphere intersections)

## Impact
- Affected specs: math library
- Affected code: all 22 files that `#include "engine_math.h"` — no API change, umbrella header preserves compat
- Breaking change: NO (umbrella header maintains backward compatibility)
- User benefit: modular includes, richer math API, better compile times
