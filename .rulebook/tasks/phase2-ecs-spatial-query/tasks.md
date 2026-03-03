## 1. Prerequisites

- [ ] 1.1 Confirm `phase1-scene-system` is complete — EnTT registry, `CTransform`, `CMeshRenderer` exist
- [ ] 1.2 Read `docs/specs/scene-system.md` — understand component layout and node hierarchy
- [ ] 1.3 Survey BVH construction algorithms: top-down SAH (Surface Area Heuristic) vs. bottom-up agglomerative
- [ ] 1.4 Decide: top-down SAH for static geometry, incremental refit for dynamic entities — document decision

## 2. SpatialIndex Interface

- [ ] 2.1 Create `src/engine/ecs/spatial_index.h` — pure virtual `SpatialIndex` interface
- [ ] 2.2 Define `ISpatialIndex::insert(entity, AABB)` — add or update entity bounds
- [ ] 2.3 Define `ISpatialIndex::remove(entity)` — remove entity from index
- [ ] 2.4 Define `ISpatialIndex::query_sphere(Vec3 center, float radius, vector<entt::entity>&) const` — append results
- [ ] 2.5 Define `ISpatialIndex::query_frustum(Frustum, vector<entt::entity>&) const` — for culling
- [ ] 2.6 Define `ISpatialIndex::raycast(Vec3 origin, Vec3 dir, float max_dist) -> RaycastHit?` — returns nearest hit
- [ ] 2.7 Define `RaycastHit` struct: `entity`, `distance`, `hit_point`, `hit_normal`
- [ ] 2.8 Define `ISpatialIndex::clear()` — full reset

## 3. BvhSpatialIndex Implementation

- [ ] 3.1 Create `src/engine/ecs/bvh_spatial_index.h/cpp`
- [ ] 3.2 Define `BvhNode` struct: `AABB bounds`, `left_child`, `right_child`, `entity` (leaf only)
- [ ] 3.3 Implement `BvhSpatialIndex::build(span<pair<entity, AABB>>)` — top-down SAH partition
- [ ] 3.4 Implement SAH cost function: surface area of left + right child sets weighted by primitive count
- [ ] 3.5 Implement `refit()` — update parent AABBs after entity move (O(depth) per entity)
- [ ] 3.6 Implement `insert(entity, AABB)` — incremental insert with local refit; rebuild triggered if tree becomes unbalanced
- [ ] 3.7 Implement `remove(entity)` — mark leaf as removed; lazy compact on rebuild threshold
- [ ] 3.8 Implement `query_sphere` — traverse BVH, AABB-sphere overlap test at each node
- [ ] 3.9 Implement `query_frustum` — traverse BVH, AABB-frustum test (6-plane) at each node
- [ ] 3.10 Implement `raycast` — AABB slab test at each node; track nearest leaf hit
- [ ] 3.11 Implement rebuild trigger: if `removed_count > 0.25 * total_count`, schedule full rebuild next frame

## 4. SpatialIndexSystem — EnTT Integration

- [ ] 4.1 Create `src/engine/ecs/spatial_index_system.h/cpp`
- [ ] 4.2 Implement `SpatialIndexSystem::setup(entt::registry&)` — install EnTT observers
- [ ] 4.3 Install `on_construct<CTransform>` observer — `insert` entity on first CTransform add
- [ ] 4.4 Install `on_update<CTransform>` observer — `insert/update` entity bounds on transform change
- [ ] 4.5 Install `on_destroy<CTransform>` observer — `remove` entity from index
- [ ] 4.6 Implement `SpatialIndexSystem::update()` — flush observer queues, trigger rebuild if threshold exceeded
- [ ] 4.7 Compute entity AABB: from `CMeshRenderer.mesh.bounds` transformed by `CTransform.world_matrix`; fallback to unit AABB if no mesh

## 5. Lua API — ctx.world spatial queries

- [ ] 5.1 Implement `ctx.world:query_sphere(cx, cy, cz, radius) -> table` — returns Lua array of `NodeRef`
- [ ] 5.2 Implement `ctx.world:query_sphere(vec3, radius) -> table` — Vec3 userdata overload
- [ ] 5.3 Implement `ctx.world:raycast(ox, oy, oz, dx, dy, dz, max_dist) -> RaycastHit?` — returns nil on miss
- [ ] 5.4 Implement `ctx.world:raycast(origin_vec3, dir_vec3, max_dist) -> RaycastHit?`
- [ ] 5.5 Define `RaycastHit` Lua userdata: `.node` (NodeRef), `.distance` (number), `.point` (Vec3), `.normal` (Vec3)
- [ ] 5.6 Implement `ctx.world:query_frustum_nodes() -> table` — returns nodes visible in current camera frustum

## 6. Frustum Culling Integration

- [ ] 6.1 Update renderer culling path — replace linear entity scan with `SpatialIndex::query_frustum`
- [ ] 6.2 Verify rendered entity count matches linear scan results (regression test)
- [ ] 6.3 Measure culling performance improvement with 1000 entities

## 7. Testing

- [ ] 7.1 Unit test: `build([10 entities])` → `query_sphere(center, 5.0)` returns correct subset
- [ ] 7.2 Unit test: `raycast` against single AABB hit — correct distance and hit point
- [ ] 7.3 Unit test: `raycast` miss — returns nullopt
- [ ] 7.4 Unit test: `remove(entity)` → entity no longer returned by `query_sphere`
- [ ] 7.5 Unit test: BVH refit after entity moves — query returns updated positions
- [ ] 7.6 Unit test: rebuild trigger fires after 25% entities removed
- [ ] 7.7 Integration test: EnTT observer updates BVH when `CTransform` component changes
- [ ] 7.8 Integration test: `ctx.world:query_sphere(0,0,0, 10)` returns correct NodeRefs from Lua
- [ ] 7.9 Performance test: 1000 entities, `query_sphere` completes in < 0.1ms

## 8. Documentation

- [ ] 8.1 Update `docs/specs/scene-system.md` — add "Spatial Query API" section
- [ ] 8.2 Create `docs/specs/spatial-index.md` — BVH design, API reference, performance characteristics
- [ ] 8.3 Update `docs/guides/lua-scripting-guide.md` — "Spatial Queries" section with NPC use case example
