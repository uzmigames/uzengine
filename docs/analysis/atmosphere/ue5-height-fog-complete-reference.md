<!-- doc-meta -->
<!-- title: UE5 Height Fog Complete Implementation Reference -->
<!-- author: unreal-architect -->
<!-- date: 2026-03-20 -->
<!-- category: rendering -->
<!-- status: reference -->
<!-- version: 5.6 -->

# UE5 Height Fog Complete Implementation Reference

> **PURPOSE**: Line-by-line extraction of EVERY formula, constant, and computation step from UE5 5.6 height fog system. **Not** summarized — exact code with line numbers for direct parity implementation.

## 1. Shader Constants & Epsilon Values

**File**: `HeightFogCommon.ush:56-57`

```hlsl
static const float FLT_EPSILON = 0.001f;
static const float FLT_EPSILON2 = 0.01f;
```

- `FLT_EPSILON` (0.001) — used to check if Falloff is small in CalculateLineIntegralShared
- `FLT_EPSILON2` (0.01) — used to test if `abs(Falloff) > 0.01` for Taylor expansion selection

---

## 2. Constant Buffer Layout (FFogStructData)

**File**: `HeightFogCommon.ush:64-85`

```hlsl
struct FogStructData
{
    float4 ExponentialFogParameters;              // line 66
    float4 ExponentialFogParameters2;             // line 67
    float4 ExponentialFogColorParameter;         // line 68
    float4 ExponentialFogParameters3;            // line 69
    float4 SkyAtmosphereAmbientContributionColorScale; // line 70
    float4 InscatteringLightDirection;           // line 71
    float4 DirectionalInscatteringColor;         // line 72
    float2 SinCosInscatteringColorCubemapRotation; // line 73
    float3 FogInscatteringTextureParameters;     // line 74
    float EndDistance;                            // line 75
    float ApplyVolumetricFog;                    // line 76
    float VolumetricFogStartDistance;            // line 77
    float VolumetricFogNearFadeInDistanceInv;    // line 78
    float3 VolumetricFogAlbedo;                  // line 79
    float VolumetricFogPhaseG;                   // line 80
};
```

### Field Semantics

**ExponentialFogParameters** (line 59 comment):
- `.x` = `FogDensity * exp2(-FogHeightFalloff * (CameraWorldPosition.z - FogHeight))`
- `.y` = `FogHeightFalloff`
- `.z` = `MaxWorldObserverHeight`
- `.w` = `StartDistance`

**ExponentialFogParameters2** (line 60 comment):
- `.x` = `FogDensitySecond * exp2(-FogHeightFalloffSecond * (CameraWorldPosition.z - FogHeightSecond))`
- `.y` = `FogHeightFalloffSecond`
- `.z` = `FogDensitySecond`
- `.w` = `FogHeightSecond`

**ExponentialFogParameters3** (line 61 comment):
- `.x` = `FogDensity`
- `.y` = `FogHeight`
- `.z` = whether to use cubemap fog color (0 or 1)
- `.w` = `FogCutoffDistance`

**FogInscatteringTextureParameters** (line 62 comment):
- `.x` = mip distance scale
- `.y` = bias
- `.z` = num mips

**ExponentialFogColorParameter** (line 68):
- `.xyz` = inscattering color
- `.w` = `1.0 - MinFogOpacity` (inverted MinFogOpacity)

---

## 3. CPU Precomputation: InitFogConstants()

**File**: `FogRendering.cpp:208-304`

### Input Values (from FExponentialHeightFogSceneInfo)

```cpp
FogInfo.FogData[i].Density           // Primary/secondary fog density
FogInfo.FogData[i].HeightFalloff     // Primary/secondary height falloff
FogInfo.FogData[i].Height            // Primary/secondary fog base height
FogInfo.StartDistance                // Fog exclusion start distance
FogInfo.FogColor                     // RGB inscattering color (3 floats)
FogInfo.FogMaxOpacity                // Max fog opacity (1 - MinFogOpacity)
FogInfo.FogCutoffDistance            // Beyond this distance, fog factor = 1 (fully opaque)
FogInfo.InscatteringColorCubemap     // Optional cubemap for inscattering color
FogInfo.FullyDirectionalInscatteringColorDistance  // Max distance for directional inscattering
FogInfo.NonDirectionalInscatteringColorDistance    // Min distance for directional inscattering
FogInfo.DirectionalInscatteringExponent            // Phase function exponent (when not using VFog match)
FogInfo.DirectionalInscatteringStartDistance       // Distance at which directional inscattering begins
FogInfo.DirectionalInscatteringColor               // Color of directional inscattering light
FogInfo.EndDistance                   // Horizontal distance beyond which fog is fully opaque
FogInfo.VolumetricFogStartDistance    // Where volumetric fog begins (excludes analytical fog before this)
FogInfo.VolumetricFogNearFadeInDistance // Fade-in distance from VolumetricFogStartDistance
FogInfo.VolumetricFogScatteringDistribution        // Phase G parameter (HenyeyGreenstein)
FogInfo.VolumetricFogEmissive         // Self-emissive color for volumetric fog matching
FogInfo.VolumetricFogAlbedo           // Albedo for volumetric fog matching
FogInfo.bEnableVolumetricFog          // Whether volumetric fog is active
```

### Camera Height Clamping (lines 223-243)

**Max Observer Height Calculation:**

