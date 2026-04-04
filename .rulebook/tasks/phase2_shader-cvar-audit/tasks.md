## 1. Phase 1 — High Priority CVars (visual quality, user-facing)

### 1.1 SSGI Parameters
- [ ] 1.1.1 Add `r.SSGI.RayCount` (int, default=8, range [1,32]) — ssgi_trace_cs.hlsl:58
- [ ] 1.1.2 Add `r.SSGI.MaxMarchSteps` (int, default=32, range [8,128]) — ssgi_trace_cs.hlsl:59
- [ ] 1.1.3 Add `r.SSGI.StepSize` (float, default=0.02, range [0.005,0.1]) — ssgi_trace_cs.hlsl:60
- [ ] 1.1.4 Add `r.SSGI.Thickness` (float, default=0.5, range [0.01,2.0]) — ssgi_trace_cs.hlsl:61
- [ ] 1.1.5 Add `r.SSGI.TemporalAlpha` (float, default=0.05, range [0.01,0.5]) — ssgi_temporal_accum_cs.hlsl:40

### 1.2 Shadow Parameters
- [ ] 1.2.1 Add `r.Shadow.PCFPenumbraRadius` (float, default=2.0, range [0.5,8.0]) — shadow.hlsli:96
- [ ] 1.2.2 Add `r.Shadow.PCSSMinPenumbra` (float, default=0.5, range [0.1,4.0]) — shadow_pcss.hlsli:60
- [ ] 1.2.3 Add `r.Shadow.PCSSMaxPenumbra` (float, default=16.0, range [4.0,64.0]) — shadow_pcss.hlsli:60
- [ ] 1.2.4 Add `r.Shadow.ESMExponentConstant` (float, default=80.0, range [10,200]) — shadow_pcss.hlsli:72
- [ ] 1.2.5 Add `r.Shadow.ContactShadowSteps` (int, default=16, range [4,32]) — contact_shadows.hlsli:21
- [ ] 1.2.6 Add `r.Shadow.ContactShadowThickness` (float, default=0.005, range [0.001,0.05]) — contact_shadows.hlsli:23

### 1.3 Bloom & Exposure
- [ ] 1.3.1 Add `r.Bloom.Knee` (float, default=0.5, range [0.0,1.0]) — bloom_threshold_cs.hlsl:51
- [ ] 1.3.2 Add `r.Exposure.SpeedUp` (float, default=3.0, range [0.1,20.0]) — exposure_average_cs.hlsl:31
- [ ] 1.3.3 Add `r.Exposure.SpeedDown` (float, default=1.0, range [0.1,20.0]) — exposure_average_cs.hlsl:32
- [ ] 1.3.4 Add `r.Exposure.LowPercent` (float, default=10.0, range [1,50]) — exposure_average_cs.hlsl:33
- [ ] 1.3.5 Add `r.Exposure.HighPercent` (float, default=90.0, range [50,99]) — exposure_average_cs.hlsl:34
- [ ] 1.3.6 Add `r.Exposure.KeyValue` (float, default=0.18, range [0.01,1.0]) — exposure_average_cs.hlsl:77

### 1.4 God Rays
- [ ] 1.4.1 Add `r.GodRays.Samples` (int, default=16, range [4,64]) — god_rays_cs.hlsl:48
- [ ] 1.4.2 Add `r.GodRays.Density` (float, default=0.97, range [0.5,1.0]) — god_rays_cs.hlsl:70
- [ ] 1.4.3 Add `r.GodRays.Weight` (float, default=0.25, range [0.01,1.0]) — god_rays_cs.hlsl:71
- [ ] 1.4.4 Add `r.GodRays.Decay` (float, default=0.975, range [0.9,1.0]) — god_rays_cs.hlsl:72

### 1.5 Material
- [ ] 1.5.1 Add `r.Material.DielectricF0` (float, default=0.04, range [0.01,0.08]) — standard_material.hlsli:80
- [ ] 1.5.2 Add `r.Material.MinGGXAlpha` (float, default=0.0064, range [0.001,0.01]) — constants.hlsli:52

## 2. Phase 2 — Medium Priority CVars (fine-tuning, denoiser)

