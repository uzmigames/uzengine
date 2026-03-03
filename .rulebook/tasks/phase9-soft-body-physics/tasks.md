## 1. Prerequisites

- [ ] 1.1 Confirm `phase4-wind-system-unified` task is complete: `WindSystem::get_wind_at()` is available for per-frame wind force queries
- [ ] 1.2 Confirm Jolt Physics version in `vcpkg.json` is >= v4.0 (first version with `JPH::SoftBodyCreationSettings`)

## 2. CSoftBody ECS Component

- [ ] 2.1 Create `src/physics/components/csoftbody.h` declaring `CSoftBody` struct with fields: `mesh_asset` (std::string), `stiffness` (float), `damping` (float), `pressure` (float, for inflatables, 0=no pressure), `iteration_count` (uint32_t), `gravity_scale` (float, default 1.0), `self_collision` (bool), `wind_factor` (float)
- [ ] 2.2 Register `CSoftBody` with the ECS component registry for scene serialization

## 3. Jolt Soft Body Registration

- [ ] 3.1 On `CSoftBody` component add, load the rest-pose vertex mesh from `mesh_asset` using the existing mesh loader
- [ ] 3.2 Build `JPH::SoftBodyCreationSettings` from the mesh: populate vertex positions, edge constraints (per mesh edge), and volume constraints (per mesh tetrahedron or surface triangle with pressure)
- [ ] 3.3 Set `JPH::SoftBodyCreationSettings::mStiffness`, `mLinearDamping`, `mPressure`, `mNumIterations`, and `mGravityFactor` from the `CSoftBody` component fields
- [ ] 3.4 Enable self-collision via `mUpdatePosition = true` and `mMakeRotationIdentity = false` on `SoftBodyCreationSettings` when `CSoftBody::self_collision == true`
- [ ] 3.5 Register the soft body with `JPH::PhysicsSystem::AddBody()` and store the body ID in the `CSoftBody` component

## 4. Per-Frame Pose Readback

- [ ] 4.1 Each physics step, call `JPH::SoftBodyMotionProperties::GetVertices()` to read back the current simulated vertex positions from Jolt
- [ ] 4.2 Store the vertex positions in `CSoftBody::simulated_vertices` (a `std::vector<Vec3>`) for use by the renderer

## 5. Soft Body Renderer

- [ ] 5.1 Create `src/renderer/soft_body_renderer.h` and `src/renderer/soft_body_renderer.cpp` implementing `SoftBodyRenderer::update_mesh(entity_id, std::span<Vec3> vertices)`
- [ ] 5.2 Each frame, upload `CSoftBody::simulated_vertices` into a dynamic DX12 vertex buffer, replacing the rest-pose positions
- [ ] 5.3 Preserve the rest-pose vertex normals and UV coordinates from the original mesh asset; only positions are updated each frame

## 6. Attachment Constraints to Rigid Bodies

- [ ] 6.1 Implement `SoftBodyAttachment` struct with fields: `soft_body_entity` (entity_id), `vertex_index` (uint32_t), `rigid_body_entity` (entity_id or world anchor), `local_offset` (Vec3)
- [ ] 6.2 Create `JPH::DistanceConstraint` between the specified soft body vertex body and the rigid body (or a Jolt fixed body for world anchors) in `PhysicsSystem::add_soft_body_attachment()`
- [ ] 6.3 Apply cape-to-shoulder workflow: two attachment constraints (one per shoulder bone) keeping the top edge of a cape mesh fixed to the character's shoulder rigid bodies

## 7. Wind Force Integration

- [ ] 7.1 Each physics tick, query `WindSystem::get_wind_at(soft_body_world_center)` for each active soft body
- [ ] 7.2 Apply the wind force as a per-vertex body force scaled by `CSoftBody::wind_factor` using `JPH::SoftBodyMotionProperties::ApplyForce(vertex_index, wind_force)`

## 8. Lua API

- [ ] 8.1 Add `ctx.physics:apply_soft_force(entity, vertex_index, {fx, fy, fz})` Lua binding in `src/scripting/lua_physics_bindings.cpp` calling `JPH::SoftBodyMotionProperties::ApplyForce()`
- [ ] 8.2 Add `ctx.physics:reset_soft_body(entity)` Lua binding that resets all vertex positions to rest-pose positions and zeroes all velocities

## 9. Asset Pipeline

- [ ] 9.1 Extend the `.uzmesh` format to optionally store a `SoftBodyRestPose` chunk: vertex positions for the rest-pose mesh alongside the render mesh
- [ ] 9.2 In the mesh importer, when `[asset.mesh] soft_body = true` is set in the `.uzasset` sidecar, export the mesh vertices as the rest-pose chunk
- [ ] 9.3 In the runtime mesh loader, read the `SoftBodyRestPose` chunk if present and pass the vertex data to `CSoftBody` initialization

## 10. Testing

- [ ] 10.1 Create `tests/physics/test_soft_body.cpp` testing that adding `CSoftBody` to an entity registers a Jolt soft body (body count increases by 1)
- [ ] 10.2 Test `JPH::SoftBodyCreationSettings` fields match `CSoftBody` component: stiffness, damping, pressure, iteration_count
- [ ] 10.3 Test self_collision flag: verify `SoftBodyCreationSettings` reflects the correct flag when `self_collision=true` vs `false`
- [ ] 10.4 Test pose readback: after 10 physics steps, verify `CSoftBody::simulated_vertices` differ from rest-pose positions (gravity has moved them)
- [ ] 10.5 Test `reset_soft_body()`: after 10 physics steps, calling reset restores all vertex positions to rest-pose within 0.001m tolerance
- [ ] 10.6 Test `apply_soft_force()` on vertex 0: verify vertex 0 position changes more than vertex 1 after one physics step when an upward force is applied only to vertex 0
- [ ] 10.7 Test wind integration: with `WindSystem` set to `base_speed=10.0`, verify soft body vertices on the windward side displace more than lee-side vertices after 20 steps
- [ ] 10.8 Test rigid body attachment: a soft body with vertex 0 pinned to a rigid body at position P keeps vertex 0 within 0.01m of P during simulation
- [ ] 10.9 Test `SoftBodyRestPose` asset pipeline round-trip: import a mesh with `soft_body=true`, serialize to `.uzmesh`, reload; verify vertex count matches
- [ ] 10.10 Achieve >= 95% line and branch coverage across `soft_body_renderer.cpp`, Jolt registration code, and Lua soft body bindings

## 11. Documentation

- [ ] 11.1 Update `docs/specs/physics-system.md` documenting `CSoftBody` fields, Jolt v4 requirement, attachment constraint API, and wind integration
- [ ] 11.2 Update `docs/specs/renderer.md` documenting the soft body vertex buffer update pipeline and rest-pose UV preservation
- [ ] 11.3 Update `docs/specs/lua-scripting.md` documenting `ctx.physics:apply_soft_force()` and `ctx.physics:reset_soft_body()`