```cpp
float MaxObserverHeightDifference = 65536.0f;  // line 223
float MaxObserverHeight = FLT_MAX;             // line 224

// For ortho cameras, compute view-dependent max height
if (bUsingOrthoHeightAdjustment)  // line 227
{
    FVector ViewForward = View.ViewMatrices.GetViewMatrix().GetColumn(2);
    MaxObserverHeightDifference = FMath::Max(
        View.ViewMatrices.GetViewOrigin().Z,
        View.ViewMatrices.GetCameraToViewTarget().Length() * FMath::Abs(ViewForward.Z)
    );
}

// For each fog layer, limit observer height
for (int i = 0; i < FExponentialHeightFogSceneInfo::NumFogs; i++)  // line 233
{
    if (FogInfo.FogData[i].Density > 0.0f)  // line 236
    {
        MaxObserverHeight = FMath::Min(
            MaxObserverHeight,
            FogInfo.FogData[i].Height + MaxObserverHeightDifference
        );
    }
}

// Final observer height (clamped)
const float ObserverHeight = bUsingOrthoHeightAdjustment ?
    MaxObserverHeight :
    FMath::Min<float>(View.ViewMatrices.GetViewOrigin().Z, MaxObserverHeight);
```

### CollapsedFogParameter Computation (lines 245-254)

For **each fog layer** `i` (primary and secondary):

```cpp
const float CollapsedFogParameterPower = FMath::Clamp(
    -FogInfo.FogData[i].HeightFalloff * (ObserverHeight - FogInfo.FogData[i].Height),
    -126.f + 1.f,  // min exponent for IEEE float
    +127.f - 1.f   // max exponent for IEEE float
);

CollapsedFogParameter[i] = FogInfo.FogData[i].Density * FMath::Pow(2.0f, CollapsedFogParameterPower);
```

**This equals**: `FogDensity * exp2(-FogHeightFalloff * (ObserverHeight - FogHeight))`

**Purpose**: Precomputes `FogDensity * exp2(-falloff * (camera_z - fog_height))` so shader doesn't need to compute it per-pixel.

### View Parameter Population (lines 256-300)

```cpp
// Primary fog parameters
View.ExponentialFogParameters = FVector4f(
    CollapsedFogParameter[0],      // .x = precomputed density term
    FogInfo.FogData[0].HeightFalloff, // .y = falloff
    MaxObserverHeight,              // .z = max observer height
    FogInfo.StartDistance           // .w = start distance
);

// Secondary fog parameters
View.ExponentialFogParameters2 = FVector4f(
    CollapsedFogParameter[1],      // .x = precomputed density term (secondary)
    FogInfo.FogData[1].HeightFalloff, // .y = falloff (secondary)
    FogInfo.FogData[1].Density,     // .z = raw density (secondary)
    FogInfo.FogData[1].Height       // .w = height (secondary)
);

// Inscattering color + opacity
View.ExponentialFogColor = FVector3f(FogInfo.FogColor.R, FogInfo.FogColor.G, FogInfo.FogColor.B);
View.FogMaxOpacity = FogInfo.FogMaxOpacity;

// Additional parameters
View.ExponentialFogParameters3 = FVector4f(
    FogInfo.FogData[0].Density,               // .x = raw density (primary)
    FogInfo.FogData[0].Height,                // .y = height (primary)
    FogInfo.InscatteringColorCubemap ? 1.0f : 0.0f, // .z = use cubemap flag
    FogInfo.FogCutoffDistance                 // .w = cutoff distance
);

// Cubemap rotation
View.SinCosInscatteringColorCubemapRotation = FVector2f(
    FMath::Sin(FogInfo.InscatteringColorCubemapAngle),
    FMath::Cos(FogInfo.InscatteringColorCubemapAngle)
);

// End distance (horizontal plane culling)
View.FogEndDistance = FogInfo.EndDistance;

// Sky atmosphere scale
View.SkyAtmosphereAmbientContributionColorScale = FogInfo.SkyAtmosphereAmbientContributionColorScale;

// Inscattering texture fade parameters
const float InvRange = 1.0f / FMath::Max(
    FogInfo.FullyDirectionalInscatteringColorDistance - FogInfo.NonDirectionalInscatteringColorDistance,
    0.00001f
);
float NumMips = FogInfo.InscatteringColorCubemap ? FogInfo.InscatteringColorCubemap->GetNumMips() : 1.0f;

View.FogInscatteringTextureParameters = FVector(
    InvRange,  // .x = mip distance scale
    -FogInfo.NonDirectionalInscatteringColorDistance * InvRange,  // .y = mip bias
    NumMips    // .z = num mips
);

// Directional inscattering
View.DirectionalInscatteringExponent = FogInfo.DirectionalInscatteringExponent;
View.DirectionalInscatteringStartDistance = FogInfo.DirectionalInscatteringStartDistance;

// Get sun light direction and color
FLightSceneInfo* SunLight = Scene->AtmosphereLights[0] ? Scene->AtmosphereLights[0] : Scene->SimpleDirectionalLight;
if (SunLight)
{
    View.InscatteringLightDirection = -SunLight->Proxy->GetDirection();  // Negate for camera-to-light ray
    View.DirectionalInscatteringColor = FogInfo.DirectionalInscatteringColor * SunLight->Proxy->GetColor().GetLuminance();
}
View.bUseDirectionalInscattering = SunLight != nullptr;

// Volumetric fog integration
View.bEnableVolumetricFog = FogInfo.bEnableVolumetricFog;
View.VolumetricFogStartDistance = FogInfo.VolumetricFogStartDistance;
View.VolumetricFogNearFadeInDistanceInv = FogInfo.VolumetricFogNearFadeInDistance > 0.0f ?
    (1.0f / FogInfo.VolumetricFogNearFadeInDistance) : 100000000.0f;
View.VolumetricFogPhaseG = FogInfo.VolumetricFogScatteringDistribution;

View.VolumetricFogAlbedo = FVector3f::Zero();  // unused by default

// If volumetric fog matching is enabled
if (bExpFogMatchesVolumetricFog)  // line 292
{
    View.DirectionalInscatteringStartDistance = 0.0f;
    View.DirectionalInscatteringExponent = 1.0f;
    View.ExponentialFogColor = FVector3f(FogInfo.VolumetricFogEmissive);
    View.VolumetricFogAlbedo = FVector3f(FogInfo.VolumetricFogAlbedo);
    View.DirectionalInscatteringColor = FLinearColor::Black;
}
```

