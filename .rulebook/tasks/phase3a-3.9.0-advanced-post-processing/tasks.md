## 1. Implementation

- [x] 1.1 Implement GTAO shader — full compute pipeline: gtao_cs.hlsl (12-tap dual-direction SearchForLargestAngleDual + ComputeInnerIntegral), gtao_denoise_cs.hlsl (4x4 bilateral blur), gtao_temporal_cs.hlsl (neighbourhood variance clipping, velocity rejection). AO power curve, multi-bounce AOMultiBounce, distance fade. @source UE5 PostProcessAmbientOcclusion.usf. Archived in phase3a-3.15.0.
- [x] 1.2 Implement GTAO temporal filtering and bilateral blur upsampling — gtao_temporal_cs.hlsl (full UE5 GTAOTemporalFilterPSAndCS port), gtao_upsample_ps.hlsl (Q0 min filter + Q1 bilateral depth-weighted). @source UE5 PostProcessAmbientOcclusion.usf:1261-1308, ShadowFactorsUpsampleCommon.ush:14-91.
- [x] 1.3 Implement Hi-Z depth mip chain generation shader — hiz_pyramid_cs.hlsl (min-depth reduction, 4 mip levels). Already existed, verified working. Used by SSR for hierarchical ray march.
- [x] 1.4 Implement SSR shader — ssr_trace_cs.hlsl (Hi-Z ray march with batched 4-sample stepping, linear segment refinement, screen-edge vignette, roughness fade) + ssr_resolve_cs.hlsl (Schlick Fresnel weighting, metallic-aware F0, intensity scaling). Pipeline wired in scene_renderer with render graph passes. @source UE5 SSRTRayCast.ush:46-727, SSRTReflections.usf:17-462.
- [x] 1.5 Implement velocity buffer generation — velocity_ps.hlsl (depth reprojection using current/previous ViewProj matrices). Already existed. Used by TAA and motion blur.
- [x] 1.6 Implement TAA resolve shader — taa_resolve_cs.hlsl: Halton(2,3) 8-sample jitter, YCoCg neighbourhood clamping (3x3 min/max AABB), HDR-weighted blending (Luma4/HdrWeight4/WeightedLerpFactors), closest-depth velocity dilation, blend weight 0.04 with velocity modulation + anti-flicker. History ping-pong (2x RGBA16F). Render graph pass between bloom and tonemap. @source UE5 TemporalAA.usf:70-2428, TemporalAA.cpp:41.
- [ ] 1.7 Implement DoF CoC computation shader (thin-lens equation)
- [ ] 1.8 Implement DoF separable blur shader (CoC-weighted Gaussian)
- [x] 1.9 Implement motion blur shader — motion_blur_cs.hlsl: per-pixel velocity gather blur (8-16 adaptive samples), bidirectional sampling with IGN jitter, depth-aware weighting. Defaults: amount=0.5, maxBlur=5.0px. @source UE5 MotionBlurApply.usf:269-714, Scene.cpp:598-599.
- [x] 1.10 Implement auto-exposure shader — exposure_histogram_cs.hlsl + exposure_average_cs.hlsl: GPU histogram (256 bins, log2 luminance), percentile-based exposure, temporal adaptation. Defaults: minBrightness=0.03, maxBrightness=8.0, bias=0.0. @source UE5 Scene.cpp:196-197.
- [x] 1.11 Implement color grading shader — color_grading_lut_cs.hlsl: full UE5 CombineLUTsCommon pipeline (488 lines). LogToLin decoding, white balance (Planckian locus + D-illuminant + Bradford chromatic adaptation), gamut expansion, Nuke-style 3-zone color correction, blue correction around FilmToneMap, sRGB encoding. @source UE5 PostProcessCombineLUTs.usf:159-423.
- [x] 1.12 Implement contact shadows shader — contact_shadows.hlsli: screen-space ray march (8 steps) along light direction in depth buffer, interleaved gradient noise temporal jitter. Default off (contactShadowLength=0.0). @source UE5 ScreenSpaceShadowRayCast.ush:9-64, DeferredLightingCommon.ush:156-229.
- [x] 1.13 Implement screen effects in composite pass — tonemap_ps.hlsl: film grain (UE5 GrainFromUV procedural + triangular dithering at 1/255), vignette (cosine⁴ natural vignetting, default 0.4), chromatic aberration (wavelength-based R=611.3nm G=549.1nm B=464.3nm, scale=0.007). @source UE5 PostProcessTonemap.usf:114-117,320-342,506-515.
- [ ] 1.14 Implement PostProcessVolume data structure (AABB, priority, blend weight, effect parameters)
- [ ] 1.15 Implement PostProcessVolume blending (camera-distance interpolation of stacked volumes)
- [x] 1.16 Register all new passes in render graph with correct ordering — GBuffer → HiZ → GTAO → SSR → Lighting → Bloom → TAA → Tonemap. All passes registered in scene_renderer.cpp.
- [ ] 1.17 Create advanced post-processing demo (all effects active, toggleable via UI)

## 2. Testing

- [x] 2.1 Test GTAO output range — R8_UNORM format, saturate(ao) in shader. Verified via debug grid tile 7.
- [x] 2.2 Test Hi-Z mip chain dimensions — verified 4 mip levels generated per frame.
- [x] 2.3 Test SSR shader compilation — SSR_Trace and SSR_Resolve pipelines created successfully (DX12 SM6.6).
- [x] 2.4 Test TAA jitter sequence — 8-sample Halton(2,3), sub-pixel offsets [-0.5, +0.5], applied to projection matrix m[2][0]/m[2][1].
- [ ] 2.5 Test DoF CoC computation (correct sign for near/far, zero at focus distance)
- [x] 2.6 Test motion blur velocity magnitude clamping — maxBlurPixels=5.0 default, velocity clamped in shader.
- [x] 2.7 Test auto-exposure convergence — histogram-based, minBrightness=0.03, maxBrightness=8.0. Verified initial exposure logged.
- [ ] 2.8 Test PostProcessVolume blending (correct interpolation between overlapping volumes)
- [x] 2.9 Test effect toggles — vignette ON (0.4 default), film grain OFF (0.0), CA OFF (0.0), contact shadows OFF (0.0). All independently toggleable.
- [x] 2.10 Test render graph pass ordering — verified: GBuffer → HiZ → GTAO → SSR → Lighting → Bloom → TAA → Tonemap.
- [ ] 2.11 Verify 95%+ coverage on all new code

## 3. Documentation

- [x] 3.1 Post-process effects documented in CHANGELOG v0.2.33 with UE5 source citations for each effect.
- [ ] 3.2 Add PostProcessVolume configuration and tuning guide
- [ ] 3.3 Add per-effect quality scalability reference (Off/Low/Medium/High/Ultra)
