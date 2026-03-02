## 1. Prerequisites

- [ ] 1.1 Confirm `phase3-physics-system` is complete — Jolt Physics initialized, body creation works
- [ ] 1.2 Confirm `phase3-skeletal-mesh` is complete — bone transform outputs available per-frame
- [ ] 1.3 Confirm `phase1-render-graph` is complete — compute pass support
- [ ] 1.4 Read Jolt Physics `SoftBody` documentation — `JPH::SoftBodyCreationSettings`, `SoftBodySharedSettings`, vertex constraints, face constraints

## 2. CCloth Component

- [ ] 2.1 Add `CCloth` to `ecs/components.h`
- [ ] 2.2 Fields:
  - `mesh_path: string` — source mesh (must be plane or grid topology)
  - `attachment_bones: vector<pair<int, int>>` — (vertex_index, bone_index) pairs for pinned vertices
  - `wind_zone_entity: entt::entity` — optional `CWindZone` affecting this cloth
  - `lod_disable_distance: float` — skip simulation beyond this distance
  - `compliance: float` — stretch stiffness (0=rigid, 0.001=stretchy)
  - `damping: float` — velocity damping per step
  - `thickness: float` — collision proxy thickness (meters)
  - `jolt_body_id: JPH::BodyID` (runtime)
- [ ] 2.3 Update `.uzscene` parser

## 3. CWindZone Component

- [ ] 3.1 Add `CWindZone` to `ecs/components.h`
- [ ] 3.2 Fields: `direction: Vec3`, `strength: float`, `turbulence: float` (0–1), `radius: float` (0=global)
- [ ] 3.3 Update `.uzscene` parser

## 4. ClothSystem ECS System

- [ ] 4.1 Create `src/engine/physics/cloth_system.h/cpp` — `ClothSystem : IEcsSystem`
- [ ] 4.2 Register in `PhysicsUpdate` group
- [ ] 4.3 Implement `ClothSystem::on_register` — install `on_construct<CCloth>` observer
- [ ] 4.4 On `CCloth` construct:
  - Load mesh, extract vertex positions and faces
  - Build `JPH::SoftBodySharedSettings` — one vertex per mesh vertex, one face per triangle
  - Set `pinned_vertices` from `attachment_bones` (mapped to mesh vertex index; pinned = infinite mass)
  - Set constraints: edge constraints (stretch), bend constraints (wrinkle resistance)
  - Create `JPH::SoftBodyCreationSettings`, create body in Jolt
  - Store `jolt_body_id`
- [ ] 4.5 Implement `ClothSystem::tick(registry, dt)`:
  - Update pinned vertex positions from `CSkeletalMesh.bone_transforms[bone_index]`
  - Apply wind force from nearest `CWindZone`: `force = wind.direction * wind.strength + turbulence_noise * wind.turbulence`
  - LOD check: if camera distance > `lod_disable_distance` → deactivate Jolt body (freeze)
- [ ] 4.6 On `CCloth` destroy: remove Jolt soft body from physics

## 5. Cloth Skin Compute Pass

- [ ] 5.1 Create `src/engine/renderer/passes/cloth_skin_pass.h/cpp` — compute RgPass
- [ ] 5.2 Input: Jolt soft body vertex positions (copied to `RWStructuredBuffer<float3>` each frame), skinned mesh vertex buffer
- [ ] 5.3 Output: blended vertex positions in mesh vertex buffer
- [ ] 5.4 Blend: `final_pos = lerp(skinned_pos, cloth_sim_pos, cloth_blend_weight)` — weight from bone attachment (attached vertices = 1.0, free vertices = 0.0, blended at boundary)
- [ ] 5.5 Run before main geometry pass in render graph

## 6. Collision Proxy Setup

- [ ] 6.1 Configure Jolt soft body collision: enable collision against `CRigidBody` entities
- [ ] 6.2 Configure collision layers: cloth layer vs. world geometry layer, character layer
- [ ] 6.3 Use `thickness` as contact offset to prevent cloth penetrating through thin surfaces
- [ ] 6.4 Self-collision: disabled by default (expensive); enable via `CCloth.self_collision: bool`

## 7. Wind Zone Integration

- [ ] 7.1 Implement `ClothSystem::compute_wind_at(position, CWindZone) -> Vec3`:
  - If `radius > 0`: distance check; zero wind outside radius
  - Add turbulence: `turbulence_vec = noise3d(position + time * 0.5) * wind.turbulence * wind.strength`
  - Return `wind.direction * wind.strength + turbulence_vec`
- [ ] 7.2 Apply wind as Jolt soft body point force on all non-pinned vertices each step

## 8. LOD System

- [ ] 8.1 Measure camera distance each frame for each `CCloth` entity
- [ ] 8.2 If distance > `lod_disable_distance`: deactivate Jolt body (vertices frozen at last positions)
- [ ] 8.3 If distance returns to < `lod_disable_distance * 0.8` (hysteresis): reactivate body

## 9. Testing

- [ ] 9.1 Unit test: `CCloth` construct → Jolt soft body created with correct vertex count
- [ ] 9.2 Unit test: pinned vertices (attachment_bones) do not move under gravity
- [ ] 9.3 Unit test: free vertices drape under gravity → Y positions decrease each step
- [ ] 9.4 Unit test: `CWindZone` with `radius = 10` → wind force zero at distance 15
- [ ] 9.5 Unit test: LOD — deactivate body at far distance, no position update
- [ ] 9.6 Integration test: hanging cloth under gravity settles into correct drape shape within 2 seconds
- [ ] 9.7 Integration test: rigid body sphere pushed through cloth → cloth vertices deflect around sphere
- [ ] 9.8 Integration test: `CWindZone` causes cloth to oscillate laterally

## 10. Documentation

- [ ] 10.1 Update `docs/specs/physics-system.md` — "Cloth Simulation" section with `CCloth`, `CWindZone` fields
- [ ] 10.2 Create `docs/specs/cloth-simulation.md` — full spec: setup workflow, constraint types, wind zones, LOD
- [ ] 10.3 Update `docs/guides/physics-guide.md` — "Setting Up Cloth" section with cape example
