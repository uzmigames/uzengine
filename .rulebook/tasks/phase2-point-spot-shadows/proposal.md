# Phase 2 -- Point / Spot Light Shadows

## Why

The existing shadow system only covers directional light CSM. Point lights and spot lights currently contribute lighting without shadows, which looks unrealistic -- objects appear to float and there are no contact shadows from local light sources. Adding shadow support for point and spot lights is critical for indoor scenes, lamppost lighting, flashlights, and any scenario with local illumination.

## What Changes

- New ECS components: `CPointLight` extended with `cast_shadow`, new `CSpotLight` component.
- Point light shadows via **dual-paraboloid shadow maps** (2 faces per light, cheaper than cubemap).
- Spot light shadows via single **perspective projection** shadow map.
- Two new shadow atlases: point light atlas (2048x2048, up to 8 lights) and spot light atlas (2048x2048, up to 16 lights).
- Two new render passes: `PointLightShadowPass` (sort_key=110) and `SpotLightShadowPass` (sort_key=120).
- Shadow sampling with PCF 2x2 kernel in the deferred lighting pass.
- Shadow update budget system: prioritize lights by distance and movement.
- Tile-based light culling compute pass for efficient per-pixel light evaluation.
- New `.uzscene` node types: `PointLight`, `SpotLight` with shadow properties.

## Dependencies

- **phase2-pbr-renderer** (deferred lighting pass consumes shadow data)
- **phase2-shadow-maps** (CSM system provides foundation: shadow atlas pattern, PCF sampling)
- **phase2-shader-library** (shadow.hlsli provides PCF functions)

## Spec Reference

[Point/Spot Light Shadows Spec](../../../docs/specs/point-spot-light-shadows.md)

## Impact
- Affected specs: `renderer.md`, `scene-system.md`, `point-spot-light-shadows.md` (new)
- Affected code: `src/renderer/passes/`, `src/renderer/shadow_system/`, `src/engine/components/`, `shaders/`
- Breaking change: NO (new components and passes, existing systems unchanged)
- User benefit: Realistic shadows from point and spot lights; indoor scenes look correct; configurable shadow quality and budget
