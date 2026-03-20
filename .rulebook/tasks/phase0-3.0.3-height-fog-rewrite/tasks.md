# Task 3.0.3 — Height Fog Complete Rewrite

## 1. UE5 Source Analysis

- [ ] 1.1 Read UE5 `HeightFogCommon.ush` line-by-line — extract CalculateLineIntegralShared(), GetExponentialHeightFog(), all constants, sky atmosphere injection, directional inscattering. Document every formula with line numbers.
- [ ] 1.2 Read UE5 `FogRendering.cpp` InitFogConstants() — extract the CPU-side precomputation of RayOriginTerms, CollapsedFogParameter, ExponentialFogParameters. Document unit system (cm).
- [ ] 1.3 Read UE5 `HeightFogPixelShader.usf` — extract the fullscreen pass structure, depth test, blend mode.

## 2. Delete Current Implementation

- [x] 2.1 Delete and rewrite `shaders/runtime/atmosphere/height_fog_ps.hlsl` — old 289 lines replaced with 536-line UE5-parity implementation.
- [ ] 2.2 Delete `engine/renderer/fog/fog_renderer.cpp` (280 lines) and `fog_renderer.h` (59 lines).
- [ ] 2.3 Verify `height_fog_pass.cpp/.h` and `height_fog_component.h` — keep pass shell and component, only gut the shader and fog constant computation.
- [ ] 2.4 Build — confirm compile with empty fog pass (fog disabled).

## 3. Rewrite Shader (from UE5 HeightFogCommon.ush)

- [x] 3.1 Implement `CalculateLineIntegralShared()` — exact UE5 formula with Taylor expansion fallback. Cited HeightFogCommon.ush:192-199. All constants match: FLT_EPSILON2=0.01, Falloff clamp=-127.0f, log(2.0) Taylor.
- [x] 3.2 Implement main fog computation — RayOriginTerms, line integral, fogAmount, fogFactor = exp2(-fogAmount). Two-layer support with second layer guarded by density > 0 check.
- [x] 3.3 Implement start distance exclusion — recalculate RayOriginTerms at exclusion height from raw density (not ratio). Cited HeightFogCommon.ush:272-291.
- [x] 3.4 Implement sky atmosphere ambient injection — `inscatterColor += colorScale * contribution * skyLightColor`. Cited HeightFogCommon.ush:174-180.
- [x] 3.5 Implement directional inscattering — cosine-power phase * UniformPhaseFunction(1/4PI), atmosphere post-transmittance sun illuminance, directional fog factor with separate start distance. Cited HeightFogCommon.ush:304-355.
- [x] 3.6 Implement sky pixel fog — uses farPlane distance for ray length when depth <= 0.0001 (reversed-Z far plane).
- [x] 3.7 Output: `float4(FogColor/fogOpacity, fogOpacity)` with SrcAlpha/InvSrcAlpha blend. Discard when fogOpacity < 0.001.

## 4. Rewrite CPU Constants (from UE5 FogRendering.cpp)

- [ ] 4.1 Implement InitFogConstants() — precompute RayOriginTerms = density * pow(2, -falloff * (observerH - fogH)). All units in cm.
- [ ] 4.2 Implement observer height clamp — min(camZ, fogH + 65536cm). Cite FogRendering.cpp:223-253.
- [ ] 4.3 Implement second fog layer — same formula, additive. Cite FogRendering.cpp:245-254.
- [ ] 4.4 Implement sky atmosphere post-transmittance sun illuminance — pass to shader for directional inscattering tint.
- [ ] 4.5 Populate HeightFogCB with all UE5-parity fields — verify field-by-field against FogRendering.cpp.

## 5. Integration

- [ ] 5.1 Wire fog pass in frame_lighting.cpp — same slot as before (after deferred lighting, before postprocess).
- [ ] 5.2 Verify pipeline blend state — SrcAlpha/InvSrcAlpha (keep existing).
- [ ] 5.3 Verify HeightFogComponent fields match UE5 defaults — density=0.02, falloff=0.2, maxOpacity=1.0, color=black (sky injection provides color).
- [ ] 5.4 Update pbr_test.cpp fog entity — use UE5 Basic scene exact values (density=0.0436, falloff=0.2, baseHeight from actor Z=-5000).

## 6. Visual Verification

- [ ] 6.1 Screenshot: only DirectionalLight — ground lit, no fog.
- [ ] 6.2 Screenshot: + SkyAtmosphere — sky appears, ground unchanged.
- [ ] 6.3 Screenshot: + SkyLight — ambient fill on shadow side of spheres.
- [ ] 6.4 Screenshot: + ExponentialHeightFog — horizon haze appears, ground stays visible at distance, sky-colored inscattering.
- [ ] 6.5 Compare all 4 screenshots against UE5 Basic scene reference.
- [ ] 6.6 Screenshot at distance — verify ground does NOT disappear.
- [ ] 6.7 Screenshot close-up — verify no blow-out or excessive fog.

## 7. Documentation

- [ ] 7.1 Update `docs/analysis/dataflow/sdf-gi-pipeline.md` — note fog rewrite.
- [ ] 7.2 Add `@source UE5` citations to every function in the new shader.
