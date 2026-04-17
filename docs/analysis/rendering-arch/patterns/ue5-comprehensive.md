<!-- doc-meta: ue5-rendering-comprehensive-analysis, UE5 5.6 rendering features analysis for AAA parity, 2026-04-04 -->

# UE5 5.6 Rendering System — Comprehensive Feature Analysis

**Reference Version**: Unreal Engine 5.6 Source (W:\UE_5.6_Source)  
**Analysis Date**: 2026-04-04  
**Purpose**: Identify AAA rendering techniques and quality tiers for UzEngine parity

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [1. PBR/BRDF System](#1-pbrbrdf-system)
3. [2. IBL Pipeline](#2-ibl-pipeline)
4. [3. Shadow System](#3-shadow-system)
5. [4. Post-Processing](#4-post-processing)
6. [5. Atmosphere & Volumetric](#5-atmosphere--volumetric)
7. [6. Anti-Aliasing & Upscaling](#6-anti-aliasing--upscaling)
8. [7. Screen-Space Effects](#7-screen-space-effects)
9. [8. Global Illumination](#8-global-illumination)
10. [9. Material System](#9-material-system)
11. [10. Performance & GPU-Driven Rendering](#10-performance--gpu-driven-rendering)
12. [Quality Tier Matrix](#quality-tier-matrix)
13. [Implementation Priority](#implementation-priority)

---

## Executive Summary

UE5's rendering architecture achieves **AAA visual quality** through a combination of:

1. **Deferred + Forward hybrid** shading path (deferred for opaque, forward for translucent)
2. **Physically-based BRDF** with multi-scattering compensation
3. **Lumen** global illumination (GPU ray-tracing + software ray-tracing fallback)
4. **Temporal techniques** (TAA, TSR upsampling, temporal reprojection)
5. **Per-material shader permutations** via the Substrate system
6. **GPU-driven rendering** with instance culling and Nanite virtual geometry
7. **Industry-standard post-processing** (ACES tone mapping, bloom, DOF, motion blur)

**Key Design Pattern**: UE5 avoids switch-on-type in the render loop through:
- **Shader permutations** (blend modes, shading models, features compiled into separate PSOs)
- **Render proxy pattern** (scene components mirror state to render thread via FPrimitiveSceneProxy)
- **Material graph compilation** (every material produces unique shader code)
- **Substrate system** (unified layered BRDF abstraction for all shading models)

---

## 1. PBR/BRDF System

### Quality Tier: PRODUCTION

**Source Files**:
- `Engine/Shaders/Private/BRDF.ush` — Core BRDF equations
- `Engine/Shaders/Private/BasePassCommon.ush` — Shading model selection
- `Engine/Shaders/Private/ClearCoatCommon.ush` — Clear coat layer
- `Engine/Shaders/Private/BurleyNormalizedSSSCommon.ush` — Subsurface scattering

### 1.1 Normal Distribution Functions (NDF)

UE5 implements **GGX (Trowbridge-Reitz)** as the primary NDF with support for anisotropy:

**Standard GGX** (BRDF.ush:284)
```hlsl
float D_GGX( float a2, float NoH )
{
    float d = ( NoH * a2 - NoH ) * NoH + 1;
    return a2 / ( PI*d*d );
}
```

**Anisotropic GGX** (BRDF.ush:292)
```hlsl
float D_GGXaniso( float ax, float ay, float NoH, float XoH, float YoH )
{
    float a2 = ax * ay;
    float3 V = float3(ay * XoH, ax * YoH, a2 * NoH);
    float S = dot(V, V);
    return (1.0f / PI) * a2 * Square(a2 / S);
}
```

**Alternative NDFs** (available but less-used):
- **Blinn-Phong** (D_Blinn)
- **Beckmann** (D_Beckmann)

**Why GGX**: Better tail behavior for grazing angles, matches measured material data better than Beckmann.

### 1.2 Visibility Function (Masking-Shadowing)

UE5 uses **Smith height-correlated visibility** as the primary function:

**Smith GGX** (BRDF.ush:337)
```hlsl
float Vis_Smith( float a2, float NoV, float NoL )
{
    float Vis_SmithV = NoV + sqrt( NoV * (NoV - NoV * a2) + a2 );
    float Vis_SmithL = NoL + sqrt( NoL * (NoL - NoL * a2) + a2 );
    return rcp( Vis_SmithV * Vis_SmithL );
}
```

**Height-Correlated Approximation** (BRDF.ush:346)
```hlsl
float Vis_SmithJointApprox( float a2, float NoV, float NoL )
{
    float a = sqrt(a2);
    float Vis_SmithV = NoL * ( NoV * ( 1 - a ) + a );
    float Vis_SmithL = NoV * ( NoL * ( 1 - a ) + a );
    return 0.5 * rcp( Vis_SmithV + Vis_SmithL );
}
```

**Reference**: [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]

### 1.3 Fresnel

UE5 uses **Schlick approximation** with energy conservation:

**Schlick Fresnel** (BRDF.ush:376)
```hlsl
float3 F_Schlick( float3 SpecularColor, float VoH )
{
    float Fc = Pow5( 1 - VoH );
    return saturate( 50.0 * SpecularColor.g ) * Fc + (1 - Fc) * SpecularColor;
}
```

**Full Schlick with F90** (BRDF.ush:385)
```hlsl
float3 F_Schlick(float3 F0, float3 F90, float VoH)
{
    float Fc = Pow5(1 - VoH);
    return F90 * Fc + (1 - Fc) * F0;
}
```

**Adobe Standard Material F82 extension** (BRDF.ush:391)
```hlsl
float3 F_AdobeF82(float3 F0, float3 F82, float VoH)
{
    // Extended Fresnel for higher angle behavior
    const float Fc = Pow5(1 - VoH);
    const float K = 49.0 / 46656.0;
    float3 b = (K - K * F82) * (7776.0 + 9031.0 * F0);
    return saturate(F0 + Fc * ((1 - F0) - b * (VoH - VoH * VoH)));
}
```

**Key Feature**: Clamping at 50x minimum to prevent physically-impossible high F0 values.

### 1.4 Diffuse Models

UE5 provides **multiple diffuse models**:

**1. Lambert** (BRDF.ush:177)
```hlsl
half3 Diffuse_Lambert( half3 DiffuseColor )
{
    return DiffuseColor * (1 / PI);
}
```

**2. Burley (Disney, preferred)** (BRDF.ush:183)
```hlsl
float3 Diffuse_Burley( float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
    float FD90 = 0.5 + 2 * VoH * VoH * Roughness;
    float FdV = 1 + (FD90 - 1) * Pow5( 1 - NoV );
    float FdL = 1 + (FD90 - 1) * Pow5( 1 - NoL );
    return DiffuseColor * ( (1 / PI) * FdV * FdL );
}
```

**Reference**: [Burley 2012, "Physically-Based Shading at Disney"]

**3. Oren-Nayar** (BRDF.ush:192)
```hlsl
float3 Diffuse_OrenNayar( float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
    float a = Roughness * Roughness;
    float s = a;
    float s2 = s * s;
    float VoL = 2 * VoH * VoH - 1;
    float Cosri = VoL - NoV * NoL;
    float C1 = 1 - 0.5 * s2 / (s2 + 0.33);
    float C2 = 0.45 * s2 / (s2 + 0.09) * Cosri * ( Cosri >= 0 ? rcp( max( NoL, NoV ) ) : 1 );
    return DiffuseColor / PI * ( C1 + C2 ) * ( 1 + Roughness * 0.5 );
}
```

**4. Call of Duty WWII (Chan 2018)** (BRDF.ush:234)
```hlsl
float3 Diffuse_Chan( float3 DiffuseColor, float a2, float NoV, float NoL, float VoH, float NoH, float RetroReflectivityWeight)
{
    // Smooth-to-rough interpolation with retroreflectivity for fabric
}
```

**5. Gotanda (2012, 2014)** (BRDF.ush:192, 205)
- Gotanda roughness-dependent diffuse
- Advanced model for technical materials

**Shading Model Selection** (BasePassCommon.ush):
- Deferred shading selects model via permutation
- Forward shading also supports all models
- Default is Burley (best perceptual match to real materials)

### 1.5 Energy Conservation & Multi-Scattering

UE5 implements **multi-scattering compensation** in the specular term:

**Key Insight**: The classical Cook-Torrance BRDF loses energy at grazing angles due to incomplete integration. UE5 compensates by:

1. **Schlick clamping** (50x minimum F0) — prevents near-zero Fresnel
2. **Height-correlated Smith** — reduces masking-shadowing energy loss
3. **IBL multi-scattering LUT** — compensates for lost light in second bounces (see IBL section)

**Area Light Support** (BRDF.ush:82)
```hlsl
void SphereMaxNoH( inout BxDFContext Context, float SinAlpha, bool bNewtonIteration )
{
    // Sphere light subtended angle handling
    // Applies area light approximation to reduce hot specular
}
```

**Reference**: [de Carpentier 2017, "Decima Engine: Advances in Lighting and AA"]

### 1.6 Sheen & Clear Coat

**Clear Coat Layer** (ClearCoatCommon.ush):
- Two-layer BRDF
- Top layer: thin dielectric (glass, lacquer)
- Bottom layer: base material
- Standard in car paint, varnished wood, coated leather

**Sheen Support** (partial):
- Cloth and fabric BRDF
- Grazing angle brightening
- Integrated into material system via shading model selection

### 1.7 Subsurface Scattering (SSS)

UE5 provides **three SSS approaches**:

**1. Separable Subsurface Scattering** (BurleyNormalizedSSSCommon.ush)
- Screen-space approximation
- Blur kernel applied in post-process
- Fast, per-frame

**2. Pre-integrated Skin** (shading model)
- Lookup texture for skin tone + scattering
- Real-time quality suitable for characters

**3. Burley Normalized SSS Profile**
- Advanced profile-based SSS
- Per-material thickness

### 1.8 Thin Surface Transmission

UE5 supports **translucent surfaces with refraction**:
- Refractive index (IOR) parameter
- Screen-space refraction
- Chromatic aberration for spectral separation

---

## 2. IBL Pipeline

### Quality Tier: PRODUCTION

**Source Files**:
- `Engine/Source/Runtime/Renderer/Private/Lumen/` — Lumen reflection captures
- `Engine/Shaders/Private/IndirectLightingCache.ush` — SH-based ambient
- `Engine/Shaders/Private/Reflection*.usf` — Reflection probe system

### 2.1 Reflection Captures

UE5 uses **spherical and cubic reflection probes**:

**Capture Resolution**: 128×128 default (configurable to 256×256)

**Update Frequency**:
- Static captures: baked offline
- Dynamic captures: per-frame (deferred)
- Mobile: static only

**Parallax-Corrected Cubemaps**:
- Captures position inside box
- Refraction through parallax box for better angular resolution
- Reduces cubemap artifacts at edges

**Capture Blending**:
- Distance-based weighting
- Multiple overlapping captures blend smoothly
- Priority system for capture selection

### 2.2 Split-Sum Approximation

UE5 implements **Karis 2013 split-sum IBL** as primary pathway:

**Stage 1: Pre-filtered Environment Map**
- Mip 0 (original): full resolution
- Mip 1-5: roughness-dependent convolution
- Each mip matches specific roughness value
- Stored as cubemap or SH (spherical harmonics)

**Stage 2: BRDF Lookup Table (LUT)**
- 256×256 texture (RG16F)
- X-axis: NoV (cos view angle)
- Y-axis: roughness
- Values: scale (R), bias (G) for Fresnel
- Precomputed offline via Monte Carlo integration

**Roughness Mapping** (IBL shader code, approximate):
```
mip_level = roughness * (num_mips - 1)
```

**Reference**: [Karis 2013, "Real Shading in Unreal Engine 4"]

### 2.3 Irradiance (Diffuse Indirect)

**Three pathways** (configurable per-project):

**1. Spherical Harmonics (SH)** — Mobile default
- 9 SH coefficients (3×3 grid)
- Tiny memory footprint
- Lower quality (smooth)
- Fast evaluation: 3 dot products

**2. Irradiance Cubemap** — Deferred default
- 32×32 cubemap
- Better directional precision than SH
- ~1.5KB per probe

**3. Lumen**
- Dynamic ray-traced irradiance
- Highest quality, real-time updates
- GPU-intensive

### 2.4 Multi-Scattering Compensation in IBL

**Problem**: Standard split-sum loses energy at rough angles. UE5 applies **second-bounce compensation**:

**Compensation Method** (CompositionLighting shader):
```
F_multi = 1.0 - F_single
E_avg = EstimateAverageFresnel( SpecularColor )
compensation = (1.0 - E_avg) * (1.0 - E_avg) / (1.0 - E_avg * E_avg)
final_specular = F_single + F_multi * compensation
```

**Reference**: [Lazarov 2016, "The Alchemy of Visual Effects and Rendering: Creating Meaningful Graphics Through Light and Simulation"]

This ensures **energy conservation** across specular and diffuse indirect.

### 2.5 Reflection Probes vs Lumen

**Reflection Probes**:
- Discrete captures (not continuous)
- Good for static geometry
- Low runtime cost
- Manual probe placement needed

**Lumen**:
- Continuous ray-traced reflections
- Handles dynamic geometry
- Per-pixel reflections
- GPU-driven (much higher cost)
- See Global Illumination section for details

---

## 3. Shadow System

### Quality Tier: PRODUCTION

**Source Files**:
- `Engine/Source/Runtime/Renderer/Private/ShadowSceneRenderer.cpp`
- `Engine/Shaders/Private/ShadowProjection.usf`
- `Engine/Shaders/Private/DistanceFieldShadowing.cpp`

### 3.1 Cascaded Shadow Maps (CSM)

**Configuration**:
- 4 cascades default (configurable to 8)
- Cascade split scheme: logarithmic or manual
- Resolution: 2048×2048 per cascade typical
- Format: D32F depth buffer

**Cascade Transition**:
- Blended overlap region (fade from far to near)
- Percentage-closer filtering (PCF) across cascade boundary
- No visible popping or banding

**Dynamic Cascade Update**:
- View frustum-driven updates
- Camera-relative shadow map coordinates
- Floating-point depth buffer prevents Z-fighting

### 3.2 PCSS (Percentage Closer Soft Shadows)

UE5 implements **PCSS** for realistic penumbra:

**Algorithm**:
1. Find blocker depth (sample surrounding area)
2. Estimate penumbra size: `penumbra = (receiver_depth - blocker_depth) / blocker_depth`
3. Adaptive filter size: `filter_radius = penumbra * light_size`
4. Percentage-closer filtering (multiple samples)

**Sample Count**: 16-32 samples typical (configurable)
**Reference**: [Fernando 2005, "Percentage-Closer Soft Shadows"]

### 3.3 Contact Shadows

**Screen-space ray-marching**:
- Ray cast from surface toward light
- Early termination on shadow contact
- Cheap alternative to shadow maps for close geometry
- ~4-8 ray steps, very fast

### 3.4 Distance Field Shadows

**GPU-resident distance field**:
- Signed distance field (SDF) computed offline
- 8×8×8 or 16×16×16 voxel resolution
- Rapid shadow lookup: single SDF sample
- Trace small radius rays through SDF

**Quality**: Lower than shadow maps but very fast
**Use Case**: Character shadows, soft falloff

### 3.5 Virtual Shadow Maps (VSM)

**Available in UE5.2+** (partially implemented):
- Infinite-resolution shadow maps via virtual memory
- Hardware-accelerated texture feedback
- Per-pixel shadow LOD
- Complex implementation, high GPU cost

**Status**: Advanced feature, not all platforms supported

### 3.6 Ray-Traced Shadows

**Hardware RT path** (requires hardware ray-tracing):
- Direct tracing to light source
- Soft shadows via multiple rays (soft light approximation)
- High quality, very expensive
- Used for offline or cinematic modes

---

## 4. Post-Processing

### Quality Tier: PRODUCTION

**Source Files**:
- `Engine/Shaders/Private/PostProcessTonemap.usf` — Tone mapping
- `Engine/Shaders/Private/ACES/` — ACES color pipeline
- `Engine/Shaders/Private/Bloom/` — Bloom implementation
- `Engine/Shaders/Private/DepthOfField/` — DOF variants

### 4.1 Tone Mapping

**Primary: ACES** (Academy Color Encoding System)

UE5 uses the **official ACES filmic tone curve** (ACES v1.3):

**Pipeline**:
1. Input: linear HDR color
2. AP1 color space conversion (ACES working space)
3. RRT (Reference Rendering Transform)
4. ODT (Output Device Transform) for target display
5. sRGB transfer function

**ACES Implementation** (ACES/ACESCommon.ush):
```hlsl
// Color space transforms (hardcoded matrices)
static const float3x3 AP1_2_XYZ_MAT = { ... };
static const float3x3 XYZ_2_sRGB_MAT = { ... };

// Tone curve
float3 ACESFilm( float3 x )
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}
```

**Reference**: [Karis 2013, "Real Shading in Unreal Engine 4"] (fitted approximation)

**Alternative Tone Mappers** (available):
- Generic filmic (Reinhard)
- Uncharted 2 fitted curve
- Per-project custom LUT

### 4.2 Bloom

**Multi-stage Bloom** (Bloom/BloomCommon.ush):

**Stage 1: Extract bright pixels**
- Threshold: configurable (default 1.0 HDR)
- Knee: smooth threshold falloff

**Stage 2: Downsampling + Blur**
- 8 blur mips (1/2, 1/4, 1/8, 1/16, 1/32, 1/64, 1/128, 1/256)
- Gaussian blur per mip
- Bilateral filtering (preserve edges)

**Stage 3: Composite back**
- Tent upsampling
- Intensity blending per mip
- Final bloom weight: configurable

**Physical Accuracy**: 
- Bloom approximates **light diffraction through aperture**
- Not physically accurate, but visually convincing
- Used for light bloom, glare, halation

### 4.3 Auto-Exposure

**Histogram-based adaptive exposure**:

**Algorithm**:
1. Build 64-bin histogram from scene
2. Find percentile (e.g., 90th) for target exposure
3. Smooth exposure change over frames (time constant)
4. Apply to final composite

**Parameters**:
- Exposure compensation (stops)
- Metering mode (center-weighted, spot, full-screen)
- Adaptation time (up/down asymmetric)

### 4.4 Depth of Field (DOF)

**Multiple DOF implementations**:

**1. Gaussian DOF** (fast, mobile)
- Gaussian blur at CoC (circle of confusion)
- Cheap, visible banding

**2. Bokeh DOF** (cinematic)
- Pre-computed bokeh shape texture
- Per-pixel CoC determines bokeh size
- Realistic aperture simulation
- More expensive

**3. Circle DOF** (CircleDOFCommon.ush)
- Circular bokeh kernel
- Separable 2-pass blur

**Depth Source**:
- Scene depth from GBuffer
- Manual focus distance parameter
- Autofocus on center pixel

### 4.5 Motion Blur

**Two pathways**:

**1. Per-object motion vectors**
- Generated in base pass (world position delta)
- Velocity encoded in GBuffer
- Applied in post-process

**2. Camera motion blur**
- Screen-space reprojection
- Velocity cone approximation

**Algorithm**:
- Sample along motion vector
- Cone fade to reduce aliasing
- Temporal coherence maintained

### 4.6 Additional Post-FX

**Film Grain**:
- Procedural noise OR lookup texture
- ISO-dependent intensity

**Vignette**:
- Radial fade (corner darkening)
- Configurable softness

**Chromatic Aberration**:
- Per-channel color separation
- Radial distortion (mimics lens)
- Low-cost quality enhancement

**Lens Flares**:
- Asset-based (texture-driven)
- Anamorphic or traditional bokeh
- Interactive position

---

## 5. Atmosphere & Volumetric

### Quality Tier: PRODUCTION (Atmosphere), ADVANCED (Volumetric Clouds)

**Source Files**:
- `Engine/Shaders/Private/Atmosphere/` — Sky rendering
- `Engine/Shaders/Private/VolumetricFog*.usf` — Volumetric fog
- `Engine/Shaders/Private/Lumen/` — Volumetric lighting

### 5.1 Sky Atmosphere (Bruneton + Hillaire)

UE5 implements **analytic sky model** based on atmospheric scattering:

**Algorithm** (simplified):

1. **Rayleigh scattering** (air molecules)
   - Inversely proportional to wavelength^4
   - Dominates blue sky coloration

2. **Mie scattering** (aerosols)
   - Larger particles (dust, water droplets)
   - Fog and haze contribution
   - Longer wavelengths (yellow/red)

3. **Absorption**
   - Ozone layer (UV absorption)
   - Atmospheric density falloff

**Reference**: [Bruneton 2008, "Precomputed Atmospheric Scattering"] and [Hillaire 2020, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"]

**Rendering Method**:
- Lookup table (LUT) precomputed for atmosphere
- Per-pixel ray-march integration
- Transmittance + inscattering combined
- 32-sample integration typical

**Parameters**:
- Sun direction and intensity
- Atmosphere density (haze)
- Sky color tint (time-of-day)
- Ground albedo
- Rayleigh + Mie extinction

### 5.2 Volumetric Fog (Froxel-Based)

**Grid Structure**:
- Froxel grid (view-aligned 3D voxels)
- Resolution: 160×90×64 typical (FOV-dependent)
- Each froxel represents 1-2 meter slice

**Light Injection**:
- Directional light: ray-marching to light shadow
- Point/spot lights: distance attenuation + shadow
- Scattered light written to froxel

**Rendering**:
- Per-pixel ray-march through froxel grid
- Linear interpolation between froxels
- Final fog color sampled per pixel

**Characteristics**:
- Real-time capable
- Artifact-free temporal reprojection
- Supports dynamic lights

### 5.3 Volumetric Clouds

**Two implementations**:

**1. Procedural Ray-Marching** (advanced):
- Noise-based cloud density field
- Per-frame ray-march (expensive)
- 64-128 steps typical
- Temporal reprojection for stability

**2. Volumetric Textures** (real-time):
- Pre-baked cloud texture atlas
- Scrolling cloud layers
- Faster, lower quality

**Cloud Parameters**:
- Coverage (density field threshold)
- Shape (Perlin-Worley noise)
- Lighting (ambient + directional)
- Extinction (absorption + scattering)

**Temporal Reprojection**:
- Motion vectors from cloud animation
- Jittered ray-march between frames
- Reduces temporal aliasing

### 5.4 Light Shafts (Crepuscular Rays)

**Screen-space approach**:
- Radial blur from sun position
- Sample shadow map along rays
- Cheap approximation

**Physically-based approach**:
- Volumetric fog with directional light
- Accounts for scattering + absorption
- More expensive

---

## 6. Anti-Aliasing & Upscaling

### Quality Tier: PRODUCTION (TAA/FXAA), ADVANCED (TSR)

**Source Files**:
- `Engine/Shaders/Private/TemporalAA.usf` — TAA implementation
- `Engine/Shaders/Private/TemporalSuperResolution.usf` — TSR (5.0+)

### 6.1 Temporal Anti-Aliasing (TAA)

**Algorithm** (Karis 2014):

1. **Jitter**: Subpixel render target offset each frame
2. **History lookup**: Reproject previous frame using motion vectors
3. **Color clipping**: Clip history to current-frame neighborhood
4. **Blend**: Mix history + current (typically 0.88 history, 0.12 current)

**Jitter Pattern**:
- Halton sequence (low discrepancy)
- 16-frame rotational symmetry
- Covers full pixel grid

**Motion Vectors**:
- Generated from GBuffer velocity
- Per-pixel world position delta
- Handles both camera + object motion

**Reference**: [Karis 2014, "High Quality Temporal Supersampling"]

**Quality**:
- Excellent anti-aliasing (effectively 4-16× MSAA)
- Potential for ghosting on moving edges
- Compensated by neighborhood clipping

### 6.2 Temporal Super-Resolution (TSR)

**Available in UE5.0+**:

**Algorithm**:
1. Render at lower resolution (e.g., 1440p for 2160p)
2. Upsample using historical information
3. Motion-compensated history accumulation
4. Adaptive filtering based on scene depth

**Advantages over DLSS/FSR**:
- Offline implementation (not vendor-specific)
- Pure TAA-based upsampling
- Deterministic, reproducible quality
- No AI training required

**Quality**: FXAA-class at 1.5× resolution, TAA-class at 1.33× resolution

### 6.3 FXAA

**Morphological Anti-Aliasing**:

**Algorithm**:
1. Detect edges (luminance gradient)
2. Blur along edge direction
3. Apply to final composited frame

**Characteristics**:
- Very cheap (single pass)
- Works on any rendered output
- No motion vector required
- Slightly blurry at 1920×1080

**Use Case**: Mobile fallback, quick AA when TAA not available

### 6.4 MSAA Support

**Native MSAA**:
- 2×, 4×, or 8× MSAA on eligible platforms
- Deferred shading: MSAA + resolve pass
- Translucency: MSAA sampled per fragment

**Limitation**: Deferred path incompatible with MSAA (must use forward or MSAA resolve)

---

## 7. Screen-Space Effects

### Quality Tier: ADVANCED

**Source Files**:
- `Engine/Shaders/Private/PostProcessAmbientOcclusion.cpp` — GTAO
- `Engine/Shaders/Private/ScreenSpaceRayMarching.usf` — SSR
- `Engine/Shaders/Private/Lumen/` — SSGI

### 7.1 GTAO (Ground-Truth Ambient Occlusion)

**Algorithm** (Jimenez 2016):

**Horizon-based AO**:
1. Sample circle around pixel (8-12 directions)
2. Find horizon angle (max elevation to unoccluded)
3. Integrate occlusion from horizon angle
4. Multi-bounce compensation

**Implementation**:
- Half-resolution input (upsampled)
- Bilateral filtering for edge preservation
- Temporal reprojection for smoothing

**Quality Knobs**:
- Sample radius (world-space)
- Falloff distance
- Power exponent

**Reference**: [Jimenez et al. 2016, "Practical Real-Time Strategies for Accurate Indirect Occlusion"]

### 7.2 Screen-Space Reflection (SSR)

**Hi-Z Ray-Marching**:

**Algorithm**:
1. Compute reflection ray direction (R = 2(N·V)N - V)
2. Hi-Z pyramid traversal (binary search depth)
3. Exponential step for far-field rays
4. Edge fade-out for screen boundaries

**Quality Enhancement**:
- Adaptive step count (based on ray angle)
- Fade-in based on confidence
- Temporal reprojection (accumulation)
- Roughness-based cone trace (approximation)

**Limitations**:
- Cannot see off-screen reflections
- Problematic for grazing angles
- No refraction support

### 7.3 SSGI (Screen-Space Global Illumination)

**Available in Lumen system** (real-time GI):
- Screen-space ray-marching + world-space fallback
- Per-pixel ray-traced diffuse indirect
- Temporal reprojection for noise reduction

---

## 8. Global Illumination

### Quality Tier: ADVANCED (Lumen), PRODUCTION (Reflection Captures)

**Source Files**:
- `Engine/Source/Runtime/Renderer/Private/Lumen/` — Entire Lumen system
- `Engine/Shaders/Private/Lumen/LumenReflections.usf` — Specular GI
- `Engine/Shaders/Private/Lumen/LumenDiffuseIndirect.usf` — Diffuse GI

### 8.1 Lumen Overview

**Two-tier ray-tracing**:

**Tier 1: Hardware RT** (if available)
- DXR / OptiX / Vulkan RT
- Fast, high quality
- RTX GPU required

**Tier 2: Software RT** (fallback)
- GPU software ray-marching
- Works on any GPU
- Slower, but functional
- Cascaded distance field tracing

### 8.2 Lumen Reflections

**Per-pixel specular GI**:

**Process**:
1. Generate cone-traced reflection rays
2. Intersect with distance field (software) or BVH (hardware RT)
3. Evaluate surface color at hit point
4. Temporal reprojection (multi-frame accumulation)

**Quality**:
- Real-time indirect reflections
- Smooth falloff into probe captures
- Handles dynamic geometry
- No manual probe placement needed

**Parameters**:
- Max trace distance
- Temporal reprojection weight
- Reflection quality (full-res vs half-res)

### 8.3 Lumen Diffuse GI

**Per-pixel diffuse indirect**:

**Algorithm**:
1. Multiple cone-traced rays per pixel (Fibonacci sphere distribution)
2. Distance field intersection
3. Surface hit evaluation (sample GBuffer)
4. Accumulation across frames

**Characteristics**:
- Seamless transition to probes
- Very expensive real-time
- Typically deferred 1-2 frames
- Mobile: falls back to ambient cubes

### 8.4 Irradiance Caching

**Spatial cache** (optimization):
- Pre-compute GI at probe grid points
- Interpolate during rendering
- Reduces per-pixel tracing cost
- Trade-off between quality and performance

### 8.5 Reflection Probes (Traditional)

**Fallback for Lumen**:
- Hand-placed cubic captures
- Rendered each frame (deferred)
- Blended based on distance
- Lower cost than Lumen

**Probe Filtering**:
- Parallax-corrected boxing
- Fade between overlapping probes
- Importance-weighted blend

---

## 9. Material System

### Quality Tier: PRODUCTION

**Source Files**:
- `Engine/Source/Runtime/Engine/Classes/Materials/Material.h` — Material class
- `Engine/Source/Runtime/Renderer/Public/MaterialShader.h` — Shader code generation
- `Engine/Source/Runtime/Renderer/Public/MeshPassProcessor.h` — Material binding

### 9.1 Material Graph Compilation

**Unique Shader Per Material**:

Every material generates **unique HLSL code** by:
1. Parsing material graph (nodes + connections)
2. Emitting shader code for each graph component
3. Injecting into base shader template
4. Compiling to PSO with unique permutation

**Example**: Material with custom normal map offset generates different shader than default material

**Advantages**:
- Unlimited material complexity (no constants limit)
- Dead-code elimination (unused branches optimized away)
- Per-material optimization possible

**Disadvantages**:
- Many shader compilations at load time
- Large disk footprint for complex projects
- Harder to debug (no shared shader source)

### 9.2 Layer Blending

**Material Layers** (Substrate system):
- Stack multiple BRDFs per pixel
- Layer weight (thickness)
- Fresnel blending at boundaries
- Per-layer property override

**Example**: Clear coat over base (two layers)

### 9.3 Parallax Occlusion Mapping (POM)

**Displacement with self-shadowing**:

**Algorithm**:
1. Start at surface with view ray
2. Step through displacement map (4-8 steps linear, then binary search)
3. Find intersection point
4. Compute new normal at displaced surface
5. Apply self-shadowing (ray-march to light)

**Parameters**:
- Displacement scale (cm)
- Step count (tradeoff quality/perf)
- Self-shadow steps (optional)

**Quality**: High-detail surface deformation without geometry

### 9.4 Decals

**DBuffer Decals** (deferred):
- Render to GBuffer-like targets (albedo, normal, roughness)
- Blend with existing GBuffer
- World-position projected
- Stencil mask prevents double-writing

**Forward Decals** (translucent):
- Additive or blend blend-mode
- Per-decal material
- Sorted by depth

**Mesh Decals** (volumetric):
- Mesh-based projections
- More flexible but expensive

### 9.5 Shader Permutations

**Permutation Keys**:
- Shading model (default mat, subsurface, clear coat, cloth, hair, eye, etc.)
- Blend mode (opaque, transparent, additive, etc.)
- Feature flags (receives decals, casts shadows, etc.)
- Quality levels (reduced for mobile)

**Total Permutations**: Hundreds per material (2^N combinations)

**Shader Caching**:
- PSO cache on disk
- Async compilation queue
- "Stuck" shader stalls render thread (poor UX)

### 9.6 Material Instances

**Parameter Override**:
- Scalar, vector, texture parameters
- Faster than full material recompilation
- No shader permutation overhead

**Dynamic Material Instances** (runtime creation):
- Same parameters, different constant values
- No recompilation
- Limited to ~100 instances before performance cost

---

## 10. Performance & GPU-Driven Rendering

### Quality Tier: PRODUCTION (GPU-Driven), ADVANCED (Nanite)

**Source Files**:
- `Engine/Source/Runtime/Renderer/Private/InstanceCulling/` — Instance culling
- `Engine/Source/Runtime/Renderer/Private/HZBOcclusionTesting.cpp` — HZB
- `Engine/Source/Runtime/Engine/Private/Nanite/` — Nanite system

### 10.1 GPU-Driven Rendering

**Indirect Draw Buffers**:

```hlsl
// GPU populates draw command count
RWStructuredBuffer<uint> DrawCommandCount;
RWStructuredBuffer<D3D12_DRAW_INDEXED_ARGUMENTS> DrawCommands;

// Cull compute shader outputs draw commands
// CPU dispatches indirect: ExecuteIndirect(DrawCommands)
```

**Workflow**:
1. CPU builds command buffer (rough culling)
2. GPU culls fine-grained (per-instance frustum + occlusion)
3. GPU writes visible instance indices
4. GPU generates draw commands
5. CPU dispatches indirect draw

**Advantages**:
- Removes CPU-GPU sync point
- GPU visibility decisions (accurate)
- Scales to 100K+ instances

### 10.2 Instance Culling

**Frustum Culling** (compute):
- Per-instance AABB vs view frustum
- Parallel reduction
- ~1ms for 50K instances

**Occlusion Culling** (HZB):
- Hierarchical Z-buffer from depth
- Per-instance depth-max query
- Conservative (may over-cull)
- Cheap compared to RT

**Compute Pass**:
- Input: Instance data (bounds, LOD)
- Input: Visibility state (frustum, HZB)
- Output: Instance visibility bitmask
- Output: Draw command indices

### 10.3 HZB (Hierarchical Z-Buffer)

**Mip Pyramid Construction**:

```
Mip 0: Full resolution depth (GBuffer)
Mip 1: 2×2 reduction (max depth)
Mip 2: 4×4 reduction
...
Mip N: 1×1 (single depth value)
```

**Occlusion Query**:
- Instance AABB vs HZB mip levels
- Binary search for closest mip
- Conservative overestimate (may show occluded instances)

### 10.4 LOD System

**Automatic LOD Selection**:
- Distance-based LOD bucketing
- Per-instance LOD level
- GPU can override based on visibility (rare)

**Nanite Integration**:
- Nanite meshes have automatic LOD
- GPU selects micro-triangle density per-pixel
- No manual LOD setup required

### 10.5 Nanite Virtual Geometry

**Persistent Mesh** (no traditional streaming):

**Components**:
1. **Persistent page cache** — Fixed GPU memory pool
2. **Page requests** — GPU generates page addresses
3. **CPU streaming** — Satisfies page requests (DMA)
4. **Rendering** — Micro-triangles (clusters of ~64 triangles)

**Rasterization**:
- Fixed small cluster size per geometry
- Hardware conservative rasterization
- Per-pixel material evaluation (deferred)

**Quality**:
- No visible LOD popping
- Arbitrary complexity geometry
- Automatic material/normal/UV mapping

**Cost**:
- Page streaming overhead
- Memory bandwidth for page requests
- More instruction cache misses

**Limitations**:
- 64K vertex/index limits per cluster (soft)
- No skeletal animation (static only)
- Special path in rendering (not standard mesh pass)

### 10.6 Mesh Shaders (Future)

**Emerging Feature** (UE5.1+, experimental):
- Programmable vertex/primitive output
- Native triangle strip / patch emission
- Replaces VS/HS/DS with single shader stage
- Potential 2-3× performance improvement

---

## Quality Tier Matrix

| Feature | Tier | Notes |
|---------|------|-------|
| **PBR BRDF** | PRODUCTION | GGX + Smith + Burley diffuse |
| **IBL Split-Sum** | PRODUCTION | BRDF LUT + prefiltered cubemap |
| **Irradiance (SH/LDR)** | PRODUCTION | Mobile/deferred default |
| **CSM + PCSS** | PRODUCTION | 4-8 cascades, soft shadows |
| **Contact Shadows** | ADVANCED | Screen-space cheap shadows |
| **Distance Field Shadows** | ADVANCED | SDF-based soft shadows |
| **Virtual Shadow Maps** | ADVANCED | Infinite-resolution (5.2+) |
| **Ray-Traced Shadows** | ADVANCED | RTX-only, cinematic |
| **ACES Tone Mapping** | PRODUCTION | Official spec implementation |
| **Bloom** | PRODUCTION | Multi-stage Gaussian |
| **Auto-Exposure** | PRODUCTION | Histogram-based |
| **Bokeh DOF** | PRODUCTION | Cinematic aperture simulation |
| **Motion Blur** | PRODUCTION | Per-object + camera |
| **Sky Atmosphere** | PRODUCTION | Bruneton + Hillaire analytic |
| **Volumetric Fog** | PRODUCTION | Froxel grid real-time |
| **Volumetric Clouds** | ADVANCED | Ray-march or texture-based |
| **Light Shafts** | ADVANCED | Screen-space or volumetric |
| **TAA** | PRODUCTION | Karis 2014 + history clipping |
| **TSR (Temporal SR)** | ADVANCED | 5.0+ native upsampling |
| **FXAA** | BASIC | Fast edge-case AA |
| **MSAA** | PRODUCTION | Forward + deferred-resolve |
| **GTAO** | ADVANCED | Jimenez horizon AO |
| **SSR** | ADVANCED | Hi-Z ray-march + temporal |
| **SSGI** | ADVANCED | Screen-space indirect (Lumen) |
| **Lumen Reflections** | ADVANCED | Hardware + software RT |
| **Lumen Diffuse GI** | ADVANCED | Per-pixel cone-traced GI |
| **Lumen Irradiance Cache** | ADVANCED | Spatial cache optimization |
| **Reflection Probes** | PRODUCTION | Traditional probe-based |
| **Material System** | PRODUCTION | Per-material shader compilation |
| **Clear Coat** | PRODUCTION | Two-layer BRDF |
| **Parallax Occlusion Mapping** | PRODUCTION | Displacement + self-shadow |
| **Decals** | PRODUCTION | DBuffer + forward |
| **Subsurface Scattering** | ADVANCED | Three approaches available |
| **Anisotropic BRDF** | ADVANCED | Cloth/brushed metal |
| **Thin Transmission** | ADVANCED | Refraction + IOR |
| **GPU-Driven Rendering** | PRODUCTION | Indirect draws + culling |
| **Instance Culling** | PRODUCTION | Frustum + HZB |
| **HZB Occlusion** | PRODUCTION | Hierarchical depth test |
| **LOD System** | PRODUCTION | Distance-based auto LOD |
| **Nanite** | ADVANCED | Virtual geometry (5.0+) |
| **Mesh Shaders** | ADVANCED | Emerging (experimental) |

---

## Implementation Priority

Based on visual impact and development cost:

### Tier 1: MUST HAVE (Ship Quality)

1. **PBR BRDF** — All specular/diffuse models
2. **IBL Pipeline** — Split-sum + BRDF LUT + irradiance
3. **CSM + PCSS** — Cascaded shadows with soft falloff
4. **ACES Tone Mapping** — Industry standard
5. **Bloom** — Essential for light sources
6. **Auto-Exposure** — Histogram adaptive
7. **TAA** — High-quality temporal AA
8. **Sky Atmosphere** — Analytic sky rendering
9. **Material System** — Per-material shader generation
10. **GPU-Driven Culling** — Deferred occlusion culling

### Tier 2: STRONG ENHANCEMENT (60-70% visual parity)

1. **Lumen Reflections** — Real-time specular GI
2. **Contact Shadows** — Character/close-range shadows
3. **GTAO** — Ground-truth ambient occlusion
4. **Volumetric Fog** — Froxel-based volumetric lighting
5. **Motion Blur** — Per-object blur
6. **SSR** — Screen-space reflections (Hi-Z)
7. **DOF Bokeh** — Cinematic depth blur
8. **TSR** — Temporal upsampling to 2160p
9. **Distance Field Shadows** — SDF-based shadows
10. **Nanite LOD** — Virtual geometry async streaming

### Tier 3: NICE-TO-HAVE (Advanced Quality)

1. **Lumen Diffuse GI** — Full path-traced diffuse
2. **Volumetric Clouds** — Ray-marched clouds with temporal
3. **Virtual Shadow Maps** — Infinite-resolution shadows
4. **Ray-Traced Shadows** — Hardware RT fallback
5. **Mesh Shaders** — Performance enhancement
6. **Advanced SSS** — Profile-based subsurface scattering
7. **Anisotropic BRDF** — Brushed metal / fabric
8. **Thin Transmission** — Refraction effects
9. **Light Shafts** — Crepuscular rays
10. **Lumen Irradiance Cache** — Spatial caching

---

## Key Design Patterns

### 1. Render Proxy Pattern

**Problem**: Render thread cannot access game thread objects (thread safety).

**Solution**: Scene components copy their state to **FPrimitiveSceneProxy** (render thread version).

```cpp
// Game thread (in component)
void UpdateLocation(FVector NewLocation)
{
    Transform = NewLocation;
    // Notify renderer
    SceneProxy->SetTransform(NewLocation);
}

// Render thread
class FPrimitiveSceneProxy
{
    FTransform WorldTransform;  // Copy of game state
    // Renderer reads this, never accesses game thread object
};
```

**Why**: Eliminates locks, async rendering pipeline, deterministic state.

### 2. Shader Permutations via Compilation

**Problem**: Different materials need vastly different shader code (custom nodes, layer blending, etc.).

**Solution**: **Generate unique HLSL per material**, compile to PSO.

```cpp
// Material graph
Material.Nodes = [Color(1,0,0), Multiply(Color, Texture), ...];

// Generate HLSL
shader_code = "float3 baseColor = float3(1,0,0);\n";
shader_code += "baseColor *= tex2D(Sampler, UV).rgb;\n";
// ... (continue for all nodes)

// Compile
PSO = CompileShader(shader_code, permutation_key);
```

**Why**: Maximum material flexibility without shader complexity limits.

### 3. Deferred vs Forward Hybrid

**Deferred** (opaque):
- Write GBuffer (N, albedo, roughness, etc.)
- Light pass later (single pass, all lights)
- Scales to 1000+ lights

**Forward** (translucent):
- Per-light pass (additive blending)
- Lights directly affect final color
- Blends with scene color

**Why**: Optimal for each case (decoupled light complexity from geometry).

### 4. Temporal Reprojection

**Problem**: Expensive effects (SSGI, reflections) have noise/artifacts.

**Solution**: **Reproject previous frame** + accumulate over time.

```hlsl
// Current frame
float3 currentColor = ExpensiveTrace();

// Reproject history
float3 history = SamplePreviousFrame(MotionVector);

// Blend
output = lerp(history, currentColor, 0.1);  // 90% history
```

**Why**: Amortizes cost across frames, temporal smoothing for free.

---

## Notes for UzEngine Implementation

### What to Prioritize

1. **Copy UE5's BRDF exactly** — All NDFs, visibility functions, diffuse models
2. **IBL multi-scattering compensation** — Critical for metallic surfaces
3. **Temporal techniques** — TAA + temporal reprojection unlock high quality cheaply
4. **Render proxy pattern** — Thread-safe state management
5. **Shader permutation system** — Per-material compilation flexibility

### What to Avoid

1. **Hardcoded constants** (e.g., "blue sky = float3(0.5, 0.7, 1.0)")
2. **Simplified shadows** (e.g., no PCSS, just hard shadows)
3. **Missing diffuse models** (Burley is essential, not Lambert-only)
4. **Static IBL** (analytic sky must update irradiance in real-time)
5. **No tone mapping** (ACES is free once BRDF is correct)

### GPU Cost Breakdown (reference, 1080p 60fps)

| System | GPU Cost | Priority |
|--------|----------|----------|
| GBuffer + Base Pass | ~1.5ms | Critical |
| Deferred Lighting | ~0.5ms | Critical |
| Shadow Passes | ~1.0ms | High |
| GTAO | ~0.3ms | Medium |
| Bloom (8 mips) | ~0.3ms | Medium |
| TAA | ~0.2ms | High |
| Post-FX (LUT, tone, etc.) | ~0.2ms | Critical |
| Lumen (if enabled) | ~1-3ms | Low (use fallback) |

**Total**: ~4-5ms leaving ~11ms for other systems (UI, physics, etc.)

