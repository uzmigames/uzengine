<!-- doc-meta
title: Volumetric Clouds UE5 Parity — Task Checklist
phase: 2
priority: P1
depends-on: phase0-3.1.0-volumetric-clouds-rewrite
created: 2026-04-01
-->

# Volumetric Clouds — UE5 Feature Parity Tasks

Prerequisite: Phase 0 (phase0-3.1.0-volumetric-clouds-rewrite) items 0.1–0.11 complete.
Blocked items from Phase 0 (0.12, 0.B1–B4) are addressed in Phase A below.

---

## Phase A: Fix Defaults + Missing Component Properties (UE5 Constructor Parity)

Source: `W:/UE_5.6_Source/.../VolumetricCloudComponent.cpp` constructor defaults.

- [ ] A.1 Fix `layerBottomAltitudeKm` default: 7.0 → **5.0** km (UE5: `LayerBottomAltitude = 5.0f`).
- [ ] A.2 Fix `layerHeightKm` default: 6.0 → **10.0** km (UE5: `LayerHeight = 10.0f`).
- [ ] A.3 Fix `skyLightCloudBottomOcclusion` default: 0.2 → **0.5** (UE5: `SkyLightCloudBottomOcclusion = 0.5f`).
- [ ] A.4 Fix `viewSampleCountScale` default: 0.5 → **1.0** (UE5: `ViewSampleCountScale = 1.0f`).
- [ ] A.5 Fix `reflectionViewSampleCountScale` default: 0.1 → **1.0** (UE5: `ReflectionViewSampleCountScaleValue = 1.0f`).
- [ ] A.6 Fix `shadowViewSampleCountScale` default: 0.5 → **1.0** (UE5: `ShadowViewSampleCountScale = 1.0f`).
- [ ] A.7 Fix `shadowReflectionViewSampleCountScale` default: 0.1 → **1.0** (UE5: `ShadowReflectionViewSampleCountScaleValue = 1.0f`).
- [ ] A.8 Fix `shadowTracingDistance` default: 10.0 → **15.0** km (UE5: `ShadowTracingDistance = 15.0f`).
- [ ] A.9 Fix `stopTracingTransmittanceThreshold` default: 0.01 → **0.005** (UE5: `StopTracingTransmittanceThreshold = 0.005f`).
- [ ] A.10 Fix `aerialPerspectiveRayleighScatteringFadeDistance` default: 10.0 → **0.0** (UE5: all 4 AP distances default 0.0).
- [ ] A.11 Fix `aerialPerspectiveMieScatteringFadeDistance` default: 10.0 → **0.0** (UE5: same).
- [ ] A.12 Add **`planetRadius`** property: `float planetRadius = 6360.0f` (km). Used when no SkyAtmosphere is present.
- [ ] A.13 Add **`groundAlbedo`** property: `Color groundAlbedo = {170, 170, 170}` (≈0.4 linear). Ground albedo for bottom lighting.
- [ ] A.14 Add **`usePerSampleAtmosphericLightTransmittance`** property: `bool = false`. Per-sample LUT lookup vs global transmittance.
- [ ] A.15 Add **`bVisibleInRealTimeSkyCaptures`** property: `bool = true`. Controls cloud visibility in sky light captures.
- [ ] A.16 Add **`bRenderInMainPass`** property: `bool = true`. Controls if clouds render in main pass.
- [ ] A.17 Update `cloud_test.uzscene` with corrected defaults.
- [ ] A.18 Update Lua API: add getters/setters for new properties (planetRadius, groundAlbedo, usePerSampleAtmosphericLightTransmittance, bVisibleInRealTimeSkyCaptures, bRenderInMainPass).
- [ ] A.19 Build + verify no visual regression with corrected defaults.

## Phase B: Precomputed Noise Textures (Unblock Phase 0 TDR)

Resolve Phase 0 blocker: runtime GPU noise bake causes TDR.
Strategy: precompute noise offline, load from disk.

