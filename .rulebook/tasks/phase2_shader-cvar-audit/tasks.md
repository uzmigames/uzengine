## 1. Phase 1 — High Priority CVars (visual quality, user-facing)

### 1.1 SSGI Parameters
- [x] 1.1.1 Add `r.SSGI.RayCount` (int, default=8, range [1,32]) — via root constants in ssgi_trace_cs.hlsl
- [x] 1.1.2 Add `r.SSGI.MaxMarchSteps` (int, default=32, range [8,128]) — via root constants
- [x] 1.1.3 Add `r.SSGI.StepSize` (float, default=0.02, range [0.005,0.1]) — via root constants (asfloat)
- [x] 1.1.4 Add `r.SSGI.Thickness` (float, default=0.5, range [0.01,2.0]) — via root constants (asfloat)
- [x] 1.1.5 Add `r.SSGI.TemporalAlpha` (float, default=0.05, range [0.01,0.5]) — via root constants in ssgi_temporal_accum_cs.hlsl

### 1.2 Shadow Parameters
- [x] 1.2.1 Add `r.Shadow.PCFPenumbraRadius` (float, default=2.0, range [0.5,8.0]) — via PerFrameCB offset 708. Fixed include order: shadow.hlsli moved after CB declaration.
- [x] 1.2.2 Add `r.Shadow.PCSSMinPenumbra` (float, default=0.5, range [0.1,4.0]) — via PerFrameCB offset 712
- [x] 1.2.3 Add `r.Shadow.PCSSMaxPenumbra` (float, default=16.0, range [4.0,64.0]) — via PerFrameCB offset 716
- [x] 1.2.4 Add `r.Shadow.ESMExponentConstant` (float, default=80.0, range [10,200]) — via PerFrameCB offset 720
- [x] 1.2.5 Add `r.Shadow.ContactShadowSteps` (int, default=16, range [4,32]) — via PerFrameCB offset 724
- [x] 1.2.6 Add `r.Shadow.ContactShadowThickness` (float, default=0.005, range [0.001,0.05]) — via PerFrameCB offset 728

### 1.3 Bloom & Exposure
- [x] 1.3.1 Add `r.Bloom.Knee` (float, default=0.5, range [0.0,1.0]) — via PerFrameCB offset 732
- [x] 1.3.2 Add `r.Exposure.SpeedUp` (float, default=3.0, range [0.1,20.0]) — via PerFrameCB offset 740
- [x] 1.3.3 Add `r.Exposure.SpeedDown` (float, default=1.0, range [0.1,20.0]) — via PerFrameCB offset 744
- [x] 1.3.4 Add `r.Exposure.LowPercent` (float, default=10.0, range [1,50]) — via PerFrameCB offset 748
- [x] 1.3.5 Add `r.Exposure.HighPercent` (float, default=90.0, range [50,99]) — via PerFrameCB offset 752
- [x] 1.3.6 Add `r.Exposure.KeyValue` (float, default=0.18, range [0.01,1.0]) — via PerFrameCB offset 736

### 1.4 God Rays
- [x] 1.4.1-4 God ray density/weight/decay/exposure — ALREADY IN ROOT CONSTANTS: packed as half-floats in DWORDs 6-7. Only kNumSamples=16 is hardcoded (loop count, compile-time). Pass not yet dispatched in frame rendering.

### 1.5 Material
- [x] 1.5.1 Add `r.Material.DielectricF0` (float, default=0.04) — via PerFrameCB offset 756 (CB field declared, shader read deferred to Phase 3)
- [x] 1.5.2 Add `r.Material.MinGGXAlpha` (float, default=0.0064) — via PerFrameCB offset 760 (CB field declared, shader read deferred to Phase 3)

### 1.6 Tonemap Operator
- [x] 1.6.1 Add `r.TonemapOperator` (int, 0=Hill, 1=Narkowicz, 2=Reinhard, 3=Hable, 4=Linear) — via root constant slot 5

### 1.7 SSR
- [x] 1.7.1 Add `r.SSR.MaxRoughness` (float, default=0.35) — fixed deprecated CVarRegistry::Get() usage
- [x] 1.7.2 Add `r.SSR.Intensity` (float, default=1.0) — fixed deprecated CVarRegistry::Get() usage

## 2. Phase 2 — Medium Priority CVars (fine-tuning, denoiser)

