# Proposal: Fix SkyLight Ambient + Fog Color Injection — Close UE5 Visual Gap

## Why

UzEngine's outdoor scenes have **black shadows and no horizon fog transition** compared to UE5's Basic template. Side-by-side comparison reveals the gap is NOT caused by Lumen GI — it's caused by 3 broken/missing systems:

1. **SkyLight SH irradiance too weak or zero** — GPU readback of sky cubemap SH coefficients may be returning zeros, causing the deferred lighting fallback path (`ambientColor = {0.08, 0.09, 0.12}`) which is nearly black.

2. **Sky→Fog color injection missing** — UE5's `FogRendering.cpp:282-290` automatically injects `SkyLight.GetLightColor() * HeightFogContribution` into fog inscattering. Our fog `color = {0,0,0}` (correct UE5 default), but without the injection system, fog is invisible — no smooth sky-to-ground transition at the horizon.

3. **Fallback ambient too low** — When IBL SH is unavailable, `ambientColor = {0.08, 0.09, 0.12}` is far too dark. UE5 never has this problem because SkyLight always works.

### Visual Impact

| Problem | Visual Effect |
|---------|--------------|
| SH readback zeros/low | Shadowed areas are pitch black, no fill light |
| No fog color injection | Hard horizon line, no atmospheric haze, sky cuts to black ground |
| Low fallback ambient | If SH fails, everything outside direct sun is near-black |

### NOT Lumen

Lumen adds bounced GI (light reflecting off surfaces). For outdoor scenes with a functioning SkyLight, the visual difference between Lumen ON and OFF is ~15-20%. The UzEngine gap is ~60-70%, meaning the root cause is SkyLight/fog, not GI.

## What Changes

### 1. Diagnose and fix SH irradiance pipeline
- Run demo, capture SH L0 log values at frame 100+
- If zeros: trace GPU readback path, fix the copy/fence/map sequence
- If non-zero but low: compare with expected Earth sky SH values (~0.3-0.5 L0)
- Verify IBL path is taken (not fallback) in deferred_lighting_ps.hlsl

### 2. Implement sky→fog color injection (UE5 FogRendering.cpp parity)
- Read UE5 `FogRendering.cpp:282-290` — extract the exact injection formula
- In our fog renderer: when SkyAtmosphereComponent + HeightFogComponent both exist, compute `fogInscatteringColor += skyLightColor * heightFogContribution * skyAtmContribColorScale`
- This replaces the black `{0,0,0}` fog color with sky-derived color at runtime
- UE5 ref: `SkyAtmosphereAmbientContributionColorScale` property on HeightFogComponent

### 3. Fix deferred lighting ambient fallback
- Remove hardcoded `{0.08, 0.09, 0.12}` from LightFrameData/SceneLighting defaults
- When no IBL available, compute ambient from SkyLight component color × intensity × 0.1 (approximate hemisphere average)
- This is a safety net — with fix #1, the IBL path should always be active

## Impact
- Affected files: `fog_renderer.cpp`, `frame_setup.cpp`, `frame_lighting.cpp`, `height_fog_ps.hlsl`, `deferred_lighting_ps.hlsl`, `light_renderer.h`, `scene_lighting.h`
- Affected components: `HeightFogComponent` (may need `skyAtmosphereAmbientContributionColorScale` property)
- Breaking change: NO (visual improvement only, existing scenes look better)
- Risk: LOW (additive changes to existing working pipeline)