- [ ] B.1 Implement `cloud_noise_shape_cs.hlsl`: Perlin-Worley hybrid 128^3 RGBA8. R=Perlin-Worley, G/B/A=Worley at increasing frequencies. @ref Schneider 2015 Table 1. Run as offline tool (uzcook) not runtime.
- [ ] B.2 Implement `cloud_noise_detail_cs.hlsl`: 32^3 RGBA8 high-frequency Worley. R/G/B=Worley at 3 frequencies, A=Worley-Perlin mix.
- [ ] B.3 Create `uzcook` CLI command: `uzcook --bake-cloud-noise --output content/atmosphere/`. Dispatches compute, reads back, saves as `.bin` (raw RGBA8).
- [ ] B.4 Update `CloudPass::LoadNoiseTextures()`: load both `shape.bin` (128^3) and `detail.bin` (32^3) from VFS/disk. Create 3D SRV textures.
- [ ] B.5 Update `cloud_raymarch_cs.hlsl`: replace procedural 4-octave FBM with 3D texture samples. Shape noise at base frequency, detail noise for erosion.
- [ ] B.6 Implement density remapping: `remap(shapeNoise.r, saturate(coverage), 1.0, 0.0, 1.0)` then erode with detail: `density -= detailNoise * detailAmount * (1 - heightFraction)`. @ref Schneider 2015 Section 5.
- [ ] B.7 Build + visual verification: compare procedural FBM vs precomputed noise quality. Precomputed should have more natural Worley cellular patterns.

## Phase C: Weather Map System

2D weather texture for spatially varying coverage, cloud type, and precipitation.

- [ ] C.1 Implement `cloud_weather_cs.hlsl`: 512x512 RGBA16F procedural weather map. R=coverage (Perlin fbm), G=cloud type (Perlin), B=precipitation/density modifier, A=reserved.
- [ ] C.2 Add `weatherMapTexture` path to VolumetricCloudsComponent (optional: if empty, use procedural; if set, load from disk).
- [ ] C.3 Add `weatherMapScale` property (default 1.0): world-space UV scale for weather sampling.
- [ ] C.4 Add `weatherMapOffset` property (Vec2, default 0,0): weather map UV offset (for wind scrolling).
- [ ] C.5 Update `cloud_raymarch_cs.hlsl`: sample weather map at world XY position. Use R channel for coverage, G for cloud type, B for density modifier.
- [ ] C.6 Wire wind velocity to weather map UV offset (weather moves with wind).
- [ ] C.7 Add Lua API: `set_clouds_weather_map_path`, `set_clouds_weather_map_scale`, `set_clouds_weather_map_offset`.
- [ ] C.8 Build + visual verification: verify spatially varying cloud coverage (dense in some areas, clear in others).

## Phase D: Ground Contribution (Bottom Lighting from Ground Albedo)

@ref UE5: GroundAlbedo property + Volumetric Advanced GroundContribution output.

- [ ] D.1 Add `groundContributionEnabled` property to VolumetricCloudsComponent (bool, default true).
- [ ] D.2 Pass `groundAlbedo` color to CloudCB constant buffer.
- [ ] D.3 In `cloud_raymarch_cs.hlsl`: compute ground bounce illumination at cloud bottom. `groundLight = sunColor * groundAlbedo * max(0, dot(sunDir, float3(0,0,1))) * (1/PI)`. Apply to samples in bottom 20% of cloud layer, attenuated by height fraction.
- [ ] D.4 Build + visual verification: cloud bottoms should have warm ground-bounce tint, especially at low sun angles.

## Phase E: Temporal Reconstruction (Quarter-Res + 16-Frame Accumulation)

Critical for performance. UE5 reconstructs a full frame over 16 frames using Bayer dithering.

