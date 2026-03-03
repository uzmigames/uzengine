## 1. Library Integration (vcpkg)

- [ ] 1.1 Add `recastnavigation` to `vcpkg.json` dependencies
- [ ] 1.2 CMake: link `Recast`, `Detour`, `DetourCrowd`, `DetourTileCache` targets
- [ ] 1.3 Verify vcpkg build on Windows x64 — no link errors

## 2. Navmesh Baking (NavBaker)

- [ ] 2.1 `NavConfig` struct: `cell_size`, `cell_height`, `agent_radius`, `agent_height`, `agent_max_climb`, `agent_max_slope`, `edge_max_len`, `edge_max_error`, `region_min_size`, `region_merge_size`
- [ ] 2.2 `NavConfig` parsed from `[navmesh]` block in `.uzscene` or `project.uzproj`
- [ ] 2.3 `NavBaker::collect_geometry(scene)` — walks all `StaticBody`/`StaticMesh` nodes, extracts triangle soup
- [ ] 2.4 Recast pipeline: `rcCreateHeightfield` → `rcFilterLowHangingWalkableObstacles` → `rcFilterLedgeSpans` → `rcFilterWalkableLowHeightSpans`
- [ ] 2.5 `rcBuildCompactHeightfield` → `rcErodeWalkableArea` → `rcBuildDistanceField` → `rcBuildRegions`
- [ ] 2.6 `rcBuildContours` → `rcBuildPolyMesh` → `rcBuildPolyMeshDetail`
- [ ] 2.7 `dtCreateNavMeshData` → `dtNavMesh` + `dtNavMeshQuery`
- [ ] 2.8 `NavBaker::export_binary(out_path)` — write `.uznavmesh` with UzEngine header + Detour tile blobs (see spec format)
- [ ] 2.9 Offline bake: `build.bakeNavmesh` IPC handler runs `NavBaker` on worker thread, streams progress
- [ ] 2.10 Runtime bake: `runtime_bake = true` in `.uzscene` triggers bake on scene load (worker thread)
- [ ] 2.11 Navmesh area IDs: parse `[navmesh.areas]` from `project.uzproj`, pass area costs to Detour
- [ ] 2.12 Per-node area override: `navmesh_area` property on `StaticBody` nodes

## 3. NavSystem Runtime

- [ ] 3.1 `NavSystem` class: owns `dtNavMesh*`, `dtNavMeshQuery*`, `dtCrowd*`, `dtTileCache*`
- [ ] 3.2 `NavSystem::load(scene, navmesh_path)` — load `.uznavmesh`, create `dtNavMesh` from tile blobs
- [ ] 3.3 `NavSystem::bake(scene, cfg)` — blocking bake (call from worker thread)
- [ ] 3.4 `dtNavMeshQuery` init with `MAX_NODES = 2048`
- [ ] 3.5 `dtCrowd` init: `MAX_AGENTS = 256`, shared `dtNavMesh`
- [ ] 3.6 `NavSystem::update(dt, scene)`: advance `dtCrowd::update(dt)`, sync agent positions to node transforms, update `dtTileCache`
- [ ] 3.7 `NavSystem::find_path(start, end, area_mask)` → `std::vector<glm::vec3>` waypoints
- [ ] 3.8 `NavSystem::nearest_point(pos, search_radius)` → `glm::vec3`
- [ ] 3.9 Called from `SceneSystem::update()` each frame

## 4. NavMeshAgent Component

- [ ] 4.1 `CNavMeshAgent` struct: `speed`, `acceleration`, `turn_speed`, `radius`, `height`, `stopping_distance`, `area_mask`, `avoidance_quality`; runtime: `crowd_agent*`, `destination`, `has_destination`, `reached`
- [ ] 4.2 `NavMeshAgent` node type registered in SceneManager
- [ ] 4.3 `.uzscene` parser: `type=NavMeshAgent` with all component properties
- [ ] 4.4 `NavSystem::register_agent(node, comp)` — creates `dtCrowdAgent` with params
- [ ] 4.5 `NavSystem::unregister_agent(node)` — removes from `dtCrowd`
- [ ] 4.6 Agent position sync: each frame, write `dtCrowdAgent::npos` back to node Transform3D
- [ ] 4.7 Agent velocity sync: write `dtCrowdAgent::vel` to `CNavMeshAgent::current_velocity`
- [ ] 4.8 Arrived detection: `|agent_pos - destination| < stopping_distance` → set `reached = true`

