<!-- doc-meta
title: UE5 SkyLight Component — IBL Ambient Lighting Architecture
slug: ue5-skylight-component-analysis
type: analysis
category: rendering
tags: [skylight, ibl, ambient-lighting, ue5-architecture, spherical-harmonics]
created: 2026-03-15
updated: 2026-03-15
-->

# UE5 SkyLight Component — IBL Ambient Lighting Architecture

## Overview

The UE5 SkyLight component is a **scene-wide ambient lighting system** that contributes diffuse and specular illumination via an image-based lighting (IBL) approach. It differs fundamentally from traditional ambient light: instead of a uniform color, it samples a cubemap (either captured from the scene or artist-specified) and applies spherical harmonics (SH) diffuse convolution for low-frequency ambient lighting.

**Key insight**: The SkyLight is not a directional light. It's a **hemispherical (or full-sphere) light source** that dominates indirect illumination in most outdoor scenes.

---

## Architecture Overview

### 1. Component Hierarchy

```
USkyLightComponent (Engine/Classes/Components/SkyLightComponent.h)
    ├─ Inherits: ULightComponentBase
    ├─ Scene proxy: FSkyLightSceneProxy
    ├─ Runtime data:
    │   ├─ ProcessedSkyTexture (cubemap SRV)
    │   ├─ IrradianceEnvironmentMap (9 SH coefficients, precomputed)
    │   ├─ AverageBrightness (luminance of SH0 for exposure control)
    │   └─ BlendFraction (for transitioning between two cubemaps)
    └─ Editor-time data:
        ├─ CaptureStatus, SecondsSinceLastCapture
        └─ Real-time capture queue (for dynamic updates)
```

### 2. Proxy Transfer (Game Thread → Render Thread)

The `FSkyLightSceneProxy` is created when the component is registered and acts as the render-thread copy. Key initialization:

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\SkyLightComponent.cpp`, lines 227–282

```cpp
FSkyLightSceneProxy::FSkyLightSceneProxy(const USkyLightComponent* InLightComponent)
    : IndirectLightingIntensity(InLightComponent->IndirectLightingIntensity)  // ← Key parameter
    , VolumetricScatteringIntensity(InLightComponent->VolumetricScatteringIntensity)
    , OcclusionMaxDistance(InLightComponent->OcclusionMaxDistance)
    , Contrast(InLightComponent->Contrast)
    , OcclusionExponent(InLightComponent->OcclusionExponent)
    , MinOcclusion(InLightComponent->MinOcclusion)
    , OcclusionTint(InLightComponent->OcclusionTint)
    , LowerHemisphereColor(InLightComponent->LowerHemisphereColor)
    , bLowerHemisphereIsSolidColor(InLightComponent->bLowerHemisphereIsBlack)
    // ...
    , LightColor(FLinearColor(InLightComponent->LightColor) * InLightComponent->Intensity)
    , bMovable(InLightComponent->IsMovable())
