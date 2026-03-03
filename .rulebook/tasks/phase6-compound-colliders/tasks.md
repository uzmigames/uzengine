## 1. ColliderShape and CCompoundCollider Data Structures
- [ ] 1.1 Define `ColliderShapeType` enum in `src/scene/components.h`: `Box`, `Sphere`, `Capsule`, `ConvexHull`
- [ ] 1.2 Define `ColliderShape` struct in `src/scene/components.h` with fields: `type` (ColliderShapeType), `half_extents` (float3, box), `radius` (float, sphere/capsule), `half_height` (float, capsule), `convex_hull_asset` (AssetHandle, convex hull only), `local_position` (float3), `local_rotation` (quaternion)
- [ ] 1.3 Add `CCompoundCollider` ECS component to `src/scene/components.h` with `std::vector<ColliderShape> shapes` and `is_dirty` (bool) flag set when shapes are modified
- [ ] 1.4 Register `CCompoundCollider` in the ECS component registry (`src/scene/ComponentRegistry.cpp`) with reflection metadata for inspector serialization

## 2. Jolt Physics Backend
- [ ] 2.1 In `src/physics/collider_system.cpp`, implement `CompoundColliderSystem::build_jolt_body(entity)` that iterates `CCompoundCollider::shapes`, constructs a `JPH::StaticCompoundShapeSettings`, adds each sub-shape with its local offset `JPH::Vec3` and `JPH::Quat`, then creates a `JPH::Body` from the compiled settings
- [ ] 2.2 Implement `CompoundColliderSystem::rebuild_body(entity)` that destroys the existing `JPH::Body` while preserving its current linear and angular velocity, then calls `build_jolt_body()` to re-create it; velocity is restored after re-creation
- [ ] 2.3 In `ColliderSystem::update()`, check `CCompoundCollider::is_dirty` each frame; if dirty, call `rebuild_body()` and clear the flag
- [ ] 2.4 Support all four `ColliderShapeType` values in the Jolt body builder: Box uses `JPH::BoxShapeSettings(half_extents)`, Sphere uses `JPH::SphereShapeSettings(radius)`, Capsule uses `JPH::CapsuleShapeSettings(half_height, radius)`, ConvexHull loads the hull asset and uses `JPH::ConvexHullShapeSettings`

## 3. Runtime Sub-Shape Add/Remove API
- [ ] 3.1 Add `CCompoundCollider::add_shape(const ColliderShape& shape)` that appends a shape to the vector and sets `is_dirty = true`
- [ ] 3.2 Add `CCompoundCollider::remove_shape(int index)` that bounds-checks the index, removes the shape, and sets `is_dirty = true`
- [ ] 3.3 Add `CCompoundCollider::clear_shapes()` that empties the vector and sets `is_dirty = true`

## 4. glTF Import Integration
- [ ] 4.1 In `src/assets/gltf_importer.cpp`, parse the `_colliders` array in glTF node `extras` during scene import
- [ ] 4.2 For each entry in `_colliders`, read `type` (string: "box"/"sphere"/"capsule"/"convex"), size parameters (`half_extents`, `radius`, `half_height`), and optional `offset` (position + rotation) fields
- [ ] 4.3 Populate a `CCompoundCollider` component on the corresponding entity with one `ColliderShape` per `_colliders` entry; if a node has only one entry and the entity already has a `CCollider`, skip to avoid duplication and log a warning
- [ ] 4.4 If `type` is "convex" and a mesh name is provided, register the mesh as a convex hull asset and store its handle in `ColliderShape::convex_hull_asset`

## 5. Serialization
- [ ] 5.1 Implement `CCompoundCollider` serialization in `src/scene/SceneSerializer.cpp` writing a `[[compound_collider.shape]]` INI array block per sub-shape with all fields including local offset transform
- [ ] 5.2 Implement deserialization: parse `[[compound_collider.shape]]` blocks and reconstruct the `shapes` vector, setting `is_dirty = true` so the physics body is built on first update

## 6. Editor Inspector Panel
- [ ] 6.1 In `src/editor/inspector_physics.cpp`, render the `CCompoundCollider` inspector section listing all sub-shapes with their type icon, local position, and a remove (X) button per shape
- [ ] 6.2 Add a per-shape local offset editor: position (float3 drag) and rotation (Euler angles drag), calling `rebuild_body()` on change
- [ ] 6.3 Add an "Add Shape" button with a dropdown for Box/Sphere/Capsule/ConvexHull that appends a default-sized shape via `add_shape()`

## 7. Editor Gizmo
- [ ] 7.1 In the editor viewport gizmo system, draw each sub-shape outline at its local offset position using the existing debug draw primitives (wireframe box, sphere, capsule)
- [ ] 7.2 Highlight the currently selected sub-shape in yellow; non-selected shapes are drawn in the standard collision green

## 8. Testing
- [ ] 8.1 Create `tests/physics/test_compound_colliders.cpp` with Google Test cases: `CCompoundCollider` with 3 shapes builds a Jolt body with 3 sub-shapes, `rebuild_body()` preserves linear velocity within 1% tolerance, removing a shape via `remove_shape()` and updating reduces sub-shape count in Jolt body
- [ ] 8.2 Add tests for each `ColliderShapeType`: Box, Sphere, Capsule bodies build without assertion; ConvexHull with valid asset handle builds correctly; ConvexHull with null handle logs an error and skips that shape
- [ ] 8.3 Add serialization round-trip tests for `CCompoundCollider`: serialize to INI string, deserialize, assert identical shape count, types, and local offsets
- [ ] 8.4 Add glTF import tests: a node with `_colliders` array of two entries produces a `CCompoundCollider` with two shapes; a node with no `_colliders` produces no `CCompoundCollider`
- [ ] 8.5 Add an add/remove API test: add 5 shapes, remove index 2, assert remaining shapes are correct indices; `add_shape()` sets `is_dirty`; `remove_shape(-1)` does not crash
- [ ] 8.6 Verify line coverage >= 95% and branch coverage >= 95% across `collider_system.cpp` new code paths and `gltf_importer.cpp` `_colliders` parsing

## 9. Documentation
- [ ] 9.1 Update `docs/specs/physics-system.md` with `CCompoundCollider` component field reference, sub-shape add/remove API, the `rebuild_body()` behavior, and Jolt shape type mapping table
- [ ] 9.2 Update `docs/specs/asset-pipeline.md` with the glTF `_colliders` node extras format and all supported shape type strings and their required parameters
- [ ] 9.3 Update `docs/specs/editor-gizmos.md` with compound collider gizmo rendering behavior (per-shape wireframes, selected shape highlight color)
