# Proposal: phase1_fix-sponza-lighting-calibration

## Why

Sponza scene renders flat, washed out, and warm-tinted compared to the hybrid-rendering reference.
Root cause analysis (`docs/analysis/rendering/sponza-lighting-rootcause.md`) identified 4 bugs:

1. Sun intensity=80 with exposure=1.0 pushes all lit pixels onto the ACES shoulder, compressing
   a 9:1 HDR contrast ratio to 1.6:1 post-tonemap — destroying sun shaft visibility.
2. `skyTint` in deferred_lighting_ps.hlsl double-multiplies IBL intensity (SH scaled by 1.5 on CPU,
   then multiplied by skyLightIntensity=1.5 again in shader = net 2.25×), making shadow regions
   too bright relative to direct light.
3. Warm `sky_light.light_color={1,0.95,0.88}` compounds with an already-warm sunset HDRI, producing
   pink/salmon cast on all indirect-lit surfaces.
4. `r.Tonemap.Slope/Toe` CVars are dead code under operator 1 (Hill ACES) — user adjustments
   have zero effect.

## What Changes

- Scene file: reduce sun intensity, neutralize sky_light color, remove dead CVars
- Shader: remove skyLightIntensity from skyTint (fix double-multiply)
- No changes to shadow pipeline, BRDF, GBuffer, or tonemap operator code

## Impact
- Affected specs: none (lighting calibration, not API)
- Affected code: sponza.uzscene, deferred_lighting_ps.hlsl
- Breaking change: NO (scene-specific values + shader bugfix)
- User benefit: Sponza renders with correct contrast, neutral colors, and visible sun shafts