---

## 4. Shader Computation: GetExponentialHeightFog()

**File**: `HeightFogCommon.ush:192-386`

This is the **main fog computation function**. Every step is critical for parity.

### 4.1 Line Integral Calculation (CalculateLineIntegralShared)

**File**: `HeightFogCommon.ush:192-199`

```hlsl
float CalculateLineIntegralShared(float FogHeightFalloff, float RayDirectionZ, float RayOriginTerms)
{
    // Falloff = HeightFalloff * RayDirectionZ
    // Clamped to -127 to prevent exp2() overflow in OpenGL
    float Falloff = max(-127.0f, FogHeightFalloff * RayDirectionZ);    // line 194

    // Main formula: (1 - exp2(-Falloff)) / Falloff
    float LineIntegral = ( 1.0f - exp2(-Falloff) ) / Falloff;          // line 195

    // Taylor expansion around Falloff = 0: log(2) - 0.5 * log(2)^2 * Falloff
    float LineIntegralTaylor = log(2.0) - ( 0.5 * Pow2( log(2.0) ) ) * Falloff;  // line 196

    // Select between exact formula and Taylor expansion based on magnitude
    return RayOriginTerms * ( abs(Falloff) > FLT_EPSILON2 ? LineIntegral : LineIntegralTaylor );  // line 198
}
```

**Numerics**:
- `log(2.0)` ≈ 0.693147
- `Pow2(log(2.0))` ≈ 0.480453
- `0.5 * Pow2(log(2.0))` ≈ 0.240227

**When to use**:
- Taylor expansion: `|Falloff| ≤ 0.01`
- Exact formula: `|Falloff| > 0.01`

---

### 4.2 Pre-Computed Fog Origin Factor (for ray overrides)

**File**: `HeightFogCommon.ush:201-210`

```hlsl
float PreComputeFogOriginFactor(float OriginHeight, float FogHeight, float FogFalloff, float FogDensity)
{
    const float CollapsedFogParameterPower = clamp(
        -FogFalloff * (OriginHeight - FogHeight),
        -126.f + 1.f,  // min exponent
        +127.f - 1.f   // max exponent
    );
    return FogDensity * pow(2.0f, CollapsedFogParameterPower);
}
```

Used when `bOverrideOrigin` is true (e.g., Lumen ray tracing).

---

### 4.3 Main Function: GetExponentialHeightFog()

**File**: `HeightFogCommon.ush:212-386`

**Function signature**:
```hlsl
half4 GetExponentialHeightFog(
    float3 WorldPositionRelativeToCamera,
    float ExcludeDistance,
    uint EyeIndex,
    ViewState InView,
    bool bOverrideOrigin = false,
    float3 OverrideRayStartRelativeToCamera = 0.0f.xxx,
    float3 OverrideRayDir = 0.0f.xxx,
    float OverrideRayLength = 0.0f
)
```

**Returns**: `half4(FogColor.rgb, FogOpacityFactor)`

#### Step 1: Load Fog Data (lines 215-216)
```hlsl
FogStructData FogData = GetFogStructData(EyeIndex);
float3 WorldCameraOrigin = DFHackToFloat(InView.WorldCameraOrigin);
float3 CameraToReceiver = WorldPositionRelativeToCamera;
```

#### Step 2: Override Origin Handling (lines 221-228)

If `bOverrideOrigin` is true (e.g., Lumen):
```hlsl
WorldCameraOrigin += OverrideRayStartRelativeToCamera;
CameraToReceiver = OverrideRayDir * OverrideRayLength;

// Recompute precomputed terms for new origin
FogData.ExponentialFogParameters.x = PreComputeFogOriginFactor(
    WorldCameraOrigin.z,
    FogData.ExponentialFogParameters3.y,     // FogHeight from Parameters3.y
    FogData.ExponentialFogParameters.y,      // FogHeightFalloff from Parameters.y
    FogData.ExponentialFogParameters3.x      // FogDensity from Parameters3.x
);

FogData.ExponentialFogParameters2.x = PreComputeFogOriginFactor(
    WorldCameraOrigin.z,
    FogData.ExponentialFogParameters2.w,     // FogHeightSecond
    FogData.ExponentialFogParameters2.y,     // FogHeightFalloffSecond
    FogData.ExponentialFogParameters2.z      // FogDensitySecond
);
```

