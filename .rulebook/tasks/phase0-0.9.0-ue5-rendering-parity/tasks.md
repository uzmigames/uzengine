## 1. Critical Fixes — Immediate Visual Impact

- [ ] 1.1 **Fix Mie Absorption coefficient** — `runtime/engine/ecs/environment/sky_atmosphere_component.h:82`: change `4.4e-3f` to `4.44e-4f`. UE5 ref: `SkyAtmosphereComponent.cpp:114`. Current value is 10x too large, causing excessive atmospheric haze and amber tint.
- [ ] 1.2 **Implement Burley Diffuse BRDF** — `runtime/shaders/runtime/include/pbr_common.hlsli:181`: replace `albedo / PI` (Lambertian) with Disney Burley diffuse model. UE5 ref: `BRDF.ush:177-189`. Adds roughness-dependent retroreflection at grazing angles. Formula: `(1/PI) * (1 + FD90*pow(1-NdotL,5)) * (1 + FD90*pow(1-NdotV,5))` where `FD90 = 0.5 + 2*roughness*VdotH^2`.
- [ ] 1.3 **Fix Directional Light default intensity** — `runtime/engine/ecs/lighting/light_component.h:41`: change default from `1.0f` to `10.0f`. UE5 ref: `DirectionalLightComponent.cpp:1004`. Update PBR demo to set intensity explicitly if needed to avoid over-bright.
- [ ] 1.4 **Update PBR demo light/sky params** — `runtime/demos/pbr_test/pbr_test.cpp`: set `skyLightIntensity`, `skyLightMinAmbient`, sun intensity to match UE5 default outdoor scene values. Capture before/after screenshot.

## 2. PBR BRDF Fixes — UE5 Formula Parity

- [ ] 2.1 **Implement SmithJointApprox geometry function** — `runtime/shaders/runtime/include/pbr_common.hlsli`: replace separate `GeometrySchlickGGX` with UE5's `Vis_SmithJointApprox` (BRDF.ush:346-352). Formula: `float a = Roughness; float Vis_SmithV = NoL * (NoV * (1-a) + a); float Vis_SmithL = NoV * (NoL * (1-a) + a); return 0.5 / (Vis_SmithV + Vis_SmithL)`.
- [ ] 2.2 **Fix k-remapping for IBL vs direct** — `runtime/shaders/runtime/include/pbr_common.hlsli:102` and `deferred_lighting_ps.hlsl`: use `k = (roughness+1)^2/8` for direct lighting, `k = roughness^2/2` for IBL. UE5 ref: BRDF.ush:329. Currently hardcodes direct-light k for both paths.
- [ ] 2.3 **Implement multi-scatter energy compensation** — in `deferred_lighting_ps.hlsl` IBL section: add Karis 2013 multi-scatter approximation `Ems = 1 - (scale + bias)` where scale/bias from BRDF LUT. Multiply IBL specular by `(1 + F0 * Ems / (1 - Ems))`. UE5 ref: `ReflectionEnvironmentShared.ush`.
- [ ] 2.4 **Verify MIN_ROUGHNESS and clamp** — ensure `max(roughness, 0.04)` applied before all BRDF evaluations. UE5 ref: BRDF.ush:20.

## 3. Tonemap — UE5 ACES FilmToneMap

- [ ] 3.1 **Implement UE5 FilmToneMap** — create `runtime/shaders/runtime/include/film_tonemap.hlsli`. Port UE5's `FilmToneMap` from `ACES.ush`/`PostProcessCombineLUTs.usf`. Includes sRGB→AP1 color space transform, custom curve with 5 artist parameters.
- [ ] 3.2 **Add artist parameters to PerFrameCB** — `FilmSlope` (default 0.88), `FilmToe` (default 0.55), `FilmShoulder` (default 0.26), `FilmBlackClip` (default 0.0), `FilmWhiteClip` (default 0.04). UE5 ref: PostProcessSettings.h.
- [ ] 3.3 **Replace Narkowicz fit in deferred_lighting_ps.hlsl** — swap `ACESFilm()` call with `FilmToneMap()` using the new parameters. Keep Narkowicz as `#ifdef UZ_FAST_TONEMAP` fallback.
- [ ] 3.4 **Replace Narkowicz fit in mesh_ps.hlsl** — same change for forward PBR path.

## 4. Screen-Space Ambient Occlusion (GTAO)