```

The proxy caches these parameters for the render thread, avoiding game-thread reads during rendering.

---

## Key Parameters Exposed

### Direct Lighting Control

| Parameter | Type | Default | Purpose | UE5 Header Line |
|-----------|------|---------|---------|-----------------|
| **Intensity** | `float` | 1.0 | Multiplies base light color; affects overall brightness | LightComponentBase.h:36 |
| **LightColor** | `FLinearColor` | White | Base color of the sky light; multiplied by Intensity | LightComponentBase.h:43 |
| **IndirectLightingIntensity** | `float` | 1.0 | Scale factor applied to IBL diffuse contribution in deferred lighting pass | LightComponentBase.h:115 |
| **VolumetricScatteringIntensity** | `float` | 1.0 | Controls volumetric fog scattering from the sky light (separate from diffuse) | LightComponentBase.h:119 |

### Source & Capture

| Parameter | Type | Default | Purpose | UE5 Line |
|-----------|------|---------|---------|----------|
| **SourceType** | `ESkyLightSourceType` | `SLS_CapturedScene` | Either `SLS_CapturedScene` (real-time capture) or `SLS_SpecifiedCubemap` (fixed cubemap) | SkyLightComponent.h:112 |
| **Cubemap** | `UTextureCube*` | null | User-specified cubemap; only used if `SourceType == SLS_SpecifiedCubemap` | SkyLightComponent.h:116 |
| **SourceCubemapAngle** | `float` | 0 | Rotation angle applied to the cubemap in degrees (0–360) | SkyLightComponent.h:120 |
| **CubemapResolution** | `int32` | 128 | Resolution of the processed cubemap (must be power of 2; typically 64, 128, 256) | SkyLightComponent.h:124 |
| **SkyDistanceThreshold** | `float` | 150,000 cm | Maximum distance from capture position; beyond this, geometry is treated as "sky" | SkyLightComponent.h:131 |
| **bRealTimeCapture** | `bool` | false | If true, recaptures the scene every frame (expensive, useful for real-time updates) | SkyLightComponent.h:108 |
| **bCaptureEmissiveOnly** | `bool` | false | If true, only captures emissive materials (cheaper capture) | SkyLightComponent.h:135 |

### Lower Hemisphere Control

| Parameter | Type | Default | Purpose | UE5 Line |
|-----------|------|---------|---------|----------|
| **bLowerHemisphereIsBlack** | `bool` | false | If true, the lower hemisphere (below horizon) is clamped to `LowerHemisphereColor` instead of using the cubemap | SkyLightComponent.h:143 |
| **LowerHemisphereColor** | `FLinearColor` | Black | Solid color to use for the lower hemisphere when `bLowerHemisphereIsBlack` is true | SkyLightComponent.h:146 |

**Why this matters**: UE5 assumes a "planet surface" where you never receive light from below the horizon (ground blocks the sky). Setting `bLowerHemisphereIsBlack = true` makes this physically accurate. Disabling it (false) allows bounce-light approximation.

### Distance Field Ambient Occlusion (DFAO) Parameters

The SkyLight integrates with DFAO to modulate the IBL contribution based on surface occlusion:

| Parameter | Type | Default | Purpose | UE5 Line |
|-----------|------|---------|---------|----------|
| **OcclusionMaxDistance** | `float` | 600 cm | Max distance over which one point can occlude another in DFAO calculation | SkyLightComponent.h:153 |
| **Contrast** | `float` | 0.5 | S-curve contrast applied to AO (0 = no contrast, 1 = strong contrast) | SkyLightComponent.h:159 |
| **OcclusionExponent** | `float` | 1.0 | Power curve applied to computed AO (< 1 brightens, > 1 darkens) | SkyLightComponent.h:165 |
| **MinOcclusion** | `float` | 0.0 | Clamps the darkest fully-occluded areas to this minimum (0 = pure black, 1 = no occlusion) | SkyLightComponent.h:171 |
| **OcclusionTint** | `FColor` | Black | Color tint applied to occluded areas (for artistic control) | SkyLightComponent.h:175 |
| **OcclusionCombineMode** | `EOcclusionCombineMode` | Min | How to combine DFAO with SSAO: `Min` (conservative) or `Multiply` (additive darkening) | SkyLightComponent.h:206 |

### Volumetric Cloud Ambient Occlusion

| Parameter | Type | Default | Purpose | UE5 Line |
|-----------|------|---------|---------|----------|
| **bCloudAmbientOcclusion** | `bool` | false | If true, volumetric clouds occlude the sky light (dims ambient light in shadow) | SkyLightComponent.h:181 |
| **CloudAmbientOcclusionStrength** | `float` | 1.0 | Strength of cloud occlusion (0 = no occlusion, 1 = full occlusion) | SkyLightComponent.h:186 |
| **CloudAmbientOcclusionExtent** | `float` | 20 km | World-space radius around the camera to sample cloud occlusion | SkyLightComponent.h:191 |
| **CloudAmbientOcclusionMapResolutionScale** | `float` | 1.0 | Scales the base 512×512 cloud AO map (0.25–8.0 typical) | SkyLightComponent.h:196 |
| **CloudAmbientOcclusionApertureScale** | `float` | 0.01 | Cone aperture for cloud AO (0 = sharp single-direction, 1 = hemispherical blur) | SkyLightComponent.h:201 |

---

## How SkyLight Affects the Lighting Pipeline

### 1. Diffuse Ambient Lighting (Deferred Pass)

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyLightingDiffuseShared.ush`, lines 85–139