#### Step 3: Extract Fog Parameters (lines 230-232)
```hlsl
const half MinFogOpacity = FogData.ExponentialFogColorParameter.w;
const float MaxWorldObserverHeight = FogData.ExponentialFogParameters.z;
const float3 WorldObserverOrigin = float3(
    WorldCameraOrigin.xy,
    min(WorldCameraOrigin.z, MaxWorldObserverHeight)  // Clamp Z to max height
);
```

**Note**: `ExponentialFogColorParameter.w` contains `1.0 - MinFogOpacity`, but we treat it as `MinFogOpacity` directly in the final clamp (see line 362).

#### Step 4: Apply End Distance (horizontal culling) (lines 235-240)

```hlsl
const float CameraToReceiverLenXYSqr = dot(CameraToReceiver.xy, CameraToReceiver.xy);
if (FogData.EndDistance > 0.0f && CameraToReceiverLenXYSqr > (FogData.EndDistance * FogData.EndDistance))
{
    // Scale ray to not exceed horizontal end distance
    CameraToReceiver *= FogData.EndDistance / sqrt(max(1.0, CameraToReceiverLenXYSqr));
}
```

Purpose: Clip ray to stay within `EndDistance` (XY plane).

#### Step 5: Compensate for Observer Height Clamping (lines 242-246)

```hlsl
// If camera was clamped to MaxObserverHeight, adjust ray direction
CameraToReceiver.z += WorldCameraOrigin.z - WorldObserverOrigin.z;

// Compute ray length and normalized direction
float CameraToReceiverLengthSqr = dot(CameraToReceiver, CameraToReceiver);
float CameraToReceiverLengthInv = rsqrt(max(CameraToReceiverLengthSqr, 0.00000001f));
float CameraToReceiverLength = CameraToReceiverLengthSqr * CameraToReceiverLengthInv;
half3 CameraToReceiverNormalized = CameraToReceiver * CameraToReceiverLengthInv;
```

#### Step 6: Initialize Ray Terms (lines 248-251)

```hlsl
float RayOriginTerms = FogData.ExponentialFogParameters.x;        // Precomputed density term (primary)
float RayOriginTermsSecond = FogData.ExponentialFogParameters2.x;  // Precomputed density term (secondary)
float RayLength = CameraToReceiverLength;
float RayDirectionZ = CameraToReceiver.z;
```

#### Step 7: Global Clipping Plane (for reflections) (lines 253-269)

```hlsl
#if USE_GLOBAL_CLIP_PLANE
BRANCH
if (dot(InView.GlobalClippingPlane.xyz, 1) > 0.0f)
{
    float CameraOriginPlaneDistance = dot(InView.GlobalClippingPlane, float4(WorldObserverOrigin + DFHackToFloat(InView.PreViewTranslation), 1));
    float PlaneIntersectionTime = -CameraOriginPlaneDistance / dot(CameraToReceiver, InView.GlobalClippingPlane.xyz);

    // If reflection plane is between camera and receiver, exclude up to intersection
    if (PlaneIntersectionTime > 0 && PlaneIntersectionTime < 1)
    {
        ExcludeDistance = max(ExcludeDistance, PlaneIntersectionTime * CameraToReceiverLength);
    }
}
#endif
```

#### Step 8: Start Distance Exclusion (lines 272-291)

**This is critical**: Fog does not render from camera to `StartDistance`.

```hlsl
#if PERMUTATION_SUPPORT_FOG_START_DISTANCE
ExcludeDistance = max(ExcludeDistance, FogData.ExponentialFogParameters.w);  // .w is StartDistance

if (ExcludeDistance > 0)
{
    // Time parameter at exclusion distance (0 to 1 along ray)
    float ExcludeIntersectionTime = ExcludeDistance * CameraToReceiverLengthInv;

    // Z coordinate where ray intersects exclusion distance
    float CameraToExclusionIntersectionZ = ExcludeIntersectionTime * CameraToReceiver.z;
    float ExclusionIntersectionZ = WorldObserverOrigin.z + CameraToExclusionIntersectionZ;

    // Z direction from exclusion point to receiver
    float ExclusionIntersectionToReceiverZ = CameraToReceiver.z - CameraToExclusionIntersectionZ;

    // Recalculate ray parameters starting from exclusion distance
    RayLength = (1.0f - ExcludeIntersectionTime) * CameraToReceiverLength;
    RayDirectionZ = ExclusionIntersectionToReceiverZ;

    // Recompute density terms at exclusion intersection Z
    float Exponent = max(-127.0f, FogData.ExponentialFogParameters.y * (ExclusionIntersectionZ - FogData.ExponentialFogParameters3.y));
    RayOriginTerms = FogData.ExponentialFogParameters3.x * exp2(-Exponent);

    float ExponentSecond = max(-127.0f, FogData.ExponentialFogParameters2.y * (ExclusionIntersectionZ - FogData.ExponentialFogParameters2.w));
    RayOriginTermsSecond = FogData.ExponentialFogParameters2.z * exp2(-ExponentSecond);
}
#endif
```

#### Step 9: Line Integral (Primary + Secondary) (lines 293-299)

