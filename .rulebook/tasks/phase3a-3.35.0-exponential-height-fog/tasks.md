## 1. Implementation

- [ ] 1.1 Define FogParameters struct — density (0.02 default), falloff (0.5), height (0.0), startDistance (0.0), inscatteringColor (RGB), directionalInscatteringExponent (4.0), directionalInscatteringColor (RGB), maxOpacity (1.0).
- [ ] 1.2 Add fog CB to root signature — single CBV slot with FogParameters, uploaded per-frame from SceneEnvironment gatherer.
- [ ] 1.3 Implement exponential height fog formula in HLSL — `fogFactor = 1 - exp(-density * max(0, distance - startDistance))` with altitude falloff: `heightFactor = exp(-falloff * max(0, worldPos.y - fogHeight))`. Combined: `fog = fogFactor * heightFactor * maxOpacity`.
- [ ] 1.4 Implement directional inscattering — `dirFactor = pow(saturate(dot(viewDir, sunDir)), directionalInscatteringExponent)`. Blend inscatteringColor with directionalInscatteringColor by dirFactor.
- [ ] 1.5 Integrate fog into deferred_lighting_ps.hlsl — after light accumulation, lerp final color toward fog color by fog factor. Preserve emissive (fog does not affect self-lit pixels).
- [ ] 1.6 Add FogComponent to render world — collects fog parameters from scene, packs into CB.
- [ ] 1.7 Add fog toggle to PostProcessStack — enable/disable flag, fog quality enum (Off/Low/High).
- [ ] 1.8 Create fog demo — flat terrain with objects at varying distances (10m, 50m, 200m, 500m), showing fog density gradient. Include directional inscattering from sun.

## 2. Visual Verification

- [ ] 2.1 Screenshot: fog OFF vs fog ON side-by-side — distant objects must fade into fog color
- [ ] 2.2 Screenshot: camera at ground level vs elevated — height falloff must reduce fog above fog height
- [ ] 2.3 Screenshot: directional inscattering — looking toward sun shows brighter fog, away shows neutral
- [ ] 2.4 Regression: existing demos (pbr_test, deferred_test) still render correctly with fog disabled

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/06-lighting.md` with fog parameters and integration point
- [ ] 3.2 Add fog tuning guide (density vs falloff vs height interaction)
