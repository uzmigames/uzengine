# Proposal: 3.4.0 — Shadow Maps

## Why
Directional light cascaded shadow maps are essential for outdoor scenes. CSM provides shadow coverage at all distances with PCF soft edges.

## What Changes
- CSM with 4 cascades: frustum split, depth-only pass, shadow atlas
- PCF filtering (3x3 or 5x5 percentage-closer filtering)
- Cascade blending for smooth transitions between boundaries
- Shadow render graph pass with auto barriers

## Impact
- Affected specs: `docs/engine/12-rendering/`
- Affected code: `runtime/core/renderer/shadows/`, shaders
- Breaking change: NO
- User benefit: Realistic shadows at all distances, soft edges
