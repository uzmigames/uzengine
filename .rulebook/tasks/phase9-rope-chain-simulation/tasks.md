## 1. CRope ECS Component

- [ ] 1.1 Create `src/physics/components/crope.h` declaring `CRope` struct with fields: `segment_count` (uint32_t, min 2, max 64), `total_length` (float), `radius` (float), `stiffness` (float), `damping` (float), `anchor_a` (Vec3 or entity attachment point), `anchor_b` (Vec3 or entity attachment point, optional), `free_end` (bool)
- [ ] 1.2 Register `CRope` with the ECS component registry for scene serialization

## 2. Jolt Constraint Chain Backend

- [ ] 2.1 In `src/physics/physics_system.cpp`, on `CRope` component add, create `segment_count` `JPH::Body` sphere bodies with radius = `CRope::radius`, placed along the catenary initial curve
- [ ] 2.2 Link adjacent segment bodies with `JPH::DistanceConstraint` pairs using `min_distance = segment_length * 0.95` and `max_distance = segment_length * 1.05` for slight elasticity
- [ ] 2.3 Attach the first segment to `anchor_a` with a `JPH::DistanceConstraint` at zero distance (pin joint); if `anchor_a` is an entity, attach to that entity's body
- [ ] 2.4 If `free_end == false`, attach the last segment to `anchor_b` with a zero-distance constraint; if `free_end == true`, leave the last segment unconstrained

## 3. Catenary Initial Shape Solver

- [ ] 3.1 Implement `solve_catenary(Vec3 a, Vec3 b, float total_length, int segments)` in `src/physics/rope_solver.cpp` returning a `std::vector<Vec3>` of segment positions forming a catenary curve
- [ ] 3.2 Use iterative Newton-Raphson to find the catenary parameter `c` such that the arc length matches `total_length`; fall back to linear distribution if convergence fails after 100 iterations
- [ ] 3.3 Call `solve_catenary()` when the `CRope` component is added and use the result as the initial body positions

## 4. CCD for Fast-Moving Ropes

- [ ] 4.1 Enable Continuous Collision Detection on each rope segment body via `JPH::BodyCreationSettings::mMotionQuality = JPH::EMotionQuality::LinearCast` for `CRope` bodies with `stiffness > 0.5` or `free_end == true`

## 5. Per-Frame Pose Readback and Tube Mesh

- [ ] 5.1 Create `src/renderer/rope_renderer.h` and `src/renderer/rope_renderer.cpp` implementing `RopeRenderer::update_mesh(entity_id, std::span<Vec3> segment_positions, float radius)`
- [ ] 5.2 Each frame, read back `JPH::Body::GetPosition()` for all segment bodies of each rope into a `std::vector<Vec3>` sorted by segment index
- [ ] 5.3 Generate a procedural tube mesh: for each pair of adjacent segment positions, extrude a circle of `8` vertices and triangulate the tube wall; UV scrolls along the tube length for animated chain/rope textures
- [ ] 5.4 Upload the tube mesh vertex buffer to the GPU each frame as a dynamic DX12 buffer

## 6. Attachment API

- [ ] 6.1 Add `ctx.physics:attach_rope(entityA, anchorA, entityB, anchorB, length, options)` Lua binding in `src/scripting/lua_physics_bindings.cpp`; returns the rope entity ID
- [ ] 6.2 Add `ctx.physics:cut_rope(rope_entity)` Lua binding that removes the `JPH::DistanceConstraint` connecting the two halves at the midpoint; converts both halves into free-hanging ropes

## 7. Editor Gizmo

- [ ] 7.1 In `src/editor/gizmos.cpp`, when a `CRope` entity is selected, draw the segment chain as a series of green spheres connected by white lines in the editor wireframe pass
- [ ] 7.2 Draw drag handles at each anchor point (anchor_a, anchor_b) as yellow spheres

## 8. Testing

- [ ] 8.1 Create `tests/physics/test_rope_simulation.cpp` testing `solve_catenary()` with anchors at the same height 2 units apart and `total_length = 3`: verify the midpoint segment Y is below the anchor Y (sag exists)
- [ ] 8.2 Test `solve_catenary()` with `total_length == distance(a, b)`: verify all segments lie on the straight line between a and b (no sag, taut rope)
- [ ] 8.3 Test Jolt constraint chain creation: adding `CRope` with `segment_count=5` creates 5 Jolt bodies; verify via `PhysicsSystem::get_body_count()`
- [ ] 8.4 Test `free_end=false` pins both ends: simulate 100 physics steps; verify anchor_a segment position is within 0.01m of the anchor point
- [ ] 8.5 Test `free_end=true`: simulate 100 physics steps; verify the last segment moves under gravity (Y decreases over time)
- [ ] 8.6 Test tube mesh generation: `update_mesh()` with 3 segment positions produces a mesh with exactly `2 * 8 + 8` vertices (two rings plus caps)
- [ ] 8.7 Test `cut_rope()`: after cutting, the two halves each have an unconstrained end; verify the two free ends diverge under gravity over 20 physics steps
- [ ] 8.8 Test Lua `ctx.physics:attach_rope()` returns a non-zero entity ID; the entity has a `CRope` component
- [ ] 8.9 Achieve >= 95% line and branch coverage across `rope_solver.cpp`, `rope_renderer.cpp`, and Lua rope bindings

## 9. Documentation

- [ ] 9.1 Update `docs/specs/physics-system.md` documenting `CRope` fields, catenary solver, constraint chain, CCD behavior, and `cut_rope()` semantics
- [ ] 9.2 Update `docs/specs/lua-scripting.md` documenting `ctx.physics:attach_rope()` and `ctx.physics:cut_rope()`
- [ ] 9.3 Update `docs/specs/editor-gizmos.md` documenting the rope segment chain wireframe gizmo and anchor drag handles