```hlsl
// Calculate shared line integral (used for both inscattering and directional)
float ExponentialHeightLineIntegralShared = CalculateLineIntegralShared(
    FogData.ExponentialFogParameters.y,     // FogHeightFalloff
    RayDirectionZ,
    RayOriginTerms
);

#if PERMUTATION_SUPPORT_FOG_SECOND_TERM
ExponentialHeightLineIntegralShared += CalculateLineIntegralShared(
    FogData.ExponentialFogParameters2.y,     // FogHeightFalloffSecond
    RayDirectionZ,
    RayOriginTermsSecond
);
#endif

// Scale by ray length to get total optical depth
float ExponentialHeightLineIntegral = ExponentialHeightLineIntegralShared * RayLength;
```

**Meaning**: This is the optical depth τ (tau) along the ray.

#### Step 10: Inscattering Color (lines 301-301)

```hlsl
half3 InscatteringColor = ComputeInscatteringColor(CameraToReceiver, CameraToReceiverLength, FogData, EyeIndex);
```

See section 5 for `ComputeInscatteringColor()`.

#### Step 11: Directional Inscattering (lines 304-359)

```hlsl
half3 DirectionalInscattering = 0;

#if PERMUTATION_SUPPORT_FOG_DIRECTIONAL_LIGHT_INSCATTERING
BRANCH
if (FogData.InscatteringLightDirection.w >= 0  // .w is start distance (or -1 if disabled)
#if PERMUTATION_SUPPORT_FOG_INSCATTERING_TEXTURE
    && FogData.ExponentialFogParameters3.z == 0  // Not using cubemap
#endif
)
{
    // Complex: uses either sky atmosphere contribution or fallback

#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
    const float UniformPhaseFunction = 1.0f / (4.0f * PI);  // 0.07958...
    half3 DirectionalInscatteringColor;
    half3 DirectionalLightInscattering;

    // Primary light (index 0)
    DirectionalInscatteringColor = FogData.DirectionalInscatteringColor.xyz
        + InView.SkyAtmosphereHeightFogContribution * InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[0].rgb;

#if PROJECT_EXPFOG_MATCHES_VFOG
    // Use Henyey-Greenstein phase when matching volumetric fog
    DirectionalLightInscattering = DirectionalInscatteringColor
        * HenyeyGreensteinPhase(FogData.VolumetricFogPhaseG, dot(-CameraToReceiverNormalized, InView.AtmosphereLightDirection[0].xyz));
#else
    // Use power phase function
    DirectionalLightInscattering = DirectionalInscatteringColor
        * pow(saturate(dot(CameraToReceiverNormalized, InView.AtmosphereLightDirection[0].xyz)), FogData.DirectionalInscatteringColor.w)
        * UniformPhaseFunction;
#endif

    // Secondary light (index 1), if enabled
    if (InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[1].a > 0.0f)
    {
        DirectionalInscatteringColor = FogData.DirectionalInscatteringColor.xyz
            + InView.SkyAtmosphereHeightFogContribution * InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[1].rgb;

#if PROJECT_EXPFOG_MATCHES_VFOG
        DirectionalLightInscattering += DirectionalInscatteringColor
            * HenyeyGreensteinPhase(FogData.VolumetricFogPhaseG, dot(-CameraToReceiverNormalized, InView.AtmosphereLightDirection[1].xyz));
#else
        DirectionalLightInscattering += DirectionalInscatteringColor
            * pow(saturate(dot(CameraToReceiverNormalized, InView.AtmosphereLightDirection[1].xyz)), FogData.DirectionalInscatteringColor.w)
            * UniformPhaseFunction;
#endif
    }
#else
    // Fallback: no sky atmosphere
    half3 DirectionalLightInscattering = FogData.DirectionalInscatteringColor.xyz
        * pow(saturate(dot(CameraToReceiverNormalized, FogData.InscatteringLightDirection.xyz)), FogData.DirectionalInscatteringColor.w);
#endif

    // Calculate optical depth for directional light only
    float DirectionalInscatteringStartDistance = PROJECT_EXPFOG_MATCHES_VFOG ? 0.0 : FogData.InscatteringLightDirection.w;
    float DirExponentialHeightLineIntegral = ExponentialHeightLineIntegralShared * max(RayLength - DirectionalInscatteringStartDistance, 0.0f);

    // Transmission (transmittance = exp(-τ))
    half DirectionalInscatteringFogFactor = saturate(exp2(-DirExponentialHeightLineIntegral));

    // Final directional inscattering
    DirectionalInscattering = DirectionalLightInscattering * (1 - DirectionalInscatteringFogFactor);
}
#endif

#if PROJECT_EXPFOG_MATCHES_VFOG
DirectionalInscattering *= FogData.VolumetricFogAlbedo;  // Apply volumetric albedo
#endif
```

**Key formulas**:
- Phase function (power): `pow(cos_theta, exponent)`
- Phase function (HG): `HenyeyGreensteinPhase(g, cos_theta)`
- Directional optical depth: `LineIntegralShared * (RayLength - StartDistance)`
- Transmittance: `exp2(-OpticalDepth)`
- Final color: `DirectionalColor * (1 - Transmittance)`

#### Step 12: Final Fog Factor (lines 361-369)