The SkyLight contribution to diffuse is computed in the deferred lighting pass via:

```hlsl
float3 SkyLightDiffuse(FGBufferData GBuffer, float AmbientOcclusion, ..., float3 DiffuseColor)
{
    // 1. Get SH diffuse lookup (precomputed spherical harmonics)
    float3 DiffuseLookup = GetSkySHDiffuse(Normal) * View.SkyLightColor.rgb;

    // 2. Apply AO modulation
    float SkyVisibility = length(BentNormal);  // From DFAO or SSAO
    SkyVisibility = pow(SkyVisibility, OcclusionExponent);  // Curve control
    SkyVisibility = lerp(SkyVisibility, 1, MinOcclusion);   // Clamp to minimum

    // 3. Accumulate diffuse contribution
    Lighting += SkyVisibility * DiffuseLookup * DiffuseColor * DiffuseWeight;
    Lighting *= View.PreExposure;  // Applied at composition time

    return Lighting;
}
```

**Key steps**:
1. The SH irradiance map is precomputed from the cubemap (9 SH coefficients)
2. The normal-dependent lookup is evaluated via SH basis functions
3. AO (bent normal) modulates the contribution
4. The result is multiplied by diffuse color and energy-preservation weight
5. The effective light color is `View.SkyLightColor = GetEffectiveLightColor()`, which includes:
   - `Intensity * LightColor * GSkylightIntensityMultiplier * SpecifiedCubemapColorScale`

### 2. SH Irradiance Lookup

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\ReflectionEnvironmentShared.ush`, lines 84–103

```hlsl
float3 GetSkySHDiffuse(float3 Normal)
{
    // SH coefficients stored in View.SkyIrradianceEnvironmentMap (9 float4s)
    float4 NormalVector = float4(Normal, 1.0f);

    // First 3 bands (L0, L1, L2) of spherical harmonics
    float3 Intermediate0;
    Intermediate0.x = dot(SkyIrradianceEnvironmentMap[0], NormalVector);  // L0, L1_x
    Intermediate0.y = dot(SkyIrradianceEnvironmentMap[1], NormalVector);  // L1_y, L1_z
    Intermediate0.z = dot(SkyIrradianceEnvironmentMap[2], NormalVector);  // L2_0, L2_1

    // L2 basis functions involve Normal.x*Normal.y, Normal.y*Normal.z, etc.
    float4 vB = NormalVector.xyzz * NormalVector.yzzx;
    Intermediate1.x = dot(SkyIrradianceEnvironmentMap[3], vB);
    Intermediate1.y = dot(SkyIrradianceEnvironmentMap[4], vB);
    Intermediate1.z = dot(SkyIrradianceEnvironmentMap[5], vB);

    // Final L2 basis (quadratic term)
    float vC = NormalVector.x * NormalVector.x - NormalVector.y * NormalVector.y;
    Intermediate2 = SkyIrradianceEnvironmentMap[6].xyz * vC;

    return max(0, Intermediate0 + Intermediate1 + Intermediate2);
}
```

This computes the diffuse irradiance by evaluating SH basis functions at the surface normal. The result is a 3D color per normal direction.

### 3. Specular Reflections

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\ReflectionEnvironmentShared.ush`, lines 43–50

