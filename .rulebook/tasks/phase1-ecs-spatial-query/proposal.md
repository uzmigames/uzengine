# Proposal: phase1-ecs-spatial-query

## Why

NPC AI, physics triggers, and rendering frustum culling all need to answer "which entities are near this point?" or "which entity does this ray hit?". Without a spatial index, these queries require iterating all entities — O(N) per frame, which becomes prohibitive at 1000+ entities. A BVH (Bounding Volume Hierarchy) spatial index integrated with the EnTT ECS provides O(log N) sphere queries and raycasts, and automatically updates when `CTransform` or `CMeshRenderer` components change.

## What Changes

- **`src/engine/ecs/spatial_index.h/cpp`** — `SpatialIndex` interface and `BvhSpatialIndex` implementation
- **`src/engine/ecs/spatial_index_system.h/cpp`** — ECS system that keeps BVH in sync with `CTransform` changes
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.world:query_sphere(center, radius) -> NodeRef[]` and `ctx.world:raycast(origin, dir, max_dist) -> RaycastHit?` Lua API
- **Tests** — BVH build correctness, sphere query results, raycast hit/miss, EnTT observer integration

## Impact

- Affected specs: `docs/specs/scene-system.md` (spatial query API)
- Dependencies: `phase1-scene-system` (EnTT registry, CTransform component)
- Breaking change: NO — additive; existing scene code unchanged
- User benefit: NPC "find nearest enemy" in O(log N); frustum culling accelerated; physics trigger zones without physics engine overhead
