# Proposal: Material Graph Pipeline End-to-End (Phase 2)

## Why
The material graph system (MaterialGraph, HlslMaterialTranslator, MaterialPipeline, MaterialManager) is 90% implemented but has 6 blocking gaps that prevent any .uzmat material from rendering on screen. All sample scenes currently hardcode texture paths directly in scene JSON — bypassing the material graph entirely. This means:
- No channel routing (glTF packed metallic-roughness textures can't be split into correct channels)
- No material reuse across scenes (each scene duplicates texture paths)
- No material graph editing (the graph compiler output is never consumed by the renderer)
- No material instancing (parameter overrides without duplicating the entire material)

The DamagedHelmet glTF showcase is blocked: its metallic-roughness texture packs G=roughness, B=metallic into one file, but the fixed-PBR path only reads the R channel of each texture. The material graph solves this with TextureSample nodes that expose per-channel outputs (R, G, B, A).

## What Changes

### Gap Fixes (6 items)
1. **gbuffer_ps.hlsl injection marker** — Add `#include "material_body.hlsli"` marker and `#ifdef UZ_MATERIAL_GRAPH` branch that calls the generated `ComputeMaterialOutputs()` function. Add `materialParams` cbuffer at register b3.
2. **TextureSample texIndex declaration** — Fix `HlslMaterialTranslator` to declare `uint texIndex_N` variables in a cbuffer so the generated HLSL compiles when TextureSample nodes are present.
3. **Default .uzmat format** — Fix `defaults/default_lit.uzmat` and `default_unlit.uzmat` to use the parser's expected format: `"Vector3Constant"` type names, `"scalarParams"` keys, inline `srcExpr`/`srcPin` connections on input pins.
4. **StaticMeshComponent graph field** — Add `uint32_t materialGraphId` to `StaticMeshComponent` so the renderer can detect graph-based materials.
5. **Scene loader .uzmat support** — Add `"material_graph"` field parsing in `scene_loader.cpp` that loads the graph via `MaterialManager` and stores the ID on the component.
6. **SyncWorldToDrawList integration** — When a mesh has a graph material, call `MaterialPipeline::GetOrCreatePSO(graph)`, set `cmd.materialGraphPipeline`, and populate `cmd.materialParams[]`.

### New Materials
- `sample/content/materials/damaged_helmet.uzmat` — glTF PBR with TextureSample channel routing
- Convert all sample scenes from inline texture paths to .uzmat references

## Impact
- Affected specs: `docs/specs/rendering/shader-variants.md`, new `docs/specs/rendering/material-graph-pipeline.md`
- Affected code: `gbuffer_ps.hlsl`, `hlsl_material_translator.cpp`, `scene_loader.cpp`, `scene_sync.cpp`, `static_mesh_component.h`, `defaults/*.uzmat`, all `sample/content/scenes/*.uzscene`
- Breaking change: NO — fixed-PBR path remains as fallback. Graph materials are opt-in via `"material_graph"` field.
- User benefit: Correct glTF metallic-roughness rendering, reusable materials, full material graph editing
