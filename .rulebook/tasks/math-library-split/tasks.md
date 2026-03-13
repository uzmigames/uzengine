## 1. Phase 1 — Split existing types into separate files
- [ ] 1.1 Create `math_defs.h` (constants, epsilon, real_t)
- [ ] 1.2 Create `vector2.h/.cpp` (extract Vec2)
- [ ] 1.3 Create `vector3.h/.cpp` (extract Vec3)
- [ ] 1.4 Create `vector4.h/.cpp` (extract Vec4)
- [ ] 1.5 Create `quaternion.h/.cpp` (extract Quat)
- [ ] 1.6 Create `mat4.h/.cpp` (extract Mat4)
- [ ] 1.7 Create `transform.h/.cpp` (extract Transform)
- [ ] 1.8 Create `math_funcs.h` (uz::Math:: utility functions)
- [ ] 1.9 Convert `engine_math.h` to umbrella include
- [ ] 1.10 Verify build compiles with split files
- [ ] 1.11 Verify all existing tests pass

## 2. Phase 2 — Add missing methods to existing types
- [ ] 2.1 Vec2/Vec3: distance_to, angle_to, slerp, cubic_interpolate, rotated, slide, bounce, reflect
- [ ] 2.2 Vec2/Vec3: abs, floor, ceil, round, clamp, snap, sign, is_zero_approx, is_equal_approx
- [ ] 2.3 Vec3: signed_angle_to, octahedron_encode/decode, get_any_perpendicular
- [ ] 2.4 Quat: log, exp, angle_to, get_euler, get_axis, get_angle, spherical_cubic_interpolate
- [ ] 2.5 Write tests for all new methods (95%+ coverage)

## 3. Phase 3 — New types
- [ ] 3.1 Create `basis.h/.cpp` (Basis — 3x3 rotation/scale matrix)
- [ ] 3.2 Create `plane.h/.cpp` (Plane — normal + distance)
- [ ] 3.3 Create `aabb.h/.cpp` (AABB — axis-aligned bounding box)
- [ ] 3.4 Create `rect2.h/.cpp` (Rect2 — 2D rectangle)
- [ ] 3.5 Create `color.h/.cpp` (Color — RGBA, HSV, sRGB/linear)
- [ ] 3.6 Write tests for all new types (95%+ coverage)

## 4. Phase 4 — Geometry utilities
- [ ] 4.1 Create `geometry2d.h/.cpp` (intersections, triangulation, convex hull)
- [ ] 4.2 Create `geometry3d.h/.cpp` (ray/segment/triangle/sphere intersections)
- [ ] 4.3 Write tests for geometry utilities (95%+ coverage)

## 5. Documentation
- [ ] 5.1 Update /docs/engine/02-math/ with new file structure
- [ ] 5.2 Document all new types and methods
