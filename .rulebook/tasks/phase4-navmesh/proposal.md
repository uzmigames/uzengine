# Proposal: phase4-navmesh

## Why

The engine has no AI pathfinding capability. NPCs cannot navigate environments because there is no navmesh to query, no path-following component, and no local avoidance system. Without Recast+Detour integration, every game that needs NPCs to walk through a level must implement pathfinding from scratch. Dynamic obstacles (doors, crates) further require runtime navmesh modification that is not possible without a built-in solution. An engine without pathfinding cannot support any game with AI-driven characters.

## What Changes

- **Recast+Detour+DetourCrowd** integrated as vcpkg dependencies; `NavMeshSystem` owns the `dtNavMesh` and `dtCrowd` instances per scene
- **Offline baking** — `UzEngine.exe --cook` voxelizes static geometry with Recast and writes `.uznavmesh` alongside the level; bake parameters (cell size, agent radius, agent height, max slope) configurable in `.uzscene` `[navmesh]` section
- **Runtime baking** — `NavMeshSystem::bake_async()` for procedurally generated levels; background thread, signals completion via EventBus `navmesh.ready` event
- **`CNavMeshAgent` ECS component** — stores `dtCrowdAgentIndex`, target position, arrival threshold, max speed, and separation weight; `NavMeshSystem::update()` calls `dtCrowd::update()` and writes result velocity into `CCharacterController` or `CRigidBody`
- **`CNavObstacle` ECS component** — registers a `dtObstacleRef` with the tilecache for box or cylinder dynamic obstacles; automatically added/removed when the entity is activated/destroyed
- **Lua API** — `ctx.nav:set_target(agent_node, pos)`, `ctx.nav:stop(agent_node)`, `ctx.nav:find_path(start, end) -> Vec3[]`, `ctx.nav:is_reachable(start, end) -> bool`, `ctx.nav:get_nearest_point(pos) -> Vec3?`
- **Debug visualization** — DX12 overlay draws navmesh triangles (green walkable, red off-mesh), agent cylinders, planned paths, and obstacle extents when `r_debug_nav 1` cvar is set; visible in editor viewport without entering play mode

## Impact

- Affected specs: `docs/specs/navmesh.md`, `docs/specs/physics-system.md`, `docs/specs/lua-scripting.md`, `docs/specs/debug-profiling.md`
- Affected code: `src/navigation/navmesh_system.cpp`, `src/navigation/navmesh_system.h`, `src/scene/components.h`, `src/scripting/lua_nav.cpp`, `src/tools/cook/navmesh_baker.cpp`, `src/renderer/debug_overlay.cpp`
- Breaking change: NO — purely additive; scenes without `[navmesh]` config skip baking silently
- User benefit: NPC enemies can navigate complex environments with obstacle avoidance, door-aware pathfinding, and RVO2 multi-agent separation, all configured via ECS components and driven from Lua with no custom pathfinding code required.