### 2.1 TAA Parameters
- [ ] 2.1.1 Add `r.TAA.VarianceClipGamma` (float, default=1.25, range [0.5,3.0]) — taa_resolve_cs.hlsl:310
- [ ] 2.1.2 Add `r.TAA.VelocityThreshold` (float, default=40.0, range [5,100]) — taa_resolve_cs.hlsl:325
- [ ] 2.1.3 Add `r.TAA.AntiFlickerMax` (float, default=0.3, range [0.0,1.0]) — taa_resolve_cs.hlsl:335

### 2.2 SSGI Denoiser
- [ ] 2.2.1 Add `r.SSGI.SpatialSigmaDepth` (float, default=1.0, range [0.1,10.0]) — ssgi_spatial_denoise_cs.hlsl:52
- [ ] 2.2.2 Add `r.SSGI.SpatialSigmaNormal` (float, default=128.0, range [8,512]) — ssgi_spatial_denoise_cs.hlsl:53
- [ ] 2.2.3 Add `r.SSGI.SpatialSigmaLuminance` (float, default=4.0, range [0.5,16.0]) — ssgi_spatial_denoise_cs.hlsl:54
- [ ] 2.2.4 Add `r.SSGI.VarianceClipGamma` (float, default=1.0, range [0.5,3.0]) — ssgi_temporal_accum_cs.hlsl:44
- [ ] 2.2.5 Add `r.SSGI.UpsampleDepthSigma` (float, default=0.5, range [0.1,2.0]) — ssgi_upsample_cs.hlsl:118
- [ ] 2.2.6 Add `r.SSGI.UpsampleNormalSigma` (float, default=32.0, range [4,128]) — ssgi_upsample_cs.hlsl:121

### 2.3 SVGF Denoiser
- [ ] 2.3.1 Add `r.SVGF.SigmaDepth` (float, default=1.0, range [0.1,10.0]) — svgf_atrous_cs.hlsl:71
- [ ] 2.3.2 Add `r.SVGF.SigmaNormal` (float, default=128.0, range [8,512]) — svgf_atrous_cs.hlsl:72
- [ ] 2.3.3 Add `r.SVGF.SigmaLuminance` (float, default=4.0, range [0.5,16.0]) — svgf_atrous_cs.hlsl:73
- [ ] 2.3.4 Add `r.SVGF.MomentsSigmaNormal` (float, default=32.0, range [4,128]) — svgf_filter_moments_cs.hlsl:93

### 2.4 Atmosphere
- [ ] 2.4.1 Add `r.Atmosphere.RayMarchSteps` (int, default=32, range [8,128]) — sky_atmosphere_ps.hlsl:54
- [ ] 2.4.2 Add `r.Atmosphere.MieAsymmetry` (float, default=0.8, range [0.0,0.99]) — sky_cubemap_cs.hlsl:32
- [ ] 2.4.3 Add `r.Atmosphere.GroundAlbedo` (float3, default=0.7,0.7,0.7) — sky_cubemap_cs.hlsl:35
- [ ] 2.4.4 Add `r.Atmosphere.CubemapSteps` (int, default=16, range [4,64]) — sky_cubemap_cs.hlsl:37

### 2.5 Clouds
- [ ] 2.5.1 Add `r.Cloud.DensityScale` (float, default=25.0, range [1,100]) — cloud_raymarch_cs.hlsl:91
- [ ] 2.5.2 Add `r.Cloud.MaxPrimarySteps` (int, default=64, range [16,256]) — cloud_raymarch_cs.hlsl:380
- [ ] 2.5.3 Add `r.Cloud.DetailErosion` (float, default=0.35, range [0.0,1.0]) — cloud_raymarch_cs.hlsl:302
- [ ] 2.5.4 Add `r.Cloud.DetailFrequencyScale` (float, default=4.0, range [1,16]) — cloud_raymarch_cs.hlsl:298
- [ ] 2.5.5 Add `r.Cloud.BeerPowderBlend` (float, default=0.5, range [0,1]) — cloud_raymarch_cs.hlsl:443
- [ ] 2.5.6 Add `r.Cloud.TransmittanceThreshold` (float, default=0.01, range [0.001,0.1]) — cloud_raymarch_cs.hlsl:407
- [ ] 2.5.7 Add `r.Cloud.MaxMarchDistanceKm` (float, default=50.0, range [10,200]) — cloud_raymarch_cs.hlsl:375

