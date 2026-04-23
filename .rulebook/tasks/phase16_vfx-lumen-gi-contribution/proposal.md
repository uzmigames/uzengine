# Emissive Particle GI Contribution (DDGI Injection)

## Why

Emissive particles (explosions, magic bursts, fire plumes) do NOT currently contribute to the engine's global illumination (DDGI probe grid). They light themselves and act as local lights if `LightRenderModule` injects point lights, but the ambient bounce lighting the scene receives from a big explosion is missing — rooms stay dark during a fireball.

Niagara has `Lumen Radiance Cache` integration that marks emissive particles as radiance contributors. Our equivalent is DDGI — we should inject particle radiance into the probe capture pass so a fireball under Sponza's roof correctly bounces warm light onto the walls.

This is the single largest visual upgrade available from the VFX layer for the `feat/sponza-ddgi-lighting-improvements` target.

## What Changes

- New pass `VfxDdgiInjectionPass` runs during the DDGI probe ray dispatch.
- When a probe's ray hits an emissive particle AABB (or a particle-light AABB), contribute its emissive radiance to the probe's irradiance accumulator.
- Implementation options (rank from simplest):
  1. **Cheap**: bake all active emissive particles into a small "virtual light list" that DDGI samples like regular lights (reuses light grid infrastructure).
  2. **Medium**: rasterize emissive particles into a low-res cube map centered on each probe during probe update.
  3. **Expensive**: full RT tracing into particle billboards (requires RT support).
- Start with approach 1: aggregate per-emitter emissive output into a single virtual light per emitter (position = emitter center, radius = emitter AABB, intensity = avg emissive × alive count).

## Impact

- Affected specs: DDGI probe update pipeline, VFX aggregation pass
- Affected code: new `vfx_ddgi_injection_pass.cpp`, DDGI probe update CS, new virtual-light buffer
- Breaking change: NO (CVar-gated, default on but fallback preserves existing behavior)
- User benefit: emissive VFX correctly contributes bounce lighting to scenes (fireball lights up Sponza atrium)

## Risks

- DDGI probe capture is ray-based; approach 1 (virtual lights) only approximates emissive particle distribution. May need approach 2 for tight localized glows. Plan: ship approach 1, capture visual gap, decide on approach 2.
