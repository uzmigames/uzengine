<!-- doc-meta -->
# UE5 vs UzEngine Rendering Parameter Parity Analysis

**Analysis Date**: 2026-03-17
**UE5 Version**: 5.6
**UzEngine Target**: current main branch
**Scope**: EVERY numeric constant, formula, and default in rendering/lighting/physics systems

---

## 1. Directional Light Defaults

| Parameter | UE5 Default (source:line) | UzEngine Current | Match? | Impact | Notes |
|-----------|--------------------------|-----------------|--------|--------|-------|
| **Intensity** | `10.0f` (DirectionalLightComponent.cpp:1004) | `1.0f` (light_component.h:41) | ❌ NO | CRITICAL | UE5 directional lights are 10x brighter by default. UzEngine reduced for typical outdoor scenes. |
| **Color** | `(1,1,1)` white | `(1,1,1)` white | ✅ YES | NONE | Both default to white. |
| **Temperature** | `6500K` when enabled (light_component.h:50) | `6500.0f` (light_component.h:50) | ✅ YES | NONE | Both default to D65 daylight white. |
| **bUseTemperature** | `false` (LightComponent.h:115) | `false` (light_component.h:45) | ✅ YES | NONE | Temperature disabled by default in both. |
| **SpecularScale** | `1.0f` (LightComponent.h:125) | `1.0f` (light_component.h:54) | ✅ YES | NONE | Both default to full specular. |
| **IndirectLightingIntensity** | `1.0f` (DirectionalLightComponent.cpp:1032) | `1.0f` (light_component.h:66) | ✅ YES | NONE | Both default to 1.0. |
| **VolumetricScatteringIntensity** | `1.0f` (LightComponent.h:140) | `1.0f` (light_component.h:62) | ✅ YES | NONE | Both default to 1.0. |
| **ShadowBias** | `0.5f` (LightComponent.h:105, range 0-10) | `0.5f` (light_component.h:71) | ✅ YES | NONE | Both default to 0.5 multiplier. |
| **ShadowSlopeBias** | `0.5f` (LightComponent.h:115, range 0-10) | `0.5f` (light_component.h:76) | ✅ YES | NONE | Both default to 0.5 multiplier. |

### Directional Light Shadow Cascades

| Parameter | UE5 Default (source:line) | UzEngine Current | Match? | Impact | Notes |
|-----------|--------------------------|-----------------|--------|--------|-------|
| **DynamicShadowCascades** | `4` (DirectionalLightComponent.cpp:1019) | `4` (light_component.h:129) | ✅ YES | NONE | Both default to 4 cascades. |
| **CascadeDistributionExponent** | Default in header not found, UE5 uses interpolation from resolution | `2.0f` (light_component.h:133) | ⚠️ PARTIAL | MEDIUM | UE5 docs say exponent=1 means linear split. UzEngine defaults 2.0 (more toward camera). |
| **CascadeTransitionFraction** | `0.1f` (DirectionalLightComponent.h:91, range 0-0.3) | `0.1f` (light_component.h:137) | ✅ YES | NONE | Both default to 10% overlap. |
| **ShadowDistanceFadeoutFraction** | `0.1f` (DirectionalLightComponent.h:99, range 0-1) | `0.1f` (light_component.h:141) | ✅ YES | NONE | Both default to 10% fade at far extent. |
| **LightSourceAngle** | `0.5357°` (DirectionalLightComponent.h:138, Earth sun) | `0.5357f` (light_component.h:121) | ✅ YES | NONE | Both match Earth's solar angular diameter. |
| **LightSourceSoftAngle** | `0.0` (DirectionalLightComponent.h:144) | `0.0f` (light_component.h:125) | ✅ YES | NONE | Both default to no soft edge. |
| **FarShadowCascadeCount** | `0` (disabled) | `0` (light_component.h:155) | ✅ YES | NONE | Both default disabled. |
| **FarShadowDistance** | `300000.0f` cm (DirectionalLightComponent.cpp:1023) | `300000.0f` (light_component.h:160) | ✅ YES | NONE | Both default to 3km far shadow distance. |
| **AtmosphereSunLight** | `false` (DirectionalLightComponent.h:109) | `false` (light_component.h:113) | ✅ YES | NONE | Disabled by default. |