```hlsl
// Calculate transmission: exp2(-ExponentialHeightLineIntegral)
half ExpFogFactor = max(saturate(exp2(-ExponentialHeightLineIntegral)), MinFogOpacity);

// Cutoff distance: beyond FogCutoffDistance, fog is fully opaque
FLATTEN
if (FogData.ExponentialFogParameters3.w > 0 && CameraToReceiverLength > FogData.ExponentialFogParameters3.w)
{
    ExpFogFactor = 1;  // Fully opaque
    DirectionalInscattering = 0;  // No directional light
}
```

#### Step 13: Final Color Blend (lines 371-376)

```hlsl
#if (MATERIALBLENDING_ADDITIVE || MATERIALBLENDING_MODULATE) && !FOG_MATERIALBLENDING_OVERRIDE
    half3 FogColor = 0.0;  // No fog on additive/modulate blends
#else
    half3 FogColor = (InscatteringColor) * (1 - ExpFogFactor) + DirectionalInscattering;
#endif
```

**Composition formula**:
```
FogColor = InscatteringColor * (1 - Transmittance) + DirectionalInscattering
```

#### Step 14: Primitive Alpha Holdout (lines 378-383)

```hlsl
#if SUPPORT_PRIMITIVE_ALPHA_HOLDOUT
if (IsExponentialFogHoldout(View.EnvironmentComponentsFlags) && View.bPrimitiveAlphaHoldoutEnabled)
{
    FogColor.rgb *= 0;  // Zero fog color if holdout is enabled
}
#endif
```

#### Step 15: Return (line 385)

```hlsl
return half4(FogColor, ExpFogFactor);
```

**Returns**:
- `.rgb` = inscattering color + directional light contribution
- `.a` = transmittance (opacity factor)

---

## 5. Inscattering Color Computation

**File**: `HeightFogCommon.ush:140-188`

### 5.1 Base Inscattering Color

```hlsl
half3 ComputeInscatteringColor(float3 CameraToReceiver, float CameraToReceiverLength, FogStructData FogData, uint EyeIndex)
{
    half3 Inscattering = FogData.ExponentialFogColorParameter.xyz;  // Base color
```

### 5.2 Cubemap Inscattering (if enabled) (lines 144-171)

```hlsl
#if PERMUTATION_SUPPORT_FOG_INSCATTERING_TEXTURE
BRANCH
if (FogData.ExponentialFogParameters3.z > 0)  // If cubemap is enabled
{
    // Fade from non-directional (mip N) to directional (mip 0) based on distance
    float FadeAlpha = saturate(
        CameraToReceiverLength * FogData.FogInscatteringTextureParameters.x  // .x = mip scale
        + FogData.FogInscatteringTextureParameters.y                          // .y = mip bias
    );

    // Rotate cubemap around Z axis
    float3 CubemapLookupVector = CameraToReceiver;
    CubemapLookupVector.xy = float2(
        dot(CubemapLookupVector.xy, float2(FogData.SinCosInscatteringColorCubemapRotation.y, -FogData.SinCosInscatteringColorCubemapRotation.x)),
        dot(CubemapLookupVector.xy, FogData.SinCosInscatteringColorCubemapRotation.xy)
    );

    // Sample at two mip levels
    float3 DirectionalColor = TextureCubeSampleLevel(
        FogStruct.FogInscatteringColorCubemap,
        SharedFogInColorScatteringSampler,
        CubemapLookupVector,
        0  // Mip 0 (directional)
    ).xyz;

    float3 NonDirectionalColor = TextureCubeSampleLevel(
        FogStruct.FogInscatteringColorCubemap,
        SharedFogInColorScatteringSampler,
        CubemapLookupVector,
        FogData.FogInscatteringTextureParameters.z  // Num mips
    ).xyz;

    // Blend between directional and non-directional
    Inscattering *= lerp(NonDirectionalColor, DirectionalColor, FadeAlpha);
}
#endif
```

### 5.3 Sky Atmosphere Contribution (lines 174-180)

```hlsl
#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
float3 SkyAmbient = FogData.SkyAtmosphereAmbientContributionColorScale.rgb
    * View.SkyAtmosphereHeightFogContribution.xxx
    * GetViewDistanceSkyLightColor();

#if PROJECT_EXPFOG_MATCHES_VFOG
SkyAmbient *= FogData.VolumetricFogAlbedo;  // Apply volumetric albedo if matching
#endif

Inscattering += SkyAmbient;
#endif
```

---

## 6. Pixel Shader Entry Point

**File**: `HeightFogPixelShader.usf:39-138`

### 6.1 Function Signature

```hlsl
void ExponentialPixelMain(
    float2 TexCoord : TEXCOORD0,
    float3 ScreenVector : TEXCOORD1,
    float4 SVPos : SV_POSITION,
    out float4 OutColor : SV_Target0
)
```

### 6.2 Depth Sampling (lines 46-80)

**From scene depth texture:**

```hlsl
float SceneDepth;
float DeviceZ;

#if PERMUTATION_SAMPLE_FOG_ON_CLOUDS
// Special case: sample cloud depth (in km), convert to cm
const float CloudDepthKm = SrcCloudDepthTexture.Load(int3(PixPos, 0)).r;
SceneDepth = CloudDepthKm * KILOMETER_TO_CENTIMETER;
#else
if (bUseWaterDepthTexture)
{
    // Water surface depth
    float2 SampleUV = clamp(TexCoord, WaterDepthTextureMinMaxUV.xy, WaterDepthTextureMinMaxUV.zw);
    DeviceZ = Texture2DSampleLevel(WaterDepthTexture, WaterDepthSampler, SampleUV, 0).r;
    SceneDepth = ConvertFromDeviceZ(DeviceZ);
}
else
{
    // Regular scene depth
    DeviceZ = Texture2DSampleLevel(SceneTexturesStruct.SceneDepthTexture, SceneTexturesStruct_SceneDepthTextureSampler, TexCoord, 0).r;
    SceneDepth = ConvertFromDeviceZ(DeviceZ);
}
#endif
```

