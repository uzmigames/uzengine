## 1. Prerequisites

- [ ] 1.1 Confirm `phase3-physics-system` is complete — Jolt `CRigidBody`, `CCollider`, `EventBus` collision events
- [ ] 1.2 Confirm `phase3-world-streaming` is complete — streaming chunk reload behavior understood
- [ ] 1.3 Read `docs/specs/physics-system.md` — understand Jolt body creation API, layer system
- [ ] 1.4 Survey Voronoi fracture algorithms (CGAL or hand-rolled) for offline tool

## 2. uzfrac Offline Tool

- [ ] 2.1 Create `tools/uzfrac/main.cpp` — standalone exe: `uzfrac input.glb --count 32 --seed 42 --out output.uzfrac`
- [ ] 2.2 Load input mesh using glTF importer (reuse `phase2-gltf-import` code)
- [ ] 2.3 Implement Voronoi fracture: generate N random seed points inside mesh volume, compute Voronoi cells
- [ ] 2.4 Clip each Voronoi cell against mesh using BSP tree → fragment mesh
- [ ] 2.5 Compute per-fragment convex hull for Jolt collision shape
- [ ] 2.6 Compute inter-fragment adjacency graph (which fragments touch which neighbors)
- [ ] 2.7 Write `.uzfrac` binary file format:
  - Header: fragment count, AABB, total vertex count
  - Per fragment: vertex buffer, index buffer, convex hull points, neighbor_ids[], local_centroid
- [ ] 2.8 Validate output: sum of fragment volumes ≈ original mesh volume (within 5%)

## 3. .uzfrac Runtime Loader

- [ ] 3.1 Create `src/engine/physics/uzfrac_loader.h/cpp`
- [ ] 3.2 Implement `UzFracLoader::load(path) -> FracMesh` — parse binary format
- [ ] 3.3 Implement `FracMesh::create_jolt_shapes()` — create `JPH::ConvexHullShapeSettings` per fragment (deferred to fracture time)
- [ ] 3.4 Create `FracMeshRegistry` — cache loaded `FracMesh` by path

## 4. CDestructible Component

- [ ] 4.1 Add `CDestructible` to `ecs/components.h`
- [ ] 4.2 Fields: `frac_path: string`, `health: float`, `max_health: float`, `damage_threshold: float` (min damage to apply), `debris_lifetime_s: float`, `state: DestructibleState` (Intact/Broken)
- [ ] 4.3 Update `.uzscene` parser

## 5. DestructibleSystem

- [ ] 5.1 Create `src/engine/physics/destructible_system.h/cpp` — `DestructibleSystem : IEcsSystem`
- [ ] 5.2 Register in `LateUpdate` group
- [ ] 5.3 Implement `DestructibleSystem::apply_damage(entity, damage, hit_point, impulse)`:
  - If `damage < damage_threshold`: ignore
  - `health -= damage`
  - If `health <= 0`: call `fracture(entity, impulse)`
- [ ] 5.4 Implement `DestructibleSystem::fracture(entity, impulse)`:
  - Load `FracMesh` from registry
  - For each fragment: request `DebrisPool::spawn(fragment, world_transform, impulse)` → `entt::entity`
  - Set fragment launch velocity: base impulse + random scatter (range from `.uzproj`)
  - Destroy original `CDestructible` entity (hide mesh renderer)
  - Post `DestructibleBrokenEvent { entity, hit_point }`
  - Start `debris_lifetime_s` timer → schedule `DebrisPool::reclaim` after expiry

## 6. DebrisPool

- [ ] 6.1 Create `src/engine/physics/debris_pool.h/cpp`
- [ ] 6.2 Pre-allocate 256 Jolt bodies at init (via `JPH::BodyInterface::CreateBody`)
- [ ] 6.3 Implement `DebrisPool::spawn(FracFragment, transform, impulse) -> entt::entity`:
  - Claim a pool slot (LRU steal if pool full)
  - Set body shape to fragment convex hull
  - Set body position/velocity
  - Activate in Jolt
  - Return ECS entity with `CRigidBody`, `CMeshRenderer` (fragment mesh)
- [ ] 6.4 Implement `DebrisPool::reclaim(entity)` — deactivate Jolt body, return slot to freelist
- [ ] 6.5 Implement `DebrisPool::reclaim_all()` — called on scene unload

## 7. EventBus Integration

- [ ] 7.1 Define `DestructibleBrokenEvent`: `entity: entt::entity`, `hit_point: Vec3`, `fragment_count: int`
- [ ] 7.2 Post to `EventBus<DestructibleBrokenEvent>` from `fracture()`
- [ ] 7.3 Lua: `ctx.events:subscribe("DestructibleBroken", fn)` → callback with `{entity, hit_point}`

## 8. Collision Integration

- [ ] 8.1 Subscribe to `CollisionEvent` for entities with `CDestructible`
- [ ] 8.2 In collision callback: call `apply_damage(entity, impulse_magnitude / impulse_scale)`
- [ ] 8.3 Config: `destructible_impulse_scale` from `.uzproj` (converts N·s → damage units)

## 9. Testing

- [ ] 9.1 Unit test: `uzfrac` tool — 32 fragments generated, total volume within 5% of input
- [ ] 9.2 Unit test: `apply_damage` below threshold → health unchanged
- [ ] 9.3 Unit test: `apply_damage` to 0 → `fracture` called, `DestructibleBrokenEvent` posted
- [ ] 9.4 Unit test: `DebrisPool` at capacity (256) → 257th spawn claims LRU slot
- [ ] 9.5 Unit test: `debris_lifetime_s` timer → `reclaim` called after correct duration
- [ ] 9.6 Integration test: throw fast object at `CDestructible` wall → wall shatters into fragments within one frame
- [ ] 9.7 Integration test: 256 debris active → 257th fracture event reclaims oldest debris (not crash)

## 10. Documentation

- [ ] 10.1 Update `docs/specs/physics-system.md` — "Destructible Meshes" section with `CDestructible` fields, `uzfrac` tool usage
- [ ] 10.2 Create `docs/specs/destructible-meshes.md` — full spec: fracture workflow, debris pool, event system
- [ ] 10.3 Update `docs/guides/physics-guide.md` — "Setting Up Destructibles" section
