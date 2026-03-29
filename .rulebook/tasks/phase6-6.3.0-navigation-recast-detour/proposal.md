# Proposal: phase6-6.3.0-navigation-recast-detour

## Why
NPC movement requires pathfinding. Recast (navmesh generation) + Detour (path queries) + DetourCrowd (local avoidance) is the industry standard used by UE5, Unity, Godot, and most AAA games. Without navmesh, AI agents can't navigate the world, avoid obstacles, or coordinate movement. This unlocks behavior trees, patrol routes, and any AI-driven gameplay.

## What Changes
- Recast integration: offline navmesh baking from scene geometry via uzcook
- Detour: A* pathfinding on navmesh tiles
- CNavMeshAgent ECS component: speed, radius, height, pathfinding
- DetourCrowd: multi-agent local steering with RVO2 avoidance
- CNavObstacle: dynamic obstacles with tile cache rebuild
- .uznavmesh binary format for baked navmesh data
- Lua API: uz.nav.find_path, move_to, set_destination

## Impact
- New directory: `runtime/engine/navigation/`
- Thirdparty: recastnavigation (add to setup.ps1)
- Depends on: Physics (raycasts for geometry), Scene (static mesh data)
- Spec: navmesh.md
- Breaking change: NO
