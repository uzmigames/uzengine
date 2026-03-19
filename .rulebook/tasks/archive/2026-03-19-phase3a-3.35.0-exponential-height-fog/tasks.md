## 1. Implementation

- [x] 1.1 Define FogParameters struct — HeightFogCB in constant_buffers.h with density, falloff, baseHeight, startDistance, fogColor, directionalInscatteringColor/Exponent/StartDistance, maxOpacity, fogCutoffDistance, second layer params, sky atmosphere injection fields. All defaults match UE5 ExponentialHeightFogComponent.cpp:64-103.
- [x] 1.2 Add fog CB to root signature — HeightFogCB at b2 (per-object CBV slot, reused for fog pass). Uploaded per-frame via CBRingAllocator in fog_renderer.cpp.
- [x] 1.3 Implement exponential height fog formula in HLSL — UE5 CalculateLineIntegralShared (HeightFogCommon.ush:192-198) with Taylor expansion fallback (line 91), 2-layer additive combination (HeightFogCommon.ush:294-296), start distance exclusion with per-layer RayOriginTerms recalculation (HeightFogCommon.ush:285-289), cutoff distance. Second fog layer uses same line integral with separate density/falloff/height params.
- [x] 1.4 Implement directional inscattering — `dirFactor = pow(saturate(dot(viewDir, sunDir)), exponent)` with separate line integral and start distance. @source UE5 HeightFogCommon.ush:330-350. Blended via SrcAlpha/InvSrcAlpha compositing.
- [x] 1.5 Integrate fog as separate fullscreen pass — height_fog_ps.hlsl renders after deferred lighting as an alpha-blended fullscreen quad. SrcAlpha/InvSrcAlpha blending: inscatterColor * fogOpacity + scene * (1-fogOpacity). Sky pixels use farPlane distance for horizon haze.
- [x] 1.6 Add FogComponent to render world — HeightFogComponent in engine/ecs/environment/height_fog_component.h. fog_renderer.cpp extracts first HeightFogComponent from ECS, precomputes UE5 RayOriginTerms on CPU (FogRendering.cpp:245-254), passes to HeightFogCB.
- [x] 1.7 Sky atmosphere injection — @source UE5 HeightFogCommon.ush:174-180. When SkyAtmosphere exists, fog inscattering += skyAtmContribColorScale * heightFogContribution * skyLightColor (from SH L0). This provides the blue/white horizon haze without hardcoded colors.
- [x] 1.8 HeightFogContribution AP anti-double-fog — @source UE5 HeightFogCommon.ush:175, SceneRendering.cpp:1440. In deferred_lighting_ps.hlsl, AP inscattering scaled by (1-heightFogContribution) to prevent double-fogging.
- [x] 1.9 Fog visible in pbr_test demo with sky-blue inscattering (0.448,0.639,1.0), directional sun glow (1.0,0.9,0.7), density=0.02, falloff=0.2. Horizon haze and atmospheric distance fog verified visually.

## 2. Visual Verification

- [x] 2.1 Fog active in pbr_test demo — height fog with sky atmosphere injection visible at horizon. Sky→fog color comes automatically from SH L0.
- [x] 2.2 Height falloff verified — fog density decreases with altitude (exponential decay, falloff=0.2).
- [x] 2.3 Directional inscattering implemented — pow(dot(viewDir, sunDir), exponent) with separate line integral.
- [x] 2.4 Regression: pbr_test (183 FPS), deferred_test render correctly with fog. DX11 zero errors.

## 3. Documentation

- [x] 3.1 UE5 sky→fog injection mechanism documented in docs/guides/ue5-sky-fog-injection.md — full data flow from component → proxy → view → shader with UE5 file:line citations.
- [x] 3.2 Fog parameters documented in height_fog_component.h with UE5 citations. Tuning: density (0.001-0.1), falloff (0.2-2.0), baseHeight, inscattering color/exponent. Detailed guide in docs/analysis/skylight-fog-parity.md.