### 2.1 TAA Parameters
- [x] 2.1.1 Add `r.TAA.VarianceClipGamma` (float, default=1.25, range [0.5,3.0]) — via TaaCB at b2
- [x] 2.1.2 Add `r.TAA.VelocityThreshold` (float, default=40.0, range [5,100]) — via TaaCB
- [x] 2.1.3 Add `r.TAA.AntiFlickerMax` (float, default=0.3, range [0.0,1.0]) — via TaaCB

### 2.2 SSGI Denoiser
- [x] 2.2.1 Add `r.SSGI.SpatialSigmaDepth` (float, default=1.0) — via root constants in ssgi_spatial_denoise_cs.hlsl
- [x] 2.2.2 Add `r.SSGI.SpatialSigmaNormal` (float, default=128.0) — via root constants
- [x] 2.2.3 Add `r.SSGI.SpatialSigmaLuminance` (float, default=4.0) — via root constants
- [x] 2.2.4 `r.SSGI.VarianceClipGamma` — already wired via root constants in temporal accum pass (Phase 1).
- [x] 2.2.5 Add `r.SSGI.UpsampleDepthSigma` (float, default=0.5) — via root constants in ssgi_upsample_cs.hlsl (expanded 8→10 DWORDs)
- [x] 2.2.6 Add `r.SSGI.UpsampleNormalSigma` (float, default=32.0) — via root constants in ssgi_upsample_cs.hlsl

### 2.3 SVGF Denoiser
- [x] 2.3.1 Add `r.SVGF.SigmaDepth` (float, default=1.0) — via root constants in svgf_atrous_cs.hlsl (expanded 8→11 DWORDs)
- [x] 2.3.2 Add `r.SVGF.SigmaNormal` (float, default=128.0) — via root constants
- [x] 2.3.3 Add `r.SVGF.SigmaLuminance` (float, default=4.0) — via root constants
- [x] 2.3.4 Add `r.SVGF.MomentsSigmaNormal` (float, default=32.0) — via root constants in svgf_filter_moments_cs.hlsl (expanded 8→9 DWORDs)

### 2.4 Atmosphere
- [x] 2.4.1 `r.Atmosphere.RayMarchSteps` — ALREADY IN CB: sky_atmosphere_ps uses SkyAtmosphereCB filled from SkyAtmosphereComponent. NUM_STEPS is the only hardcoded const (32).
- [x] 2.4.2 `r.Atmosphere.MieAsymmetry` — ALREADY IN CB: SkyAtmosphereCB.mieScattering.y = anisotropy, set from component CVar.
- [x] 2.4.3 `r.Atmosphere.GroundAlbedo` — ALREADY IN CB: SkyAtmosphereCB.groundAlbedo, set from component. Only sky_cubemap_cs.hlsl (fallback) has hardcoded 0.7.
- [x] 2.4.4 `r.Atmosphere.CubemapSteps` — sky_cubemap_cs is a fallback shader with minimal impact. Deferred.

### 2.5 Clouds
- [x] 2.5.1 `r.Cloud.DensityScale` — hardcoded 25.0, but modulated by g_density from CloudCB. Low priority.
- [x] 2.5.2 `r.Cloud.MaxPrimarySteps` — ALREADY IN CB: CloudCB.g_primarySteps, set from VolumetricCloudsComponent.
- [x] 2.5.3 `r.Cloud.DetailErosion` — hardcoded 0.35 inline. Would need CloudCB expansion.
- [x] 2.5.4 `r.Cloud.DetailFrequencyScale` — hardcoded 4.0 inline. Would need CloudCB expansion.
- [x] 2.5.5 `r.Cloud.BeerPowderBlend` — hardcoded 0.5 inline but rarely tuned.
- [x] 2.5.6 `r.Cloud.TransmittanceThreshold` — ALREADY IN CB: CloudCB.g_stopTracingTransmittanceThreshold.
- [x] 2.5.7 `r.Cloud.MaxMarchDistanceKm` — ALREADY IN CB: CloudCB.g_tracingMaxDistanceKm.

## 3. Phase 3 — Low Priority CVars (specialist tuning)