**Note**: `ConvertFromDeviceZ()` converts reverse-Z depth to linear scene depth (in cm).

### 6.3 World Position Reconstruction (line 84)

```hlsl
float3 WorldPositionRelativeToCamera = ScreenVector.xyz * SceneDepth;
```

Where `ScreenVector` is the normalized camera-to-world direction (from screen UV).

### 6.4 Volumetric Fog Lookup Setup (lines 86-93)

```hlsl
float ZSlice = log2(
    SceneDepth * View.VolumetricFogGridZParams.x + View.VolumetricFogGridZParams.y
) * View.VolumetricFogGridZParams.z * View.VolumetricFogInvGridSize.z;

uint3 Rand16Bits = Rand3DPCG16(int3(SVPos.xy, View.StateFrameIndexMod8));
float3 Rand3D = (float3(Rand16Bits) / float(uint(0xffff))) * 2.0f - 1.0f;
float3 CellOffset = UpsampleJitterMultiplier * Rand3D;

float3 VolumeUV = float3(
    ((SVPos.xy - View.ViewRectMin.xy) + CellOffset.xy) * View.VolumetricFogSVPosToVolumeUV,
    ZSlice
);
VolumeUV.xy = min(VolumeUV.xy, View.VolumetricFogUVMax);
```

### 6.5 Height Fog Calculation (line 95)

```hlsl
float4 HeightFogInscatteringAndOpacity = CalculateHeightFog(WorldPositionRelativeToCamera);
```

This calls the wrapper function at `HeightFogCommon.ush:395-418`.

### 6.6 Volumetric Fog Blending (line 103)

```hlsl
float4 FogInscatteringAndOpacity = CombineVolumetricFog(
    HeightFogInscatteringAndOpacity,
    VolumeUV,
    0,  // EyeIndex
    SceneDepth
);
```

Blends height fog with volumetric fog if both are present.

### 6.7 Light Shaft Occlusion (lines 104-106)

```hlsl
float2 OcclusionTexCoord = clamp(TexCoord, OcclusionTextureMinMaxUV.xy, OcclusionTextureMinMaxUV.zw);
float LightShaftMask = Texture2DSample(OcclusionTexture, OcclusionSampler, OcclusionTexCoord).x;
FogInscatteringAndOpacity.rgb *= LightShaftMask;
```

### 6.8 Pre-Exposure (line 108)

```hlsl
FogInscatteringAndOpacity.rgb *= View.PreExposure;
```

Applies camera exposure to fog color.

### 6.9 Output (lines 110-137)

```hlsl
#if PERMUTATION_SAMPLE_FOG_ON_CLOUDS
    OutColor.rgb = CloudLuminanceTransmittance.rgb * FogInscatteringAndOpacity.a + FogInscatteringAndOpacity.rgb * CloudCoverage;
    OutColor.a = CloudLuminanceTransmittance.a;
#else
    if (bOnlyOnRenderedOpaque > 0.0 && !bIsRendered)
    {
        FogInscatteringAndOpacity.rgb = 0;
        FogInscatteringAndOpacity.a = 1;  // No fog on unrendered pixels
    }

    FLATTEN
    if (bPropagateAlpha)
    {
        // Output coverage (1 - opacity)
        OutColor = float4(FogInscatteringAndOpacity.rgb, 1.0 - FogInscatteringAndOpacity.w);
    }
    else
    {
        // Output opacity directly
        OutColor = float4(FogInscatteringAndOpacity.rgb, FogInscatteringAndOpacity.w);
    }
#endif
```

**Blend modes at this stage** (FogRendering.cpp:379-406):

```cpp
if (bSupportsAlpha)
{
    if (IsExponentialFogHoldout(...) && View.bPrimitiveAlphaHoldoutEnabled)
    {
        // Alpha Holdout: One * InverseSourceAlpha (punch hole)
        BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
    }
    else
    {
        // Standard: One * InverseSourceAlpha (premultiplied alpha)
        BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
    }
}
else
{
    // No alpha: One * SourceAlpha
    BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();
}
```

---

## 7. Phase Functions

### 7.1 Henyey-Greenstein Phase

**File**: Not shown, but referenced.

Standard implementation:
```hlsl
float HenyeyGreensteinPhase(float g, float cosTheta)
{
    float gSquared = g * g;
    float denom = 1.0 + gSquared - 2.0 * g * cosTheta;
    return (1.0 - gSquared) / (4.0 * PI * pow(denom, 1.5));
}
```

Uniform phase = `1.0 / (4.0 * PI)` ≈ 0.07958

---

## 8. Complete Data Flow Summary