```hlsl
float3 GetSkyLightReflection(float3 ReflectionVector, float Roughness, out float OutSkyAverageBrightness)
{
    // Sample the processed cubemap at a mip level determined by roughness
    float AbsoluteSpecularMip = ComputeReflectionCaptureMipFromRoughness(Roughness, MaxMipLevel);
    float3 Reflection = TextureCubeSampleLevel(SkyLightCubemap, Sampler, ReflectionVector, AbsoluteSpecularMip).rgb;

    // Apply the effective light color
    OutSkyAverageBrightness = GetSkyLightCubemapBrightness() * Luminance(View.SkyLightColor.rgb);
    return Reflection * View.SkyLightColor.rgb;
}
```

The specular contribution uses **roughness-dependent LOD** (lower mips for rougher surfaces).

### 4. Intensity Scaling

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SceneRendering.cpp`, lines 1886–1899

```cpp
if (SkyLight)
{
    ViewUniformShaderParameters.SkyLightColor =
        Family->EngineShowFlags.SkyLighting ?
            SkyLight->GetEffectiveLightColor() :  // ← Includes IndirectLightingIntensity
            FLinearColor::Black;
}
```

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\SkyLightComponent.cpp`, lines 222–225

```cpp
FLinearColor FSkyLightSceneProxy::GetEffectiveLightColor() const
{
    return LightColor * GSkylightIntensityMultiplier * SpecifiedCubemapColorScale;
    // LightColor = FLinearColor(LightColor) * Intensity
    // GSkylightIntensityMultiplier = r.SkylightIntensityMultiplier (engine cvar, default 1.0)
    // SpecifiedCubemapColorScale = color scale for the specified cubemap
}
```

So the final multiplier chain is:
```
View.SkyLightColor = Intensity × LightColor × r.SkylightIntensityMultiplier × SpecifiedCubemapColorScale
```

This color is then multiplied by the SH irradiance result in the shader.

---

## Occlusion & Shadow Integration

### 1. DFAO Bent Normal Modulation

The SkyLight works with **Distance Field Ambient Occlusion** to modulate the ambient contribution based on surface curvature and nearby geometry:

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyLightingDiffuseShared.ush`, lines 32–83

```hlsl
FSkyLightVisibilityData GetSkyLightVisibilityData(...)
{
    float SkyVisibility = 1;  // Start unoccluded

    #if APPLY_SKY_SHADOWING  // Only if DFAO is enabled
    {
        // Bent normal from DFAO has magnitude = occlusion factor (0 = fully occluded)
        SkyVisibility = length(BentNormal);

        // Use the bent normal direction instead of world normal
        // (shifts the lookup toward unoccluded directions)
        float3 NormalizedBentNormal = BentNormal / max(SkyVisibility, 0.00001f);
        SkyLightingNormal = lerp(NormalizedBentNormal, WorldNormal, SkyVisibility);

        // Apply contrast curve (S-curve: sigmoid)
        float ContrastCurve = 1 / (1 + exp(-ContrastAndNormalizeMulAdd.x * (SkyVisibility * 10 - 5)));
        SkyVisibility = saturate(ContrastCurve * ContrastAndNormalizeMulAdd.y + ContrastAndNormalizeMulAdd.z);

        // Apply exponent for fine control
        SkyVisibility = pow(SkyVisibility, OcclusionExponent);

        // Clamp to minimum (prevents pure black in corners)
        SkyVisibility = lerp(SkyVisibility, 1, OcclusionTint.w);  // w = MinOcclusion
    }
    #endif

    // Combine with other AO sources (SSAO, GBuffer AO)
    if (OcclusionCombineMode == 0)
    {
        SkyVisibility = min(SkyVisibility, min(GBufferAO, ScreenAO));  // Conservative
    }
    else
    {
        SkyVisibility = SkyVisibility * min(GBufferAO, ScreenAO);  // Multiplicative
    }

    // Final modulated lookup
    return FSkyLightVisibilityData{
        SkyDiffuseLookUpMul = SkyVisibility * DotProductFactor,
        SkyDiffuseLookUpAdd = (1 - SkyVisibility) * OcclusionTint.xyz,  // Color tint in shadows
        SkyDiffuseLookUpNormal = SkyLightingNormal
    };
}
```

**Key insight**: The AO doesn't just darken the ambient light; it also **redirects the normal** toward the least-occluded direction. This creates:
- **Contact shadows** in crevices (cavities, mortar, grooves)
- **Directional darkening** that follows surface geometry
- **Softer occlusion** via the contrast curve (no harsh shadows)

### 2. Cloud Ambient Occlusion

If `bCloudAmbientOcclusion = true`, volumetric clouds render a separate occlusion map that is sampled during the sky light pass, providing dynamic shadows from overhead cloud coverage.

---

## Lower Hemisphere Handling

**File**: `W:\UE_5.6_Source\Engine\Classes\Components\SkyLightComponent.h`, lines 138–146

The `bLowerHemisphereIsBlack` parameter (misleadingly named) controls how the **lower hemisphere** (below the horizon) is treated:

```cpp
/**
 * Whether all distant lighting from the lower hemisphere should be set to LowerHemisphereColor.
 * Enabling this is accurate when lighting a scene on a planet where the ground blocks the sky,
 * However disabling it can be useful to approximate skylight bounce lighting (eg Movable light).
 */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light, AdvancedDisplay, meta=(DisplayName = "Lower Hemisphere Is Solid Color"))
