# Phase 2 — FBX & GLTF Import Pipeline

## Context

The engine has no real mesh import. Assets are hard-coded. This task adds a full import pipeline for FBX (via ufbx) and GLTF (via fastgltf), outputting `.uzmesh` + `.uzskel` binaries plus LODs via meshoptimizer. The model preview panel in VSCode lets AI agents and developers inspect imported meshes.

## Goal

- FBX + GLTF parsed into engine-internal binary format
- Skeletal mesh support: bone hierarchy, skin weights, morph targets
- LOD generation: 4 levels via meshoptimizer
- Model preview panel in VSCode (turntable JPEG stream)
- `asset.import` IPC triggers re-import, progress streamed back

## Spec Reference

[Asset Pipeline Spec](../../../docs/specs/asset-pipeline.md)
[Skeletal Mesh Spec](../../../docs/specs/skeletal-mesh.md)

## Dependencies

- phase1-scene-system (resource handles, arena allocators)
