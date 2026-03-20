# Task 3.0.3 — Height Fog Complete Rewrite

## 1. Reference Analysis

- [x] 1.1 Read reference HeightFogCommon — extracted IntegrateExponentialFogRay(), GetExponentialHeightFog(), all constants, sky atmosphere injection, directional inscattering. Documented every formula. See `docs/analysis/atmosphere/ue5-height-fog-complete-reference.md`.
- [x] 1.2 Read reference FogRendering InitFogConstants() — extracted CPU-side precomputation of RayOriginTerms, CollapsedFogParameter, ExponentialFogParameters. All units in cm. Implemented in `fog_renderer.cpp:PopulateHeightFogCB()`.
- [x] 1.3 Read reference HeightFogPixelShader — extracted fullscreen pass structure, depth test, blend mode. Implemented in `height_fog_ps.hlsl` as fullscreen pixel shader with SrcAlpha/InvSrcAlpha blend.

## 2. Delete Current Implementation

- [x] 2.1 Delete and rewrite `shaders/runtime/atmosphere/height_fog_ps.hlsl` — old 289 lines replaced with 536-line reference-parity implementation.
- [x] 2.2 Rewrote `engine/renderer/fog/fog_renderer.cpp` (now ~340 lines) and `fog_renderer.h` (~35 lines) with full reference parity. PopulateHeightFogCB() with dual-layer RayOriginTerms, observer height clamp, sky atmosphere injection.
- [x] 2.3 Verified `height_fog_component.h` — all fields match reference defaults (density=0.02, falloff=0.2, maxOpacity=1.0, etc.). Second fog layer fields present.
- [x] 2.4 Build passes with zero errors on Zig, MSVC, Clang.

## 3. Rewrite Shader (from reference HeightFogCommon)

- [x] 3.1 Implement `IntegrateExponentialFogRay()` — exact reference formula with Taylor expansion fallback. All constants match: FLT_EPSILON2=0.01, Falloff clamp=-127.0f, log(2.0) Taylor.
- [x] 3.2 Implement main fog computation — RayOriginTerms, line integral, fogAmount, fogFactor = exp2(-fogAmount). Two-layer support with second layer guarded by density > 0 check.
- [x] 3.3 Implement start distance exclusion — recalculate RayOriginTerms at exclusion height from raw density (not ratio).
- [x] 3.4 Implement sky atmosphere ambient injection — `inscatterColor += colorScale * contribution * skyLightColor`.
- [x] 3.5 Implement directional inscattering — cosine-power phase * UniformPhaseFunction(1/4PI), atmosphere post-transmittance sun illuminance, directional fog factor with separate start distance.
- [x] 3.6 Implement sky pixel fog — uses farPlane distance for ray length when depth <= 0.0001 (reversed-Z far plane).
- [x] 3.7 Output: `float4(FogColor/fogOpacity, fogOpacity)` with SrcAlpha/InvSrcAlpha blend. Discard when fogOpacity < 0.001.

## 4. Rewrite CPU Constants (from reference FogRendering)

- [x] 4.1 Implement PopulateHeightFogCB() in fog_renderer.cpp — precompute RayOriginTerms = density * pow(2, -falloff * (observerH - fogH)). All units in cm. Clamped exponent to [-125, 126].
- [x] 4.2 Implement observer height clamp — min(camZ, min across all layers of (baseHeight + 65536cm)). Implemented in fog_renderer.cpp:158-171.
- [x] 4.3 Implement second fog layer — same formula, additive. fogComp.secondFogDensity > 0 guard. RayOriginTerms computed per-layer. Implemented in fog_renderer.cpp:184-212.
- [x] 4.4 Implement sky atmosphere post-transmittance sun illuminance — skyFogR/G/B from SH L0 coefficients * skyLightIntensity, passed via fog pass parameters in frame_lighting.cpp:873-883.
- [x] 4.5 Populate HeightFogCB with all reference-parity fields — density, heightFalloff, baseHeight, startDistance, cutoffDistance, maxOpacity, inscatterColor, directionalInscatterColor, directionalExponent, directionalStartDistance, secondFog fields, fogHeight, skyLightColor. All verified.

## 5. Integration

- [x] 5.1 Fog pass wired in frame_lighting.cpp:878 — after deferred lighting, before postprocess. Guarded by `m_passInspector.IsPassEnabled("fog")`.
- [x] 5.2 Pipeline blend state — SrcAlpha/InvSrcAlpha confirmed in fog pass pipeline.
- [x] 5.3 HeightFogComponent fields match reference defaults — density=0.02, falloff=0.2, maxOpacity=1.0, color=black (sky injection provides color). Second layer: secondFogDensity=0, secondFogHeightFalloff=0.2.
- [x] 5.4 Updated pbr_test.cpp fog entity — density=0.0436, falloff=0.2, baseHeight from actor Z=-5000 (matching reference Basic scene values).

## 6. Visual Verification

- [x] 6.1 Screenshot: pbr_test with fog — horizon haze appears, ground stays visible, sky-colored inscattering.
- [x] 6.2 Screenshot: sky atmosphere + fog — sky renders correctly, fog blends with horizon.
- [x] 6.3 Screenshot: ambient skylight + fog — shadow side of spheres lit, fog color derived from sky SH.
- [x] 6.4 Screenshot: full scene — fog inscattering, directional inscattering toward sun, distance fade correct.
- [x] 6.5 Visual comparison: matches reference Basic scene appearance (blue-white horizon haze, ground visible at distance).
- [x] 6.6 Distance test: ground does NOT disappear at distance with default parameters.
- [x] 6.7 Close-up test: no blow-out or excessive fog at near range.

## 7. Documentation

- [x] 7.1 Analysis doc at `docs/analysis/atmosphere/ue5-height-fog-complete-reference.md` — complete reference implementation guide.
- [x] 7.2 All shader functions have `@ref` citations to published papers (Hillaire 2016 "Physically Based Volumetric Rendering"). Proprietary source references removed during IP remediation pass.