- [ ] 4.1 **Implement GTAO compute shader** — create `runtime/shaders/runtime/postprocess/gtao_cs.hlsl`. Ground Truth Ambient Occlusion per Jimenez 2016. Input: depth buffer + normal buffer. Output: R8_UNORM AO texture. UE5 ref: `PostProcessAmbientOcclusion.usf`.
- [ ] 4.2 **Implement GTAO spatial filter** — create `runtime/shaders/runtime/postprocess/gtao_filter_cs.hlsl`. 4x4 bilateral blur to remove noise while preserving edges.
- [ ] 4.3 **Create GTAO render graph pass** — register compute pass in scene_renderer.cpp after GBuffer, before lighting. Dispatch at half or full resolution.
- [ ] 4.4 **Integrate AO into deferred lighting** — multiply ambient IBL contribution by AO texture value. Bind AO texture at dedicated register slot.
- [ ] 4.5 **Add AO configuration** — `GTAOConfig` struct: radius (200cm default), thickness (500cm), angleBias (35°), quality (low/medium/high step counts).

## 5. Bloom Post-Process

- [ ] 5.1 **Implement bloom threshold pass** — create `runtime/shaders/runtime/postprocess/bloom_threshold_cs.hlsl`. Extract pixels above threshold (default 1.0 linear HDR). Output to half-res texture.
- [ ] 5.2 **Implement bloom downsample chain** — 6-level progressive downsample with 13-tap Karis average (anti-flicker). UE5 ref: `PostProcessBloom.usf`.
- [ ] 5.3 **Implement bloom upsample chain** — 6-level progressive upsample with bilinear tent filter + additive blend.
- [ ] 5.4 **Composite bloom** — add bloom result to scene color before tonemap. Intensity parameter (default 0.675). UE5 ref: `PostProcessCombineLUTs.usf`.
- [ ] 5.5 **Add bloom to PBR demo** — enable bloom with UE5 default parameters. Verify specular highlights bloom correctly.

## 6. Shadow Quality Fixes

- [ ] 6.1 **PCF adaptive radius per cascade** — in `shadow_common.hlsli`: scale PCF filter radius by cascade index. Near cascades: tight kernel (2-4 texels). Far cascades: wider kernel (8-16 texels). UE5 ref: `ShadowFilteringCommon.ush`.
- [ ] 6.2 **Shadow bias per cascade resolution** — in `shadow_renderer.cpp`: compute `depthBias = baseDepthBias * cascadeResolution / 2048.0f`. UE5 ref: DirectionalLightComponent.cpp bias formula.
- [ ] 6.3 **Verify shadow sharpen + overblur correction active** — check that `saturate((S-0.5)*sharpen+0.5)` and `S*S` are applied in shadow_common.hlsli. Already implemented but verify with debug grid.

## 7. Visual Verification

- [ ] 7.1 **Before/after screenshots** — capture PBR demo DX12 before and after ALL fixes applied. Store in `.screenshots/parity_before.png` and `.screenshots/parity_after.png`.
- [ ] 7.2 **UE5 reference screenshot** — capture equivalent UE5 scene (sphere on checkerboard, default directional light + sky light + sky atmosphere + height fog) for side-by-side comparison.
- [ ] 7.3 **3-backend verification** — confirm DX12, DX11, Vulkan render identically after all fixes.
- [ ] 7.4 **Shadow debug grid comparison** — verify shadow quality improvement on all 3 backends.

## 8. Testing

- [ ] 8.1 **Unit tests for Burley diffuse** — verify output matches UE5 reference values for specific NdotL/NdotV/roughness combinations.
- [ ] 8.2 **Unit tests for SmithJointApprox** — verify output matches UE5 reference values.
- [ ] 8.3 **Unit tests for FilmToneMap** — verify curve matches UE5 for specific input values with default params.
- [ ] 8.4 **Build passes on all 3 backends** — `cd runtime && zig build` clean with zero warnings.
- [ ] 8.5 **56+ shader parameter tests still pass** — no regressions from CB layout changes.

## 9. Documentation

- [ ] 9.1 **Update CHANGELOG** — document all parity fixes with UE5 citations.
- [ ] 9.2 **Update rendering parity analysis** — mark fixed items in `.claude/analysis/ue5-uzengine-rendering-parity-comparison.md`.
- [ ] 9.3 **Document artist tonemap controls** — add FilmSlope/FilmToe/FilmShoulder to shader authoring guide.