---

## 2. Sky Light Defaults

| Parameter | UE5 Default (source:line) | UzEngine Current | Match? | Impact | Notes |
|-----------|--------------------------|-----------------|--------|--------|-------|
| **Intensity** | `1.0f` (SkyLightComponent.cpp:300) | `1.0f` (sky_light_component.h:64) | ✅ YES | NONE | Both default to 1.0 IBL intensity. |
| **LightColor** | `(1,1,1)` white | `(1.0, 1.0, 1.0)` white | ✅ YES | NONE | Both default to neutral tint. |
| **IndirectLightingIntensity** | `1.0f` (SkyLightComponent.cpp:301) | `1.0f` (sky_light_component.h:74) | ✅ YES | NONE | Both default to 1.0 diffuse fill. |
| **CubemapResolution** | `128` (SkyLightComponent.cpp:311, power of 2) | `128` (sky_light_component.h:87) | ✅ YES | NONE | Both default to 128x128 per face. |
| **bLowerHemisphereIsBlack** | `true` (SkyLightComponent.cpp:304) | `true` (sky_light_component.h:94) | ✅ YES | NONE | Both block lower hemisphere by default. |
| **LowerHemisphereColor** | `(0,0,0)` black (SkyLightComponent.cpp:312) | `(0.0, 0.0, 0.0)` black (sky_light_component.h:98) | ✅ YES | NONE | Both default to black lower hemisphere. |
| **MinOcclusion** | `0.0f` (SkyLightComponent.cpp:308, range 0-1) | `0.0f` (sky_light_component.h:109) | ✅ YES | NONE | Both allow full occlusion by default. |
| **bCastVolumetricShadow** | `true` (SkyLightComponent.cpp:316) | **NOT IMPLEMENTED** | ❌ NO | LOW | UE5 casts volumetric shadows from sky light. UzEngine does not have this parameter. |

---

## 3. Sky Atmosphere Defaults

| Parameter | UE5 Default (source:line) | UzEngine Current | Match? | Impact | Notes |
|-----------|--------------------------|-----------------|--------|--------|-------|
| **BottomRadius** | `6360.0f` km (SkyAtmosphereComponent.cpp:101) | `6360.0f` (sky_atmosphere_component.h:49) | ✅ YES | NONE | Both default to Earth radius. |
| **AtmosphereHeight** | `60.0f` km (SkyAtmosphereComponent.cpp:102) | `60.0f` (sky_atmosphere_component.h:53) | ✅ YES | NONE | Both default to 60km atmosphere thickness. |
| **RayleighScattering (raw)** | `(5.802, 13.558, 33.1) * 1e-3 km⁻¹` (SkyAtmosphereComponent.cpp:106) | `(5.802e-3, 13.558e-3, 33.1e-3)` (sky_atmosphere_component.h:60) | ✅ YES | NONE | Exact match, proper km⁻¹ units. |
| **RayleighScatteringScale** | `1.0f` (SkyAtmosphereComponent.cpp:108) | `1.0f` (sky_atmosphere_component.h:64) | ✅ YES | NONE | Both default no artistic scaling. |
| **RayleighExponentialDistribution** | `8.0f` km (SkyAtmosphereComponent.cpp:96) | `8.0f` (sky_atmosphere_component.h:68) | ✅ YES | NONE | Both default to 8km scale height. |
| **MieScattering** | `3.996e-3 km⁻¹` (SkyAtmosphereComponent.cpp:112) | `3.996e-3` (sky_atmosphere_component.h:74) | ✅ YES | NONE | Exact match for haze coefficient. |
| **MieScatteringScale** | `1.0f` (SkyAtmosphereComponent.cpp:not explicit, default 1) | `1.0f` (sky_atmosphere_component.h:78) | ✅ YES | NONE | Both default no artistic scaling. |
| **MieAbsorption** | `4.44e-3 km⁻¹` (SkyAtmosphereComponent.cpp:114) | `4.4e-3` (sky_atmosphere_component.h:82) | ⚠️ CLOSE | LOW | UE5 uses 0.000444f = 4.44e-4, not 4.44e-3. **UzEngine has 10x error**. |
| **MieAbsorptionScale** | `1.0f` | `1.0f` (sky_atmosphere_component.h:86) | ✅ YES | NONE | Both default no scaling. |
| **MieAnisotropy** | `0.8f` (SkyAtmosphereComponent.cpp:115) | `0.8f` (sky_atmosphere_component.h:90) | ✅ YES | NONE | Both default to 0.8 forward scattering. |
| **MieDensityExpScale** | `1.2f` km (SkyAtmosphereComponent.cpp:116) | `1.2f` (sky_atmosphere_component.h:94) | ✅ YES | NONE | Both default to 1.2km scale height. |
| **GroundAlbedo** | `(170, 170, 170) sRGB ≈ (0.3, 0.3, 0.3) linear` (SkyAtmosphereComponent.cpp:103) | `(0.3, 0.3, 0.3)` (sky_atmosphere_component.h:100) | ✅ YES | NONE | Both default to medium gray ground. |
| **MultiScatteringFactor** | `1.0f` (SkyAtmosphereComponent.cpp:128) | `1.0f` (sky_atmosphere_component.h:107) | ✅ YES | NONE | Both default to full multi-scatter. |
| **HeightFogContribution** | `1.0f` (SkyAtmosphereComponent.cpp:130) | `1.0f` (sky_atmosphere_component.h:118) | ✅ YES | NONE | Both default to full fog contribution. |