- [ ] E.1 Enable the existing `cloud_temporal_cs.hlsl` by setting `temporalBlendFactor < 1.0` in `frame_atmosphere_passes.cpp` (start with 0.05).
- [ ] E.2 Create ping-pong history buffers in CloudPass: `m_temporalHistory[2]` (half-res RGBA16F). Swap each frame.
- [ ] E.3 Wire temporal pass between ray-march and composite: raymarch → temporal → composite.
- [ ] E.4 Implement 4x4 Bayer dither pattern: only trace 1/16 pixels per frame, reconstruct rest from history.
- [ ] E.5 Switch ray-march resolution from half-res to quarter-res (massive perf gain).
- [ ] E.6 Output cloud front depth from ray-march (transmittance-weighted average depth) for velocity-based reprojection.
- [ ] E.7 Add `temporalBlendFactor` to VolumetricCloudsComponent (default 0.05, range 0.0–1.0). 1.0 = no temporal (current behavior).
- [ ] E.8 Add `temporalUpscaleEnabled` property (bool, default true). When false, render at half-res without temporal.
- [ ] E.9 Build + visual verification: verify clean temporal convergence, no ghosting on camera rotation. Measure frame time improvement (target: >50% reduction vs half-res).

## Phase F: Cloud Shadow Maps (Ground Darkening + God Rays)

Critical feature: clouds cast shadows on the ground and scene geometry.
@ref UE5: `CloudShadowmapTranslatedWorldToLightClipMatrix`, per-light cascade system.

- [ ] F.1 Create `cloud_shadow_cs.hlsl`: orthographic ray-march from sun direction. Output R16F transmittance map (512x512 per cascade).
- [ ] F.2 Implement cascade frustum: single cascade covering `shadowDistance` km centered on camera XY, snapped to texel grid to prevent shimmer.
- [ ] F.3 Create `CloudShadowPass` class: owns shadow map texture + compute pipeline + temporal history.
- [ ] F.4 Add `cloudShadowEnabled` property (bool, default true).
- [ ] F.5 Add `cloudShadowResolution` property (uint32, default 512).
- [ ] F.6 Add `cloudShadowDistance` property (float, default 50.0 km).
- [ ] F.7 Add `cloudShadowStrength` property (float, default 1.0, range 0–1).
- [ ] F.8 Add `cloudShadowDepthBias` property (float, default 0.0).
- [ ] F.9 Implement `SampleCloudShadow(worldPosKm, sunDir)` HLSL function: project world position into shadow map UV, sample transmittance, return shadow factor.
- [ ] F.10 Integrate into `deferred_lighting_ps.hlsl`: multiply sun shadow by cloud shadow. `finalShadow = shadowMap * cloudShadow`.
- [ ] F.11 Integrate into `cloud_raymarch_cs.hlsl`: use cloud shadow map for improved self-shadowing (replace or augment current 6-step light march).
- [ ] F.12 Temporal filtering on cloud shadow map: variance clip history to reduce shimmer on moving clouds.
- [ ] F.13 Add Lua API: `set_clouds_shadow_enabled`, `set_clouds_shadow_resolution`, `set_clouds_shadow_distance`, `set_clouds_shadow_strength`.
- [ ] F.14 Build + visual verification: ground should darken under clouds, soft penumbra, no shimmer.

## Phase G: Cloud Sky AO (Ambient Occlusion from Clouds)

@ref UE5: `CloudSkyAO*` parameters in VolumetricCloudRendering.h. Clouds occlude ambient sky light on ground.

- [ ] G.1 Create `cloud_skyao_cs.hlsl`: orthographic ray-march looking straight up (zenith direction). Output R16F AO map (256x256).
- [ ] G.2 Add `cloudSkyAOEnabled` property (bool, default true).
- [ ] G.3 Add `cloudSkyAOStrength` property (float, default 1.0, range 0–1).
- [ ] G.4 Add `cloudSkyAOFarDepthKm` property (float, default 20.0 km).
- [ ] G.5 Integrate: multiply ambient/sky light by `(1 - cloudSkyAO * strength)` in deferred lighting.
- [ ] G.6 Build + visual verification: areas under thick clouds should have reduced ambient light.

## Phase H: Dual Atmospheric Light Support (Sun + Moon)

@ref UE5: all shadow/scatter arrays are `[2]` for two atmospheric lights.

- [ ] H.1 Extend CloudCB to carry two light directions, colors, and shadow matrices.
- [ ] H.2 Update `cloud_raymarch_cs.hlsl`: march toward both lights, accumulate in-scattering from each with separate phase function evaluation.
- [ ] H.3 Update cloud shadow system: generate shadow maps for both lights.
- [ ] H.4 Add `AtmosphericLightCloudScatteredLuminanceScale[2]` to component (per-light scatter intensity override).
- [ ] H.5 Build + visual verification: night scene with moon illuminating clouds + sun below horizon.