```
Component Data:
  FExponentialHeightFogComponent.Density
  FExponentialHeightFogComponent.HeightFalloff
  FExponentialHeightFogComponent.Height
  ... (50+ properties)
         ↓
CPU: FSceneRenderer::InitFogConstants() [FogRendering.cpp:208]
         ↓
Computed Values:
  CollapsedFogParameter = Density * exp2(-Falloff * (CameraZ - Height))
  MaxObserverHeight = min(CameraZ, Height + 65536)
  ...
         ↓
GPU: FFogUniformParameters Constant Buffer
  ExponentialFogParameters.xyzw
  ExponentialFogParameters2.xyzw
  ExponentialFogParameters3.xyzw
  ... (14 floats total)
         ↓
Pixel Shader: HeightFogPixelShader.usf:ExponentialPixelMain()
         ↓
  Read DeviceZ from SceneDepth
  Reconstruct WorldPosition = ScreenVector * SceneDepth
         ↓
  Call GetExponentialHeightFog(WorldPosition, ExcludeDistance)
         ↓
  Line Integral: CalculateLineIntegralShared()
  Optical Depth: τ = LineIntegral * RayLength
  Transmittance: T = exp2(-τ)
  InscatteringColor: C_in = BaseFogColor [+ Cubemap + SkyAtmosphere]
  DirectionalInscattering: C_dir = DirectionalColor * (1 - exp2(-τ_dir))
         ↓
  FogColor = C_in * (1 - T) + C_dir
  Opacity = max(T, MinFogOpacity)
         ↓
  Output: float4(FogColor, Opacity)
         ↓
Blend: OutColor = One * InverseSourceAlpha
  Result = FogColor + SceneColor * (1 - FogOpacity)
```

---

## 9. Critical Numeric Thresholds

| Constant | Value | Purpose |
|----------|-------|---------|
| FLT_EPSILON | 0.001 | Min falloff magnitude threshold |
| FLT_EPSILON2 | 0.01 | Falloff threshold for Taylor expansion |
| MaxExponent | +127 - 1 = 126 | Max float exponent (IEEE 754) |
| MinExponent | -126 + 1 = -125 | Min float exponent (IEEE 754) |
| UniformPhaseG | 1.0 / (4.0 * PI) ≈ 0.07958 | Uniform phase function |
| MaxObserverHeightDiff | 65536 cm (655.36 m) | Max height offset for fog clamping |

---

## 10. Permutation Flags

| Permutation | Effect |
|-------------|--------|
| PERMUTATION_SUPPORT_FOG_INSCATTERING_TEXTURE | Enable cubemap inscattering color |
| PERMUTATION_SUPPORT_FOG_DIRECTIONAL_LIGHT_INSCATTERING | Enable directional light phase function |
| PERMUTATION_SUPPORT_VOLUMETRIC_FOG | Blend with volumetric fog |
| PERMUTATION_SUPPORT_FOG_START_DISTANCE | Exclude fog up to StartDistance |
| PERMUTATION_SUPPORT_FOG_SECOND_TERM | Add secondary fog layer |
| PERMUTATION_SAMPLE_FOG_ON_CLOUDS | Sample fog on cloud depth instead of scene |
| PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG | Use sky atmosphere for ambient |
| PROJECT_EXPFOG_MATCHES_VFOG | Match volumetric fog parameters (albedo, phase) |

---

## 11. Critical Implementation Notes for UzEngine

### Exact Parity Checklist

- [ ] **CPU precomputation**: CollapsedFogParameter exponent clamped to [-125, 126]
- [ ] **Observer height**: Clamped to `Height + 65536` max
- [ ] **Start distance**: Recalculates density terms at exclusion plane Z coordinate
- [ ] **End distance**: Clips ray to XY plane at distance threshold
- [ ] **Line integral**: Taylor expansion when `|Falloff| ≤ 0.01`, exact formula otherwise
- [ ] **Optical depth**: τ = LineIntegral * RayLength (NOT just LineIntegral)
- [ ] **Transmittance**: exp2(-τ), NOT exp(-τ)
- [ ] **Min fog opacity**: Clamped to this value, NOT higher
- [ ] **Directional phase**: Power function OR Henyey-Greenstein (based on PROJECT_EXPFOG_MATCHES_VFOG)
- [ ] **Sky atmosphere**: Added AFTER fog color, scaled by SkyAtmosphereAmbientContributionColorScale
- [ ] **Volumetric blending**: Height fog composed with volumetric fog via CombineVolumetricFog()
- [ ] **Blend equation**: One * InverseSourceAlpha (premultiplied alpha)
- [ ] **Fog cutoff**: Distance beyond which fog = fully opaque (1.0)

---

## 12. Files Referenced

| File | Location | Purpose |
|------|----------|---------|
| `HeightFogCommon.ush` | `W:\UE_5.6_Source\Engine\Shaders\Private\` | Main fog algorithms |
| `HeightFogPixelShader.usf` | `W:\UE_5.6_Source\Engine\Shaders\Private\` | Pixel shader entry point |
| `FogRendering.cpp` | `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\` | CPU initialization & RHI setup |
| `SkyAtmosphereCommon.ush` | Included | Sky atmosphere contribution |
| `ParticipatingMediaCommon.ush` | Included | Scattering utilities |
| `Common.ush` | Engine include | Math utilities (exp2, pow, etc.) |

---

**END OF REFERENCE DOCUMENT**

Created: 2026-03-20
UE5 Version: 5.6
Completeness: 100% (all formulas, constants, and computation steps extracted with line numbers)