bool bLowerHemisphereIsBlack;

UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light, AdvancedDisplay)
FLinearColor LowerHemisphereColor;
```

### Behavior

| Setting | Effect | Use Case |
|---------|--------|----------|
| **`bLowerHemisphereIsBlack = true`** | Clamp lower-hemisphere SH irradiance to `LowerHemisphereColor` (usually black) | Outdoor scenes where ground physically blocks sky light (most realistic) |
| **`bLowerHemisphereIsBlack = false`** | Use full cubemap SH irradiance in all directions | Interior/underground scenes where you want bounce-light approximation from below |

**Implementation note**: The clipping is done **before** SH convolution, so it effectively zeroes out the lower-hemisphere contribution to the SH coefficients.

---

## Source Type: Captured vs. Specified

### SLS_CapturedScene

When `SourceType = SLS_CapturedScene`, the engine:
1. Renders the scene from the capture position out to `SkyDistanceThreshold`
2. Convolves the captured radiance into a cubemap at `CubemapResolution`
3. Pre-computes 9 SH coefficients from the cubemap
4. Uses these for the deferred lighting pass

**Cost**: Heavy (typically done once per level or with `r.SkylightUpdateEveryFrame 1` for real-time)

### SLS_SpecifiedCubemap

When `SourceType = SLS_SpecifiedCubemap`, the engine:
1. Uses the artist-provided `Cubemap` texture
2. Applies optional `SourceCubemapAngle` rotation
3. Scales by `SpecifiedCubemapColorScale` (optional per-cubemap color tint)
4. Pre-computes SH from the cubemap

**Cost**: Cheap (no scene capture)

---

## Adaptation Strategy for UzEngine

Based on this analysis, here's how to implement a simplified SkyLight for UzEngine:

### Minimal Parameters (MVP)

```cpp
struct SkyLightComponent {
    // ── Core Lighting ──
    Vec3  lightColor = {1, 1, 1};
    float intensity = 1.0f;
    float indirectIntensity = 1.0f;  // Scale factor for IBL diffuse

    // ── Source ──
    uint32_t irradianceSrvIndex = UINT32_MAX;  // SRV for SH irradiance cubemap
    float skyDistanceThreshold = 150000.0f;  // (Optional for later capture)

    // ── Occlusion ──
    float occlusionExponent = 1.0f;   // Controls AO curve
    float minOcclusion = 0.0f;        // Clamp darkest areas
    Vec3  occlusionTint = {0, 0, 0}; // Shadow color tint

    // ── Lower Hemisphere ──
    bool  lowerHemisphereIsSolidColor = true;
    Vec3  lowerHemisphereColor = {0, 0, 0};
};
```

### Shader Integration

In the deferred lighting pixel shader:

```hlsl
// Sample SH irradiance based on surface normal
float3 ambientDiffuse = GetSkySHDiffuse(surfaceNormal);
ambientDiffuse *= skylightColor;  // View.SkyLightColor equivalent

