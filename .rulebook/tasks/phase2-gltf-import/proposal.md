# Phase 2 — GLTF Mesh Import

## Context

There are no real meshes in the engine — only the hard-coded pyramid. This task adds GLTF 2.0 import support using `fastgltf`, uploading vertex/index buffers to GPU, and exposing the mesh as a renderable `StaticMesh` node.

## Goal

- Load `.glb` / `.gltf` files and create GPU vertex + index buffers
- Support vertex attributes: position, normal, tangent, texcoord0
- LOD generation (3 levels) using `meshoptimizer`
- Mesh registered in asset registry with `.import.json`
- `StaticMesh` node references mesh by `res://` path
- PBR materials from GLTF material data (mapped to `.uzmat`)

## Dependencies

- **phase1-scene-system** (StaticMesh node type)
- **phase2-pbr-renderer** (needs vertex layout with tangents)
