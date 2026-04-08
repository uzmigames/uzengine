# Proposal: Baked Lightmap GI (Phase 2)

## Why
UzEngine only has real-time GI (radiance cache, SSGI). Static scenes like architectural interiors and level art benefit enormously from pre-baked lightmaps — higher quality diffuse GI with zero runtime cost. UE5, Godot, and Unity all support baked lightmaps. This fills the gap for static-lighting scenarios where real-time GI is overkill or too expensive on lower hardware.

## What Changes

### New Systems
1. `LightmapBaker` — GPU compute-based lightmap baker that traces rays against scene geometry using the radiance cache
2. `LightmapPass` — Render pass that applies baked lightmap UV2 texture during deferred lighting
3. Lightmap UV generation — auto-unwrap UV2 channel for lightmap atlas
4. `.uzlightmap` asset format — compressed lightmap atlas storage

### Refactoring
- Mesh loading: read UV2 channel (lightmap UVs) from glTF/FBX
- deferred_lighting_ps.hlsl: add lightmap sampling path when lightmap is available
- Material system: add lightmap texture slot and UV2 index

## Impact
- Affected specs: docs/specs/rendering/renderer.md, docs/specs/rendering/shader-library.md
- Affected code: NEW runtime/engine/renderer/passes/lighting/lightmap_pass.cpp/.h, NEW runtime/engine/renderer/lighting/lightmap_baker.cpp/.h, runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl
- Breaking change: NO — additive feature
- User benefit: High-quality baked GI for static scenes with zero runtime cost
- Enables: Architectural visualization, mobile-tier rendering, hybrid baked+realtime GI