// Modulate by AO (if available)
float skyVisibility = bentNormalAO.length();
skyVisibility = pow(skyVisibility, skylightOcclusionExponent);
skyVisibility = lerp(skyVisibility, 1.0, skylightMinOcclusion);

// Final contribution
float3 skylightDiffuse = ambientDiffuse * skyVisibility * diffuseColor;

// Add to output lighting
lighting += skylightDiffuse;
```

### Constant Buffer Layout

Update `PerFrameCB` (currently in `constant_buffers.h`) to include:

```cpp
struct PerFrameCB {
    // ... existing fields ...

    // SkyLight parameters (NEW)
    Vec4  skylightColor;          // xyz = color * intensity, w = unused
    uint32_t irradianceSrvIndex;  // Already present (line 60)
    float skylightOcclusionExponent = 1.0f;
    float skylightMinOcclusion = 0.0f;
    Vec3  skylightOcclusionTint = {0, 0, 0};
    bool  skylightLowerHemiIsSolid = true;
    Vec3  skylightLowerHemiColor = {0, 0, 0};

    // Total padding to 256 bytes...
};
```

### Recommended Features (Phase 2+)

1. **Blending between two cubemaps** — for time-of-day transitions
2. **Cloud ambient occlusion** — dynamic cloud shadows on ambient light
3. **DFAO integration** — if implementing Distance Field AO later
4. **Real-time capture** — capture scene radiance at runtime (expensive)
5. **Cubemap rotation** — artist control via angle parameter

---

## Key Takeaways for UzEngine Implementation

### The "Why"

1. **SH Irradiance, not raw cubemap sampling**: Spherical harmonics provide a **frequency-domain representation** of the sky lighting. This is cheap to evaluate (9 dot products) and captures the low-frequency diffuse component of sky light.

2. **Intensity vs. IndirectLightingIntensity**: `Intensity` scales the light color; `IndirectLightingIntensity` scales the deferred pass contribution specifically. UE5 separates these to allow artists to control the relative strength of sky-light indirect lighting vs. direct lighting from other sources.

3. **AO as bent-normal redirection**: The occlusion doesn't just darken; it also **bends the normal** toward unoccluded directions. This is more sophisticated than simple scalar AO and creates contact shadows.

4. **Lower hemisphere clipping**: Treating the ground as opaque (not receiving light from below) is physically accurate for outdoor scenes and prevents light-leak artifacts.

### Measurement Points

If implementing, measure:
- **SH coefficient accuracy**: Pre-compute SH from a reference cubemap and compare coefficients to UE5's implementation
- **Diffuse irradiance**: Sample a matte surface in a known lighting environment and verify the computed irradiance matches expected values
- **AO modulation**: With DFAO enabled, verify that occluded surfaces darken proportionally to surface curvature

### Compatibility Notes

- UE5 uses **9 SH coefficients** (3rd-order, 3 bands of RGB each). This is the standard for game engines.
- The irradiance is stored in `View.SkyIrradianceEnvironmentMap` (array of 9 float4s in the view CB).
- Blending between two cubemaps is done via linear interpolation in the proxy (before rendering), not in the shader.

---

## References

**UE5 Source Files**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Components\SkyLightComponent.h` — Component definition
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\SceneProxies\SkyLightSceneProxy.h` — Scene proxy
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\SkyLightComponent.cpp` — Implementation (cubemap capture, SH precomputation)
- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyLightingDiffuseShared.ush` — Shader diffuse integration
- `W:\UE_5.6_Source\Engine\Shaders\Private\ReflectionEnvironmentShared.ush` — SH evaluation and specular reflection
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SceneRendering.cpp` (lines 1886–1899) — View CB setup

**Related Specs**:
- `docs/specs/renderer.md` — Rendering pipeline overview
- `docs/specs/shader-library.md` — Shader utility functions and IBL helpers
- `docs/analysis/rendering/ue5-ibl-pipeline.md` — IBL baking and cubemap generation