## Phase I: Per-Sample Atmospheric Light Transmittance

@ref UE5: `bUsePerSampleAtmosphericLightTransmittance` — sample transmittance LUT per ray-march step instead of using global light transmittance.

- [ ] I.1 When `usePerSampleAtmosphericLightTransmittance=true`, sample the Bruneton transmittance LUT at each ray-march step's altitude.
- [ ] I.2 Bind transmittance LUT SRV to cloud shader (reuse existing sky atmosphere LUT).
- [ ] I.3 Apply per-sample transmittance to sun color before computing in-scattering.
- [ ] I.4 Build + visual verification: clouds at different altitudes should have different sun color tinting (higher = less atmospheric absorption).

## Phase J: Separated Aerial Perspective (Mie vs Rayleigh)

@ref UE5: `VolumetricCloudWantsSeparatedAtmosphereMieRayLeigh()`. Separate control over haze (Mie) and sky color (Rayleigh) fade-in on clouds.

- [ ] J.1 Implement separated AP in `cloud_raymarch_cs.hlsl`: compute Rayleigh and Mie inscattering separately using start/fade distance pairs.
- [ ] J.2 Apply Rayleigh AP with `AerialPerspectiveRayleighScatteringStartDistance` and `FadeDistance`.
- [ ] J.3 Apply Mie AP with `AerialPerspectiveMieScatteringStartDistance` and `FadeDistance`.
- [ ] J.4 Verify: Rayleigh gives blue-ish sky tint at distance, Mie gives white haze. Different start/fade distances allow artistic control.
- [ ] J.5 Build + visual verification: distant clouds show blue tint (Rayleigh) independent of haze (Mie).

## Phase K: Conservative Density (Two-Step Evaluation)

@ref UE5: `ConservativeDensity` input on Volumetric Advanced Material Output. Skip expensive density evaluation when conservative check returns 0.

- [ ] K.1 Implement two-step density evaluation in `cloud_raymarch_cs.hlsl`: first do cheap weather-map + height-gradient check. If result is 0, skip the expensive 3D noise sampling.
- [ ] K.2 Add `conservativeDensityEnabled` property (bool, default true).
- [ ] K.3 Measure performance improvement: count how many samples skip the full noise eval.
- [ ] K.4 Build + visual verification: no visual change (optimization only), measure frame time improvement.

## Phase L: Cloud Emissive (Self-Illumination)

@ref UE5: Emissive Color output on cloud material. Allows clouds to glow (sunset rim lighting, lightning illumination).

- [ ] L.1 Add `emissiveColor` property to VolumetricCloudsComponent (Color, default black = no emission).
- [ ] L.2 Add `emissiveIntensity` property (float, default 0.0).
- [ ] L.3 In `cloud_raymarch_cs.hlsl`: add emissive contribution per sample: `radiance += emissive * density * stepLength`.
- [ ] L.4 Build + visual verification: set warm orange emissive at sunset, verify rim glow on cloud edges.

## Phase M: Multi-Scatter Octaves (Configurable Count)

@ref Wrenninge 2015, Oz & Kulla 2017. UE5 allows configurable octave count in material.

- [ ] M.1 Add `multiScatterOctaves` property (uint32, default 8, range 1–12).
- [ ] M.2 Add `multiScatterContribution` property (float, default 0.5). Attenuation per octave.
- [ ] M.3 Add `multiScatterOcclusion` property (float, default 0.5). Occlusion per octave.
- [ ] M.4 Add `multiScatterEccentricity` property (float, default 0.5). Phase eccentricity per octave.
- [ ] M.5 Replace current fixed 3-bounce approximation with configurable octave loop.
- [ ] M.6 Build + visual verification: compare 3 vs 8 octaves — 8 should have brighter, more realistic cloud interiors.

## Phase N: Volumetric Fog Interaction