## 5. NavObstacle Component

- [ ] 5.1 `CNavObstacle` struct: `shape` (Cylinder/Box), `radius`, `height`, `half_extents`; runtime: `obstacle_ref`
- [ ] 5.2 `NavObstacle` node type registered in SceneManager
- [ ] 5.3 `NavSystem::register_obstacle(node, comp)` — `dtTileCache::addObstacle()`
- [ ] 5.4 `NavSystem::unregister_obstacle(node)` — `dtTileCache::removeObstacle()`
- [ ] 5.5 Each frame: update obstacle position from node Transform3D (if node moved)
- [ ] 5.6 `dtTileCache::update(dt, navmesh)` called each frame with 1ms time budget

## 6. Lua API

- [ ] 6.1 `ctx.nav:set_destination(agent_id, vec3)` → bool (false if unreachable)
- [ ] 6.2 `ctx.nav:get_velocity(agent_id)` → Vec3
- [ ] 6.3 `ctx.nav:has_path(agent_id)` → bool
- [ ] 6.4 `ctx.nav:stop(agent_id)`
- [ ] 6.5 `ctx.nav:warp(agent_id, vec3)` — teleport + snap to navmesh
- [ ] 6.6 `ctx.nav:find_path(start, end, area_mask?)` → array of Vec3, or nil
- [ ] 6.7 `ctx.nav:nearest_point(pos, search_radius?)` → Vec3
- [ ] 6.8 `ctx.nav:on_arrived(agent_id, callback)` — Lua callback on arrival

## 7. IPC Handlers

- [ ] 7.1 `nav.bake {}` → progress events → `nav.bakeComplete`
- [ ] 7.2 `nav.setDestination { agent_id, target: [x,y,z] }` → `{ ok, reachable }`
- [ ] 7.3 `nav.stop { agent_id }`
- [ ] 7.4 `nav.warp { agent_id, position: [x,y,z] }`
- [ ] 7.5 `nav.findPath { start, end, area_mask? }` → `{ waypoints }`
- [ ] 7.6 `nav.getAgentState { agent_id }` → `{ position, velocity, has_path, reached }`
- [ ] 7.7 `nav.setDebugDraw { enabled }`

## 8. Debug Visualization

- [ ] 8.1 `NavSystem::set_debug_draw(bool)` — enables ImGui-based overlay
- [ ] 8.2 Draw navmesh triangles projected to screen via camera VP matrix; color by area ID
- [ ] 8.3 Draw agent paths as white polylines
- [ ] 8.4 Draw agent velocity vectors as magenta arrows
- [ ] 8.5 Draw NavObstacle extents as orange wireframe cylinders/boxes
- [ ] 8.6 Toggle button in editor overlay toolbar: "NavMesh" label

## 9. Build System Integration (Cook Step)

- [ ] 9.1 `CookPipeline::cook_navmesh(scene_path)` — bake if `.uznavmesh` is stale or missing
- [ ] 9.2 Stale check: compare navmesh bake timestamp against `.uzscene` mtime
- [ ] 9.3 Output: `.import/navmesh/<scene_name>.uznavmesh`
- [ ] 9.4 Pack: `.uznavmesh` included in `.uzpak`

## 10. Testing

- [ ] 10.1 Single agent: `set_destination` to reachable point → agent moves, `on_arrived` fires
- [ ] 10.2 Single agent: `set_destination` to unreachable point → returns false, agent stays
- [ ] 10.3 Multi-agent: 10 agents with same destination → no agents overlap (RVO2 separation)
- [ ] 10.4 Dynamic obstacle: add `NavObstacle` at midpoint → agent reroutes around it
- [ ] 10.5 Area cost: road area has cost 0.5 → agent prefers road over shortcut through grass
- [ ] 10.6 `nav.findPath` IPC returns correct waypoint list for a simple L-shaped corridor
- [ ] 10.7 Debug draw: navmesh triangles visible in editor viewport when toggled
- [ ] 10.8 Cook integration: `.uznavmesh` generated by `build.cook`, loaded from `.uzpak` in shipping
