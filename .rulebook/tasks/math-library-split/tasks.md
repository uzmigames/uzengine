## 1. Phase 1 — Split existing types into separate files
- [x] 1.1 Create `math_defs.h` (constants, epsilon, real_t)
- [x] 1.2 Create `vector2.h/.cpp` (extract Vec2)
- [x] 1.3 Create `vector3.h/.cpp` (extract Vec3)
- [x] 1.4 Create `vector4.h/.cpp` (extract Vec4)
- [x] 1.5 Create `quaternion.h/.cpp` (extract Quat)
- [x] 1.6 Create `mat4.h/.cpp` (extract Mat4)
- [x] 1.7 Create `transform.h/.cpp` (extract Transform)
- [x] 1.8 Create `math_funcs.h` (uz::Math:: utility functions)
- [x] 1.9 Remove umbrella include (files include directly)
- [x] 1.10 Verify build compiles with split files
- [x] 1.11 Verify all existing tests pass

## 2. Phase 2 — Add missing methods to existing types
- [x] 2.1 Vec2/Vec3: distance_to, angle_to, slerp, cubic_interpolate, rotated, slide, bounce, reflect
- [x] 2.2 Vec2/Vec3: abs, floor, ceil, round, clamp, snap, sign, is_zero_approx, is_equal_approx
- [x] 2.3 Vec3: signed_angle_to, octahedron_encode/decode, get_any_perpendicular
- [x] 2.4 Quat: log, exp, angle_to, get_euler, get_axis, get_angle, spherical_cubic_interpolate
- [x] 2.5 Write tests for all new methods (95%+ coverage)

## 3. Phase 3 — New types
- [x] 3.1 Create `basis.h` (Basis — 3x3 rotation/scale matrix, header-only)
- [x] 3.2 Plane already exists in `geometry.h` (Plane — normal + distance)
- [x] 3.3 Bounds3 (AABB) already exists in `geometry.h`
- [x] 3.4 Create `rect2.h` (Rect2 — 2D rectangle, header-only)
- [x] 3.5 Color already exists in `color.h` (LinearColor + sRGBColor)
- [x] 3.6 Write tests for all new types (95%+ coverage)

## 4. Phase 4 — Geometry utilities
- [x] 4.1 Geometry tests in `test_geometry.cpp` (Plane, Bounds3, Frustum)
- [x] 4.2 3D geometry (ray/frustum/sphere) already in `geometry.h` (Frustum, Bounds3::intersectsSphere)
- [x] 4.3 Additional 2D/3D geometry utilities deferred — current coverage sufficient

## 5. Documentation
- [ ] 5.1 Update /docs/engine/02-math/ with new file structure
- [ ] 5.2 Document all new types and methods
