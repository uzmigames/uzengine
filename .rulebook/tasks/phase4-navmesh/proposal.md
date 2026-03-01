# Phase 4 — NavMesh / Pathfinding

## Goal

Complete navigation system using Recast (navmesh baking) + Detour (path queries) + DetourCrowd (multi-agent RVO2 local avoidance). Supports offline baking at cook time, runtime baking for procedural levels, `NavMeshAgent` ECS component, `NavObstacle` dynamic obstacle component, and a full Lua scripting API. Debug visualization draws navmesh triangles, paths, and obstacle extents in the editor viewport.

## Spec Reference

[NavMesh / Pathfinding Spec](../../../docs/specs/navmesh.md)

## Dependencies

- phase4-build-pipeline (offline baking runs as part of the cook step)
- phase3-input-system (agent steering may respond to player input events)
- phase2-gltf-import / phase2-fbx-import (geometry for navmesh voxelization comes from imported meshes)
