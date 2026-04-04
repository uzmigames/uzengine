# Proposal: Omnidirectional PCSS Soft Shadows (Phase 2)

## Why
UzEngine has PCSS (blocker search + variable penumbra) for directional lights but point and spot lights only use basic PCF filtering — their shadows have uniform hardness regardless of distance from the shadow caster. Adding PCSS to point/spot lights makes indoor scenes look significantly more realistic with soft shadow penumbras that widen with distance.

## What Changes

### New Systems
1. Point light PCSS: blocker search in shadow atlas region, variable PCF kernel based on estimated penumbra
2. Spot light PCSS: same technique adapted for perspective shadow projection

### Refactoring
- shadow_pcss.hlsli: extend blocker search and variable-penumbra PCF to work with atlas-based point/spot shadow regions
- deferred_lighting_ps.hlsl: use PCSS path for point/spot lights when `r.ShadowPCSS` is enabled

## Impact
- Affected specs: docs/specs/rendering/shader-library.md
- Affected code: runtime/shaders/runtime/include/shadow_pcss.hlsli, runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl
- Breaking change: NO — additive quality improvement, PCF fallback when PCSS disabled
- User benefit: Realistic soft shadows from point/spot lights
- Enables: Higher quality indoor lighting