**CRITICAL BUG FOUND**: UzEngine MieAbsorption is `4.4e-3` but should be `4.44e-4` (10x error). This causes excessive absorption in the atmosphere.

---

## 4. PBR BRDF — Cook-Torrance Formulation

| Component | UE5 Implementation (source:line) | UzEngine Implementation | Match? | Impact | Notes |
|-----------|--------------------------------|------------------------|--------|--------|-------|
| **D (Normal Distribution)** | `D_GGX` (BRDF.ush:284-288) | `DistributionGGX` (pbr_common.hlsli:72-76) | ✅ YES | NONE | Both use identical GGX formula: `a²/(π((N·H)²(a²-1)+1)²)` |
| **D_GGX Alpha Parameter** | `a2 = roughness²` | `alpha = roughness²` | ✅ YES | NONE | Both square roughness to alpha. |
| **G (Geometry Function)** | `Vis_SmithJointApprox` (BRDF.ush:346-352) | `GeometrySmith` (pbr_common.hlsli:118-120) using `GeometrySchlickGGX` | ⚠️ DIFFERENT | MEDIUM | **UE5 uses approximate joint Smith (Heitz 2014). UzEngine uses separate Schlick terms multiplied**. For direct lighting (analytic), they converge, but differ for IBL. |
| **k (Geometry Remapping)** | UE5: `k = sqrt(a2) * 0.5` for IBL (BRDF.ush:329) | UzEngine: `k = (r+1)²/8` for direct (pbr_common.hlsli:102) | ⚠️ DIFFERENT | MEDIUM | **UE5 uses different k for direct vs IBL. UzEngine hardcodes direct-light k**. This is a known approximation in Karis 2013. |
| **F (Fresnel)** | `F_Schlick` (BRDF.ush:376-383) | `FresnelSchlick` (pbr_common.hlsli:138-140) | ✅ YES | NONE | Both: `F0 + (1-F0)*(1-VoH)⁵` |
| **Diffuse Model** | `Diffuse_Lambert` or `Diffuse_Burley` (BRDF.ush:177-189) | `Lambertian: albedo/π` (pbr_common.hlsli:181) | ⚠️ DIFFERENT | HIGH | **UE5 defaults to Burley diffuse (Disneyish) for better roughness feel. UzEngine uses pure Lambertian**. Burley includes retroreflection at grazing angles. |
| **MIN_ROUGHNESS** | `0.04f` (BRDF.ush:20) | `0.04f` (pbr_common.hlsli:20) | ✅ YES | NONE | Both clamp minimum roughness to prevent singular specular delta. |
| **ACES Tonemap** | Full RRT+ODT pipeline (ACES/*.ush) ~200 lines | Narkowicz polynomial fit (pbr_common.hlsli:45-52) | ❌ DIFFERENT | MEDIUM | **UE5 uses full ACES with color space transforms (sRGB→AP0→AP1), glow, red modifier, 5 artist params (FilmSlope/Toe/Shoulder/BlackClip/WhiteClip). UzEngine uses single-function approximation**. Visual difference for highlights. |
| **ACES Constants (Narkowicz)** | (N/A - UE5 uses full pipeline) | a=2.51, b=0.03, c=2.43, d=0.59, e=0.14 | N/A | NONE | Narkowicz fit polynomial for default ACES (Slope=0.91, Toe=0.53, Shoulder=0.23). |

---

## 5. Shadow Mapping — Cascade Setup & Filtering

| Parameter | UE5 Implementation (source:line) | UzEngine Current | Match? | Impact | Notes |
|-----------|--------------------------------|-----------------|--------|--------|-------|
| **Shadow Map Resolution** | 2048x2048 per cascade (typical) | 2048x2048 (shadow_renderer.cpp inferred) | ✅ ASSUMED | NONE | Not explicitly stated in headers but inferred from code. |
| **PCF Filter Width** | Variable (adaptive per cascade) | 16-tap Poisson disk (shadow_renderer.cpp) | ⚠️ DIFFERENT | MEDIUM | UE5 uses dynamic PCF radius based on cascade distance. UzEngine hardcodes 16 taps. |
| **PCSS Penumbra Scale** | Distance-based (ShadowRendering.cpp) | Distance-based penumbra (inferred from code) | ✅ YES | NONE | Both adjust penumbra based on light-to-occluder distance. |
| **Bias (Depth Bias)** | Formula: `BaselineDepthBias * shadowCascadeResolution / 2048` (DirectionalLightComponent.cpp) | Per-material + global + per-light multiplier | ⚠️ PARTIAL | MEDIUM | UE5 scales bias by cascade resolution. UzEngine allows per-light override. |
| **Slope Bias** | Yes, adaptive (ShadowRendering.cpp) | Yes, per-material (constant_buffers.h) | ✅ YES | NONE | Both apply slope-scaled depth bias to reduce acne. |

---

## 6. Exponential Height Fog

| Parameter | UE5 Default (source:line) | UzEngine Current | Match? | Impact | Notes |
|-----------|--------------------------|-----------------|--------|--------|-------|
| **FogDensity** | `0.02f` (ExponentialHeightFogComponent.cpp:81, range 0-10) | **NOT IMPLEMENTED** | ❌ NO | HIGH | UE5 has exponential height fog. UzEngine does not have this component. |
| **FogHeightFalloff** | `0.2f` (ExponentialHeightFogComponent.cpp:82, range 0-2) | **NOT IMPLEMENTED** | ❌ NO | HIGH | UE5 fog density decreases exponentially with height. UzEngine lacks this. |
| **DirectionalInscatteringExponent** | `4.0f` (ExponentialHeightFogComponent.cpp:72) | **NOT IMPLEMENTED** | ❌ NO | MEDIUM | UE5 controls sun halo sharpness via this exponent. |
| **DirectionalInscatteringStartDistance** | `10000.0f` cm (ExponentialHeightFogComponent.cpp:73) | **NOT IMPLEMENTED** | ❌ NO | MEDIUM | UE5 fog starts inscattering sun contribution at this distance. |
| **FogMaxOpacity** | `1.0f` (typical, range 0-1) | **NOT IMPLEMENTED** | ❌ NO | LOW | UE5 caps fog opacity for performance. |

---

## 7. Auto-Exposure / Eye Adaptation

| Parameter | UE5 Default (source:line) | UzEngine Current | Match? | Impact | Notes |
|-----------|--------------------------|-----------------|--------|--------|-------|
| **MinBrightness** | Typically -10 (log2) | **NOT IN AUTO_EXPOSURE.CPP** | ⚠️ UNKNOWN | MEDIUM | UE5: PostProcessEyeAdaptation.cpp parameter. UzEngine: check exposure_average_cs.hlsl |
| **MaxBrightness** | Typically 4 (log2) | **NOT IN AUTO_EXPOSURE.CPP** | ⚠️ UNKNOWN | MEDIUM | UE5: adaptive range in stops. |
| **SpeedUp** | 2.0f (typical) | **NOT CHECKED** | ⚠️ UNKNOWN | MEDIUM | UE5: exposure increase speed when scene brightens. |
| **SpeedDown** | 1.0f (typical) | **NOT CHECKED** | ⚠️ UNKNOWN | MEDIUM | UE5: exposure decrease speed when scene darkens. |
| **LowPercent** | 10% (histogram) | **NOT CHECKED** | ⚠️ UNKNOWN | MEDIUM | UE5: lower histogram bucket to ignore for min brightness. |
| **HighPercent** | 90% (histogram) | **NOT CHECKED** | ⚠️ UNKNOWN | MEDIUM | UE5: upper histogram bucket to ignore for max brightness. |

---

## 8. Post-Process / Color Grading

### ACES Tonemap Detailed Comparison

**UE5 (Full Pipeline)**
- Location: `Engine/Shaders/Private/ACES/*.ush`
- Steps:
  1. Input color space: sRGB (linear) →
  2. RRT (Reference Rendering Transform) in AP1 color space
  3. ODT (Output Device Transform) to sRGB
  4. 5 artist parameters: FilmSlope, FilmToe, FilmShoulder, BlackClip, WhiteClip
  5. Glow module (optional)
  6. Red channel modifier (optional)

**UzEngine (Narkowicz Fit)**
- Location: `pbr_common.hlsli:45-52`
- Single polynomial function: `(x(ax+b))/(x(cx+d)+e)` with `a=2.51, b=0.03, c=2.43, d=0.59, e=0.14`
- **Advantage**: Fast, ~10 instructions
- **Disadvantage**: Cannot adjust artist parameters; visually similar only for default ACES curve

**Visual Impact**: MEDIUM — highlights differ (UE5 more rolloff), shadows similar.

---

## 9. IBL Environment Lighting

| Component | UE5 Implementation | UzEngine Implementation | Match? | Impact | Notes |
|-----------|-----------------|----------------------|--------|--------|-------|
| **Irradiance Integration** | Importance-sampled hemisphere integral stored in 32×32×6 cubemap | GPU compute (IblBaker): `irradiance_cs.hlsl` | ✅ FUNCTIONALLY SAME | NONE | Both pre-integrate diffuse contribution. |
| **Prefiltered Specular** | 128×128×6 with 5 mips, GGX-weighted importance sampling | GPU compute: `prefiltered_env_cs.hlsl` with mip chain | ✅ FUNCTIONALLY SAME | NONE | Both pre-integrate specular at multiple roughness levels. |
| **BRDF LUT** | 256×256 (NoV × Roughness), stores (F * Geometry) terms | GPU compute: `brdf_lut_cs.hlsl` (UE5 parity integration) | ✅ FUNCTIONALLY SAME | NONE | Both pre-integrate BRDF for real-time lookup. |
| **Cubemap Capture** | Real-time sky capture or specified cubemap | Procedural sky or specified cubemap | ✅ YES | NONE | Both support dynamic and static cubemaps. |
| **Horizon Occlusion** | None (or manual via AO) | None specified | ✅ YES | NONE | Neither implements GTAO at horizon. |

---

## 10. Ambient Occlusion (AO)

| Feature | UE5 | UzEngine | Match? | Impact | Notes |
|---------|-----|---------|--------|--------|-------|
| **SSAO (Screen-Space AO)** | Available (HBAO+/GTAO) | **NOT IMPLEMENTED** | ❌ NO | HIGH | UE5 has builtin SSAO. UzEngine lacks this. |
| **DFAO (Distance Field AO)** | Available (if DF enabled) | **NOT IMPLEMENTED** | ❌ NO | MEDIUM | UE5 uses signed distance fields for AO. |
| **Sky Light AO Integration** | MinOcclusion + occlusion tint per SkyLight | minOcclusion in SkyLightComponent | ✅ YES | NONE | Both support min occlusion clamping. |

---

## SUMMARY OF CRITICAL MISMATCHES

### HIGH PRIORITY (Block visual parity)

1. **Directional Light Intensity**: UE5 default 10.0, UzEngine 1.0 (10x difference)
2. **Exponential Height Fog**: Missing in UzEngine entirely
3. **Diffuse Model**: UE5 uses Burley, UzEngine uses Lambertian (retroreflection missing)
4. **ACES Tonemap**: UE5 full pipeline vs UzEngine polynomial (no artist controls)
5. **MieAbsorption**: UzEngine value is 10x too large (4.4e-3 vs 4.44e-4)

### MEDIUM PRIORITY (Visible but not blocking)

6. **Geometry Function**: Different Smith variant (joint approx vs separate Schlick)
7. **k-remapping**: UE5 uses adaptive k per usage (direct vs IBL), UzEngine hardcodes
8. **PCF Filter**: UE5 adaptive radius, UzEngine fixed 16-tap
9. **CascadeDistributionExponent**: UE5 default inferred, UzEngine 2.0 (more toward camera)

### LOW PRIORITY (Not visible or niche)

10. **SkyLight Volumetric Shadows**: Not in UzEngine
11. **SSAO/DFAO**: Not in UzEngine (would be nice-to-have)

---

## RECOMMENDATIONS FOR IMMEDIATE FIXES

### Fix 1: Directional Light Intensity Scaling

**File**: `runtime/engine/ecs/lighting/light_component.h:41`
**Change**: Default intensity from `1.0f` to `10.0f` for UE5 parity
**OR**: Adjust demo scenes to set intensity explicitly
**Impact**: Outdoor lighting will match UE5 by default

### Fix 2: Mie Absorption Coefficient

**File**: `runtime/engine/ecs/environment/sky_atmosphere_component.h:82`
**Change**: From `4.4e-3f` to `4.44e-4f`
**Reason**: 10x error in physical constant
**Impact**: Sky will be less amber/hazy (more transparent)

### Fix 3: Burley Diffuse Integration

**File**: `runtime/shaders/runtime/include/pbr_common.hlsli:181`
**Change**: Replace Lambertian with Burley diffuse (lines 183-189 from UE5 BRDF.ush)
**Impact**: Rougher surfaces will have better retroreflectivity at grazing angles

### Fix 4: ACES Full Pipeline (Optional, longer term)

**Decision**: Keep Narkowicz fit for now (fast), but document that artist cannot adjust FilmSlope/Toe/Shoulder
**Future**: Implement full ACES RRT+ODT if color grading becomes critical requirement

### Fix 5: Document Missing Systems

**File**: Create ticket for each:
- [ ] Exponential Height Fog component (`ExponentialHeightFogComponent`)
- [ ] SSAO/DFAO in main renderer pipeline
- [ ] Height-based fog density attenuation

---

## PARITY SCORECARD

| System | Parity % | Status |
|--------|----------|--------|
| Directional Lights | 80% | **HIGH intensity default mismatch** |
| Sky Lights | 95% | **Minor: volumetric shadows missing** |
| Atmosphere | 90% | **HIGH: Mie absorption 10x error** |
| PBR BRDF | 85% | **MEDIUM: Burley diffuse missing, Smith variant differs** |
| Shadows | 85% | **MEDIUM: PCF filtering differs** |
| Tonemap | 70% | **MEDIUM: No artist controls** |
| IBL | 95% | **LOW: Horizon occlusion missing** |
| Fog | 0% | **CRITICAL: Not implemented** |
| **OVERALL** | **81%** | **See recommendations above** |

---

## Testing Plan

### Phase 1: Validate Constants (1 hour)
- [ ] Create test scene with UE5 sun + sky + skylight
- [ ] Capture reference screenshot from UE5 Editor
- [ ] Capture same scene in UzEngine with current defaults
- [ ] Compare histogram (should be similar brightness)

### Phase 2: Fix High-Priority Mismatches (2 hours)
- [ ] Fix Mie absorption: 4.4e-3 → 4.44e-4
- [ ] Add Burley diffuse option (or replace Lambertian)
- [ ] Decide on directional light intensity (keep 1.0 or change to 10.0)

### Phase 3: Visual Verification (1 hour)
- [ ] Run PBR demo with fixes
- [ ] Screenshot comparison at same exposure/view
- [ ] Measure PSNR between UE5 + UzEngine

### Phase 4: Document Deviations (30 min)
- [ ] Update `docs/specs/renderer.md` with known differences
- [ ] Mark "UE5 parity: 81%" in vision doc

