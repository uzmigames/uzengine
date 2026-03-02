# Proposal: phase2-deferred-decals

## Why

Games need to place dynamic surface details — bullet holes, blood splatter, scorch marks, graffiti — on arbitrary geometry at runtime without modifying mesh UVs or creating unique texture sets. Deferred decals solve this by projecting a texture box onto the GBuffer in screen space, writing albedo/normal/roughness into a separate DBuffer that is composited before the lighting pass.

The render graph makes deferred decals straightforward: a `DecalPass` reads the depth buffer, reconstructs world position, tests against `CDecal` component bounding boxes, and writes to `DBuffer` render targets. No geometry modification required.

## What Changes

- **`ecs/components.h`** — `CDecal` component: `material_path`, `projection_box` (half-extents), `blend_mode` enum, `fade_distance`
- **`src/engine/renderer/passes/decal_pass.h/cpp`** — `DecalPass` `RgPass`: reads depth, writes to DBuffer (albedo, normal, roughness mask)
- **`shaders/decal.hlsl`** — decal projection shader
- **`src/engine/renderer/renderer.cpp`** — DBuffer RTs registered; lighting pass reads DBuffer
- **Tests** — decal projection correctness, blend modes, overdraw ordering, editor gizmo

## Impact

- Affected specs: `docs/specs/renderer.md` (DBuffer section)
- Dependencies: `phase1-render-graph` (RgPass API), `phase2-pbr-renderer` (GBuffer layout known)
- Breaking change: NO — decals are opt-in via `CDecal` component
- User benefit: runtime surface detail without mesh modifications; bullet holes, puddles, graffiti all via one system
