# LOD System

## Why

All geometry renders at full detail regardless of distance, wasting GPU vertex/pixel budget on objects that occupy a few pixels on screen. UE5 selects LOD by screen-coverage percentage. P2 gap — required for scenes with more than a few dozen meshes to maintain framerate. Dependencies: mesh format must support multiple LOD levels (3.11.0 glTF import).

## What Changes

### New Systems
1. **LODGroup** — per-mesh LOD chain: array of (mesh, screenSizeThreshold) pairs. LOD0 = full detail, LOD1-3 = progressively simplified.
2. **LOD selection** — per-frame: compute screen-space bounding sphere radius for each object, select LOD level by threshold comparison. CPU-side, per-object.
3. **Dithered cross-fade** — during LOD transition, render both LODs with dithered alpha (screen-door pattern) for smooth visual transition over N frames.

### Refactoring
- RenderWorld StaticMeshProxy extended with LODGroup reference
- Draw list builder selects mesh from LODGroup based on computed LOD level
- glTF importer (3.11.0) extended to import LOD meshes from glTF extras or multiple mesh primitives

## Impact
- Affected specs: `docs/engine/12-rendering/02-visibility-pipeline.md`
- Affected code: `runtime/engine/renderer/core/draw_list.*`, `runtime/engine/renderer/render_world.*`, `runtime/engine/renderer/mesh/`
- Breaking change: NO (single-LOD meshes work as LOD0-only)
- User benefit: Scenes with 100+ objects maintain framerate via automatic detail reduction
- Enables: GPU-driven rendering (Phase 4) can use LOD for indirect draw selection
