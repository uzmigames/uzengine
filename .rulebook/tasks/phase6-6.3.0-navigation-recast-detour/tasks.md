## 1. Recast/Detour Integration

- [ ] 1.1 Add recastnavigation to `scripts/setup.ps1` — Clone recastnavigation from `recastnavigation/recastnavigation`. Update `runtime/build.zig` to link Recast + Detour + DetourCrowd + DetourTileCache static libraries.
- [ ] 1.2 Create `runtime/engine/navigation/nav_config.h` — NavConfig: cell_size (0.3), cell_height (0.2), agent_radius (0.6), agent_height (2.0), agent_max_climb (0.9), agent_max_slope (45°), edge_max_len (12), region_min_size (8), detail_sample_dist (6), detail_sample_max_error (1). Loaded from .uzproj.

## 2. Navmesh Baker

- [ ] 2.1 Create `runtime/engine/navigation/nav_baker.h/.cpp` — NavBaker: Bake(scene_geometry, config) → dtNavMesh*. Steps: collect triangle soup from StaticMesh/terrain → rcCreateHeightfield → rcMarkWalkableTriangles → rcBuildCompactHeightfield → rcBuildDistanceField → rcBuildRegions → rcBuildContours → rcBuildPolyMesh → dtCreateNavMeshData.
- [ ] 2.2 Create .uznavmesh format — Binary: magic 'UZNV', version, tile count, then Detour tile data blobs. SaveNavMesh(dtNavMesh*, path), LoadNavMesh(path) → dtNavMesh*.
- [ ] 2.3 Add runtime bake option — If .uzproj `runtime_bake=true`, bake on scene load in background thread. Fire EventBus<NavMeshReadyEvent> when complete.

## 3. Navigation System

- [ ] 3.1 Create `runtime/engine/navigation/nav_system.h/.cpp` — NavSystem: owns dtNavMesh, dtNavMeshQuery (max 2048 polys per query), dtCrowd. Init(navmesh), Shutdown(). FindPath(start, end, filter) → vector<Vec3> waypoints. FindNearestPoint(point, radius) → Vec3. Raycast(start, end) → hit/miss + t.
- [ ] 3.2 Create NavSubsystem — WorldSubsystem with TickGroup::PostPhysics. OnTick: update dtCrowd, sync agent positions to TransformComponent.

## 4. NavMeshAgent Component

- [ ] 4.1 Create `runtime/engine/navigation/components/nav_mesh_agent.h` — CNavMeshAgent: speed (5.0), acceleration (8.0), turn_speed (360°/s), radius (0.6), height (2.0), stopping_distance (0.5), avoidance_quality enum (None/Low/Medium/High), area_mask (uint32, default ALL). Runtime: is_moving, remaining_distance, current_path, dtCrowdAgentIdx.
- [ ] 4.2 Create `runtime/engine/navigation/components/nav_mesh_agent.cpp` — SetDestination(target): FindPath → set path. Stop(): clear path. Update: dtCrowd handles movement. RegisterAgent: adds to dtCrowd. UnregisterAgent: removes.

## 5. Dynamic Obstacles

- [ ] 5.1 Create `runtime/engine/navigation/components/nav_obstacle.h/.cpp` — CNavObstacle: shape (Cylinder/Box), radius, height, half_extents. Uses dtTileCache for dynamic tile rebuild. AddObstacle/RemoveObstacle → trigger tile update (budget: 1ms/frame max).

## 6. Area Costs

- [ ] 6.1 Add area type support — NavArea: name, id, cost (float, default 1.0). Define areas: Road (cost 0.5), Grass (1.0), Water (4.0), Blocked (impassable). Agents prefer low-cost paths. Configurable per agent via area_mask.

## 7. Lua Navigation API

- [ ] 7.1 Create `runtime/engine/scripting/api/lua_nav.h/.cpp` — Register `uz.nav` in LuaVM. find_path(start, end) → waypoint array. move_to(entity, target) → sets destination. stop(entity). is_moving(entity). get_remaining_distance(entity). set_avoidance_quality(entity, quality). find_nearest_point(point, radius) → Vec3.

## 8. Scene Serialization

- [ ] 8.1 Add CNavMeshAgent to ComponentSerializerRegistry — JSON: speed, acceleration, radius, height, stopping_distance, avoidance_quality.
- [ ] 8.2 Add CNavObstacle serialization — JSON: shape, radius/half_extents, height.

## 9. Testing

- [ ] 9.1 Unit tests for NavBaker — bake simple floor, verify poly count > 0.
- [ ] 9.2 Unit tests for pathfinding — path from A to B on simple navmesh, no-path case.
- [ ] 9.3 Unit tests for CNavMeshAgent — set destination, verify movement, stopping distance.
- [ ] 9.4 Unit tests for crowd steering — 4 agents crossing, verify no overlap.
- [ ] 9.5 Unit tests for .uznavmesh — save/load round-trip.

## 10. Demo

- [ ] 10.1 Create `demos/navigation/` — room with walls, 10 agents pathfinding to random targets. Navmesh debug visualization (polygons as wireframe overlay). Headless + screenshot.

## 11. Documentation

- [ ] 11.1 Update `docs/engine/20-gameplay/` with navmesh baking, pathfinding, crowd steering.
- [ ] 11.2 Add `uz.nav` to `docs/lua-api-reference.md`.
- [ ] 11.3 Document NavConfig parameters and area cost tuning.

## 12. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 12.1 Update or create documentation covering the implementation
- [ ] 12.2 Write tests covering the new behavior
- [ ] 12.3 Run tests and confirm they pass
