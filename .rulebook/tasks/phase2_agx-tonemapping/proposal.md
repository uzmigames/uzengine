# Proposal: AgX Tone Mapping (Phase 2)

## Why
UzEngine currently offers ACES, Filmic, and Reinhard tone mapping. AgX (by Troy Sobotka, adopted by Blender and Three.js r160+) provides significantly better hue preservation in saturated highlights compared to ACES — orange fire stays orange instead of shifting to yellow/white. It is perceptually uniform and handles out-of-gamut colors gracefully. Adding it as an option gives artists more creative control. Implementation is trivial (1-2 days).

## What Changes

### New Systems
1. `AgXTonemap(float3 color)` — AgX transfer function + optional "Punchy" look transform in tonemap_ps.hlsl
2. Tonemap operator selection: add AgX to the existing operator enum

### Refactoring
- tonemap_ps.hlsl: add `#elif TONEMAP_AGX` branch alongside existing ACES/Filmic/Reinhard
- PostProcessSettings: add AgX option to tonemap operator selection

## Impact
- Affected specs: docs/specs/rendering/shader-library.md
- Affected code: runtime/shaders/runtime/postprocess/tonemap_ps.hlsl, runtime/engine/renderer/postprocess/
- Breaking change: NO — additive option, ACES remains default
- User benefit: Better hue preservation for saturated content, artistic choice
- Enables: Blender-compatible rendering pipeline