@ref UE5: `ShouldVolumetricCloudsApplyFogDuringReconstruction()`. Apply height fog to cloud pixels during temporal reconstruction.

- [ ] N.1 During cloud composite or temporal resolve, apply height fog to cloud pixels based on their depth.
- [ ] N.2 Add `applyFogDuringReconstruction` property (bool, default true).
- [ ] N.3 Build + visual verification: clouds blend naturally with height fog at horizon.

## Phase O: Cloud Rendering in Reflections

@ref UE5: separate sample count scale for reflection rendering.

- [ ] O.1 When rendering reflection probes / sky capture, dispatch a lower-quality cloud pass using `reflectionViewSampleCountScale`.
- [ ] O.2 When rendering SSR, include cloud composite in the SSR input.
- [ ] O.3 Build + visual verification: clouds visible in reflective surfaces (water, metallic).

## Phase P: Min/Max Depth Optimization

@ref UE5: `ShouldVolumetricCloudTraceWithMinMaxDepth()`. Use HiZ or min/max depth buffer to skip tracing where geometry is closer than cloud layer.

- [ ] P.1 Bind HiZ pyramid (already exists for GPU culling) to cloud ray-march shader.
- [ ] P.2 Per-pixel: compare closest scene depth to cloud layer entry distance. If scene is closer, skip cloud tracing entirely.
- [ ] P.3 Measure performance improvement on scenes with significant geometry coverage.
- [ ] P.4 Build + visual verification: no visual change, frame time improvement on complex scenes.

## Phase Q: Async Compute

@ref UE5: `bAsyncCompute` in FCloudRenderContext. Cloud tracing on async compute queue.

- [ ] Q.1 Register cloud ray-march as async-compute-capable pass in render graph.
- [ ] Q.2 Ensure proper fences between cloud compute and composite.
- [ ] Q.3 Measure overlap with shadow map rendering or GBuffer fill.
- [ ] Q.4 Build + verify: no corruption, measure frame time improvement from overlap.

## Phase R: Debug View Modes

@ref UE5: `GetVolumetricCloudDebugViewMode()`, conservative density visualization.

- [ ] R.1 Add `--vis cloud.raymarch` target to diagnostic system (show raw ray-march output).
- [ ] R.2 Add `--vis cloud.shadow` target (show cloud shadow map).
- [ ] R.3 Add `--vis cloud.skyao` target (show cloud sky AO map).
- [ ] R.4 Add `--vis cloud.weather` target (show weather map).
- [ ] R.5 Add `--vis cloud.density` mode: visualize conservative density (where tracing is skipped).

## Phase S: Blue Noise Dithering

@ref UE5: `ShouldRenderVolumetricCloudWithBlueNoise_GameThread()`. STBN blue noise for ray offset.

- [ ] S.1 Generate or embed STBN 3D blue noise texture: 128x128x64 R8_UNORM.
- [ ] S.2 Add blue noise jitter to ray start offset: `t += stepSize * blueNoise[pixel % 128][frame % 64]`.
- [ ] S.3 Reduces banding artifacts, especially at low sample counts.
- [ ] S.4 Build + visual verification: compare banding before/after blue noise. Should be significantly smoother.

## Testing & Validation

- [ ] T.1 Visual test: screenshot at default camera with corrected UE5 defaults.
- [ ] T.2 Visual test: screenshot from above cloud layer.
- [ ] T.3 Visual test: screenshot at sunset (phase function glow + ground contribution).
- [ ] T.4 Visual test: screenshot with cloud shadows on ground geometry.
- [ ] T.5 Visual test: screenshot with dual lights (sun + moon).
- [ ] T.6 Visual test: clouds visible in water/metallic reflections.
- [ ] T.7 Performance test: frame time with temporal upscale (quarter-res + 16-frame) vs half-res.
- [ ] T.8 Performance test: frame time with conservative density optimization.
- [ ] T.9 Performance test: frame time with async compute overlap.
- [ ] T.10 Temporal stability test: 60-frame capture, verify no ghosting or flickering.
- [ ] T.11 Update `cloud_test.uzscene` as comprehensive cloud test scene.
- [ ] T.12 Update Lua API documentation with all new properties.
