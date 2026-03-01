# Phase 4 — Terrain System

## Goal

Full height-map terrain with splatmap material blending, GPU-instanced foliage, an optional water plane, and a suite of editor sculpt/paint tools. Implemented as the `uz_terrain` plugin (depends on phase4-plugin-system). Terrain is defined by a plain-text `.uzterrain` INI file + a 16-bit PNG heightmap + RGBA splatmap PNG. Collision uses Jolt `HeightFieldShape` for O(1) per-point queries. LOD uses 4 levels per 32×32 m chunk with geomorphing transitions to avoid popping.

## Spec Reference

[Terrain System Spec](../../../docs/specs/terrain-system.md)

## Dependencies

- phase4-plugin-system (terrain is shipped as the `uz_terrain` plugin)
- phase4-navmesh (terrain geometry is collected for navmesh baking; `navmesh_area` per terrain)
- phase3-ragdoll (Jolt physics integration; HeightFieldShape added alongside existing Jolt shapes)
- phase2-pbr-renderer (terrain shader extends the PBR pipeline; uses same G-Buffer pass)
- phase2-texture-tools (heightmap + splatmap textures imported via DirectXTex pipeline)
