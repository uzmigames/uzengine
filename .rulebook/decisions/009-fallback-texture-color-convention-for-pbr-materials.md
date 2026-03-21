# 9. Fallback Texture Color Convention for PBR Materials

**Status**: proposed
**Date**: 2026-03-15

## Context

When a PBR material doesn't have a specific texture assigned, fallback textures are used. The color of these fallbacks determines the default material behavior. Wrong fallback colors can silently override material parameters (e.g., black metallic fallback forces metallic=0 regardless of parameter).

## Decision

All fallback textures are WHITE (1,1,1,1) by default, meaning the material parameter value controls the property. Two exceptions: (1) Normal map fallback = flat blue (0.5, 0.5, 1.0) representing no perturbation, (2) Displacement map fallback = BLACK (0,0,0,0) representing no displacement offset. This convention ensures material parameters work as expected when textures are absent.

## Alternatives Considered

- All black fallbacks (breaks metallic/roughness parameter control)
- All mid-gray (0.5) fallbacks (unexpected visual results)
- No fallback — require all textures (impractical for testing)

## Consequences

Pros: Material parameters work correctly without textures, consistent across all material types, discovered and fixed 2026-03-06. Cons: Must document convention clearly, new texture types need explicit fallback color decision. Key: WHITE = parameter controls value, BLACK = zero/no-effect, BLUE = neutral normal.