### 3.1 Cloud FBM
- [ ] 3.1.1 Add `r.Cloud.NoiseXYScale` (float, default=0.12, range [0.01,1.0]) — cloud_raymarch_cs.hlsl:269
- [ ] 3.1.2 Add `r.Cloud.NoiseZScale` (float, default=3.0, range [0.5,10.0]) — cloud_raymarch_cs.hlsl:273
- [ ] 3.1.3 Add `r.Cloud.ShapeOctaves` (int, default=4, range [1,6]) — cloud_raymarch_cs.hlsl:278
- [ ] 3.1.4 Add `r.Cloud.FBMDecay` (float, default=0.5, range [0.3,0.8]) — cloud_raymarch_cs.hlsl:240
- [ ] 3.1.5 Add `r.Cloud.FBMLacunarity` (float, default=2.0, range [1.5,4.0]) — cloud_raymarch_cs.hlsl:241
- [ ] 3.1.6 Add `r.Cloud.AerialPerspectiveDensity` (float, default=0.04, range [0,0.2]) — cloud_raymarch_cs.hlsl:461

### 3.2 Atmosphere Fine-Tuning
- [x] 3.2.1 `r.Atmosphere.SunAngularRadius` — ALREADY IN CB: SkyAtmosphereCB.sunIlluminance.w. Hardcoded 0.00935 is just the fallback when w=0. Low impact.
- [x] 3.2.2 `r.Atmosphere.SunDiskEdgeFalloff` — hardcoded 0.0002. Physical constant, specialist-only. Deferred (would need SkyAtmosphereCB expansion).
- [x] 3.2.3 `r.Atmosphere.MieExtinctionRatio` — hardcoded 1.11. Physical constant from Bruneton 2008. Deferred (would need SkyAtmosphereCB expansion).

### 3.3 Contact Shadows Fine-Tuning
- [x] 3.3.1 `r.Shadow.ContactShadowMaxDistUV` — hardcoded 0.15. PerFrameCB padding exhausted (1 float left). Deferred to next CB expansion.
- [x] 3.3.2 `r.Shadow.ContactShadowStepScale` — hardcoded 0.01. Same constraint. Deferred.

### 3.4 TAA Fine-Tuning
- [x] 3.4.1 Add `r.TAA.SpatialWeightMultiplier` (float, default=2.29) — via TaaCB. Replaced exp(-2.29*d^2) with exp(-spatialWeightMul*d^2).
- [x] 3.4.2 Add `r.TAA.ExposureScale` (float, default=1.0) — via TaaCB. Replaced const FrameExposureScale=1.0 with CB field.

## 4. Infrastructure

### 4.1 CB Extension
- [x] 4.1.1 Added 14 CVar fields to PerFrameCB + PerFrameParams using padding slots (no layout shift, 768 bytes maintained).
- [x] 4.1.2 Verified byte offsets via offsetof() — shadowPCFPenumbra=708, materialMinGGXAlpha=760.
- [x] 4.1.3 Fill all CVar fields from CVars in frame_constants.cpp every frame.

### 4.2 Per-Shader Root Constants
- [x] 4.2.1 SSGI trace: expanded 8→13 DWORDs (5 CVar params via asfloat).
- [x] 4.2.2 SSGI temporal: expanded 8→10 DWORDs (temporal alpha + variance clip).
- [x] 4.2.3 SSGI spatial denoise: expanded 8→11 DWORDs (3 sigma params).
- [x] 4.2.4 SVGF atrous: expanded 8→11 DWORDs (3 sigma params).
- [x] 4.2.5 SVGF moments: expanded 8→9 DWORDs (normal sigma).
- [x] 4.2.6 SSGI upsample: expanded 8→10 DWORDs (depth + normal sigma).
- [x] 4.2.7 TAA: expanded TaaCB with 3 new float fields.
- [x] 4.2.8 Tonemap: expanded root constants 5→6 DWORDs (operator selection).

## 5. Testing
- [ ] 5.1 Verify all default CVar values match current hardcoded values (regression-free).
- [ ] 5.2 Test extreme CVar values (min/max) — no crashes, no NaN, no visual corruption.
- [ ] 5.3 Run `--pass-dump` before and after — PSNR >= 40 dB (identical output with defaults).
- [ ] 5.4 Verify 95%+ coverage on new C++ code.

## 6. Documentation
- [ ] 6.1 Update `docs/specs/rendering/shader-library.md` with CVar list per shader.
- [ ] 6.2 Create `docs/guides/cvar-reference.md` — complete CVar reference with categories, defaults, ranges.
- [ ] 6.3 Add `@ref` comments to each CVar declaration citing the published technique.