## 3. Phase 3 — Low Priority CVars (specialist tuning)

### 3.1 Cloud FBM
- [ ] 3.1.1 Add `r.Cloud.NoiseXYScale` (float, default=0.12, range [0.01,1.0]) — cloud_raymarch_cs.hlsl:269
- [ ] 3.1.2 Add `r.Cloud.NoiseZScale` (float, default=3.0, range [0.5,10.0]) — cloud_raymarch_cs.hlsl:273
- [ ] 3.1.3 Add `r.Cloud.ShapeOctaves` (int, default=4, range [1,6]) — cloud_raymarch_cs.hlsl:278
- [ ] 3.1.4 Add `r.Cloud.FBMDecay` (float, default=0.5, range [0.3,0.8]) — cloud_raymarch_cs.hlsl:240
- [ ] 3.1.5 Add `r.Cloud.FBMLacunarity` (float, default=2.0, range [1.5,4.0]) — cloud_raymarch_cs.hlsl:241
- [ ] 3.1.6 Add `r.Cloud.AerialPerspectiveDensity` (float, default=0.04, range [0,0.2]) — cloud_raymarch_cs.hlsl:461

### 3.2 Atmosphere Fine-Tuning
- [ ] 3.2.1 Add `r.Atmosphere.SunAngularRadius` (float, default=0.00935, range [0.001,0.05]) — sky_atmosphere_ps.hlsl:195
- [ ] 3.2.2 Add `r.Atmosphere.SunDiskEdgeFalloff` (float, default=0.0002, range [0.0001,0.01]) — sky_atmosphere_ps.hlsl:197
- [ ] 3.2.3 Add `r.Atmosphere.MieExtinctionRatio` (float, default=1.11, range [1.0,2.0]) — sky_atmosphere_ps.hlsl:175

### 3.3 Contact Shadows Fine-Tuning
- [ ] 3.3.1 Add `r.Shadow.ContactShadowMaxDistUV` (float, default=0.15, range [0.01,0.5]) — contact_shadows.hlsli:22
- [ ] 3.3.2 Add `r.Shadow.ContactShadowStepScale` (float, default=0.01, range [0.001,0.1]) — contact_shadows.hlsli:56

### 3.4 TAA Fine-Tuning
- [ ] 3.4.1 Add `r.TAA.SpatialWeightMultiplier` (float, default=2.29, range [1.0,5.0]) — taa_resolve_cs.hlsl:183
- [ ] 3.4.2 Add `r.TAA.ExposureScale` (float, default=1.0, range [0.1,10.0]) — taa_resolve_cs.hlsl:156

## 4. Infrastructure

### 4.1 CB Extension
- [ ] 4.1.1 Add new `PostProcessParams` struct to constant_buffers.h with all Phase 1 + 2 CVar fields (separate from PerFrameCB to avoid breaking existing layout).
- [ ] 4.1.2 Register `PostProcessParams` as b3 in tonemap/bloom/SSGI/TAA shaders.
- [ ] 4.1.3 Fill `PostProcessParams` from CVars in frame_constants.cpp every frame.

### 4.2 Per-Shader CB for Compute Shaders
- [ ] 4.2.1 For compute shaders with root constants (SSGI, SVGF, bloom, exposure), add CVar values to existing root constant blocks.
- [ ] 4.2.2 For pixel shaders reading from PerFrameCB, use PostProcessParams at b3.

## 5. Testing
- [ ] 5.1 Verify all default CVar values match current hardcoded values (regression-free).
- [ ] 5.2 Test extreme CVar values (min/max) — no crashes, no NaN, no visual corruption.
- [ ] 5.3 Run `--pass-dump` before and after — PSNR >= 40 dB (identical output with defaults).
- [ ] 5.4 Verify 95%+ coverage on new C++ code.

## 6. Documentation
- [ ] 6.1 Update `docs/specs/rendering/shader-library.md` with CVar list per shader.
- [ ] 6.2 Create `docs/guides/cvar-reference.md` — complete CVar reference with categories, defaults, ranges.
- [ ] 6.3 Add `@ref` comments to each CVar declaration citing the published technique.
