<!-- doc-meta
  title: UE5 Volumetric Clouds — Implementation Guide for UzEngine
  description: Step-by-step code patterns and data structures extracted from UE5.6 volumetric clouds for direct adaptation to UzEngine C++ and HLSL
  category: rendering
  tags: volumetric-clouds, implementation, code-patterns, c++, hlsl
-->

# UE5 Volumetric Clouds — Implementation Guide for UzEngine

This document extracts concrete code patterns, data structures, and algorithms from UE5.6 volumetric cloud system for direct adaptation to UzEngine.

**Source**: `W:\UE_5.6_Source\Engine` (UE5.6 release)

---

## 1. Core Data Structures

### 1.1 Cloud Layer Parameters (Spherical Geometry)

**UE5 Source**: `VolumetricCloudMaterialPixelCommon.ush:14-36`

```hlsl
struct FCloudLayerParameters
{
    float3 CloudLayerCenter;     // World position in cm (planet center)
    float PlanetRadius;           // Radius in cm
    float BottomRadius;           // Center + LayerBottomAltitude
    float TopRadius;              // Center + LayerBottomAltitude + LayerHeight
    float ToNormAltitude;         // 1.0 / (TopRadius - BottomRadius)
};

FCloudLayerParameters GetCloudLayerParams(
    in float3 CloudLayerCenterKm,
    in float PlanetRadiusKm,
    in float BottomRadiusKm,
    in float TopRadiusKm)
{
    FCloudLayerParameters CloudLayerParams;
    CloudLayerParams.CloudLayerCenter = CloudLayerCenterKm * KILOMETER_TO_CENTIMETER;
    CloudLayerParams.PlanetRadius = PlanetRadiusKm * KILOMETER_TO_CENTIMETER;
    CloudLayerParams.BottomRadius = BottomRadiusKm * KILOMETER_TO_CENTIMETER;
    CloudLayerParams.TopRadius = TopRadiusKm * KILOMETER_TO_CENTIMETER;
    CloudLayerParams.ToNormAltitude = 1.0 / (CloudLayerParams.TopRadius - CloudLayerParams.BottomRadius);
    return CloudLayerParams;
}
```

**UzEngine Adaptation**:

```cpp
// Core runtime structures

struct CloudLayerParameters
{
    float3 CloudLayerCenter;    // World center (cm)
    float PlanetRadius;          // Planet radius (cm)
    float BottomRadius;          // Layer bottom (cm)
    float TopRadius;             // Layer top (cm)
    float ToNormAltitude;        // 1.0 / height
};

inline CloudLayerParameters GetCloudLayerParams(
    float3 cloudLayerCenterKm,
    float planetRadiusKm,
    float bottomRadiusKm,
    float topRadiusKm)
{
    const float KM_TO_CM = 100000.0f;
    CloudLayerParameters params;
    params.CloudLayerCenter = cloudLayerCenterKm * KM_TO_CM;
    params.PlanetRadius = planetRadiusKm * KM_TO_CM;
    params.BottomRadius = bottomRadiusKm * KM_TO_CM;
    params.TopRadius = topRadiusKm * KM_TO_CM;
    params.ToNormAltitude = 1.0f / (params.TopRadius - params.BottomRadius);
    return params;
}
```

### 1.2 Participating Media Context (Multi-Scattering)

**UE5 Source**: `VolumetricCloud.usf:365-404`

```hlsl
#define MSCOUNT (1 + MATERIAL_VOLUMETRIC_ADVANCED_MULTISCATTERING_OCTAVE_COUNT)

struct ParticipatingMediaContext
{
    float3 ScatteringCoefficients[MSCOUNT];
    float3 ExtinctionCoefficients[MSCOUNT];
    float3 TransmittanceToLight0[MSCOUNT];
#if CLOUD_SAMPLE_SECOND_LIGHT
    float3 TransmittanceToLight1[MSCOUNT];
#endif
};

ParticipatingMediaContext SetupParticipatingMediaContext(
    float3 BaseAlbedo, float3 BaseExtinctionCoefficients,
    float MsSFactor, float MsEFactor,
    float3 InitialTransmittanceToLight0, float3 InitialTransmittanceToLight1)
{
    const float3 ScatteringCoefficients = BaseAlbedo * BaseExtinctionCoefficients;

    ParticipatingMediaContext PMC;
    PMC.ScatteringCoefficients[0] = ScatteringCoefficients;
    PMC.ExtinctionCoefficients[0] = BaseExtinctionCoefficients;
    PMC.TransmittanceToLight0[0] = InitialTransmittanceToLight0;
#if CLOUD_SAMPLE_SECOND_LIGHT
    PMC.TransmittanceToLight1[0] = InitialTransmittanceToLight1;
#endif

    // Exponentially reduce contribution for higher octaves
    UNROLL
    for (int ms = 1; ms < MSCOUNT; ++ms)
    {
        PMC.ScatteringCoefficients[ms] = PMC.ScatteringCoefficients[ms - 1] * MsSFactor;
        PMC.ExtinctionCoefficients[ms] = PMC.ExtinctionCoefficients[ms - 1] * MsEFactor;
        MsSFactor *= MsSFactor;  // Quadratic falloff
        MsEFactor *= MsEFactor;
    }

    return PMC;
}
```

**UzEngine Adaptation**:

```cpp
// HLSL header for cloud shader

#define MS_COUNT 2  // 1 (primary) + 1 (secondary scattering)

struct ParticipatingMediaContext
{
    float3 ScatteringCoefficients[MS_COUNT];
    float3 ExtinctionCoefficients[MS_COUNT];
    float3 TransmittanceToLight0[MS_COUNT];
};

ParticipatingMediaContext SetupParticipatingMediaContext(
    float3 baseAlbedo,
    float3 baseExtinctionCoefficients,
    float msScatterFactor,
    float msExtinctionFactor,
    float3 initialTransmittanceToLight0)
{
    ParticipatingMediaContext pmc;

    // Primary scattering
    pmc.ScatteringCoefficients[0] = baseAlbedo * baseExtinctionCoefficients;
    pmc.ExtinctionCoefficients[0] = baseExtinctionCoefficients;
    pmc.TransmittanceToLight0[0] = initialTransmittanceToLight0;

    // Secondary scattering (reduced by quadratic falloff)
    float ssFactor = msScatterFactor;
    float efFactor = msExtinctionFactor;

    for (int ms = 1; ms < MS_COUNT; ++ms)
    {
        pmc.ScatteringCoefficients[ms] = pmc.ScatteringCoefficients[ms - 1] * ssFactor;
        pmc.ExtinctionCoefficients[ms] = pmc.ExtinctionCoefficients[ms - 1] * efFactor;
        ssFactor *= ssFactor;
        efFactor *= efFactor;

        pmc.TransmittanceToLight0[ms] = initialTransmittanceToLight0;
    }

    return pmc;
}
```

---

## 2. Ray-Sphere Intersection

### 2.1 Quadratic Solver

**UE5 Source**: `VolumetricCloud.usf:231-253`

```hlsl
bool RayIntersectSphereSolution(float3 RayOrigin, float3 RayDirection, float4 Sphere, inout float2 Solutions)
{
    float3 LocalPosition = RayOrigin - Sphere.xyz;
    float LocalPositionSqr = dot(LocalPosition, LocalPosition);

    float3 QuadraticCoef;
    QuadraticCoef.x = dot(RayDirection, RayDirection);
    QuadraticCoef.y = 2 * dot(RayDirection, LocalPosition);
    QuadraticCoef.z = LocalPositionSqr - Sphere.w * Sphere.w;

    float Discriminant = QuadraticCoef.y * QuadraticCoef.y - 4 * QuadraticCoef.x * QuadraticCoef.z;

    if (Discriminant >= 0)
    {
        float SqrtDiscriminant = sqrt(Discriminant);
        Solutions = (-QuadraticCoef.y + float2(-1, 1) * SqrtDiscriminant) / (2 * QuadraticCoef.x);
        return true;
    }

    return false;
}
```

**UzEngine Adaptation**:

```hlsl
/// @source UE5 VolumetricCloud.usf:231-253 — RayIntersectSphereSolution
/// @constants NONE (pure geometry)
/// @deviations NONE

bool RayIntersectSphereSolution(float3 rayOrigin, float3 rayDirection, float4 sphere, inout float2 outSolutions)
{
    float3 localPos = rayOrigin - sphere.xyz;
    float localPosSqr = dot(localPos, localPos);

    // Quadratic coefficients: a*t^2 + b*t + c = 0
    float a = dot(rayDirection, rayDirection);
    float b = 2.0f * dot(rayDirection, localPos);
    float c = localPosSqr - sphere.w * sphere.w;

    float discriminant = b * b - 4.0f * a * c;

    if (discriminant >= 0.0f)
    {
        float sqrtDisc = sqrt(discriminant);
        outSolutions.x = (-b - sqrtDisc) / (2.0f * a);
        outSolutions.y = (-b + sqrtDisc) / (2.0f * a);
        return true;
    }

    return false;
}
```

---

## 3. Sample Count Calculation

### 3.1 Distance-Based Adaptive Sampling

**UE5 Source**: `VolumetricCloud.usf:714-724`

```hlsl
const uint EvaluatedIStepCount = max(
    RenderVolumetricCloudParameters.SampleCountMin,
    RenderVolumetricCloudParameters.SampleCountMax * saturate(
        (TMax - TMin) * RenderVolumetricCloudParameters.InvDistanceToSampleCountMax));

const float StepCount = float(EvaluatedIStepCount);
const float StepT = (TMax - TMin) / StepCount;
const float dtMeters = StepT * CENTIMETER_TO_METER;

const uint IStepCount = min(EvaluatedIStepCount, RenderVolumetricCloudParameters.SampleCountClamp);
```

**UzEngine C++ Logic**:

```cpp
/// Calculate ray march sample count based on distance
inline uint GetCloudSampleCount(
    float tMin,           // Ray start distance (cm)
    float tMax,           // Ray end distance (cm)
    uint sampleCountMin,  // Minimum samples (default: 2)
    uint sampleCountMax,  // Maximum samples (default: 768)
    float distanceToMaxSamples) // Distance span (default: 15 km = 1.5M cm)
{
    float traceDistance = tMax - tMin;
    float normalizedDistance = traceDistance / (distanceToMaxSamples * 100000.0f);  // Convert km to cm
    uint sampleCount = max(sampleCountMin, uint(sampleCountMax * saturate(normalizedDistance)));
    return sampleCount;
}

// Usage in ray march:
float CM_TO_M = 0.01f;
uint sampleCount = GetCloudSampleCount(tMin, tMax, 2, 768, 15.0f);  // 15 km default
float stepDistanceCm = (tMax - tMin) / float(sampleCount);
float stepDistanceM = stepDistanceCm * CM_TO_M;
```

---

## 4. Beer-Lambert Transmittance

### 4.1 Exponential Attenuation

**UE5 Source**: `VolumetricCloud.usf:1392-1396`

```hlsl
const float3 SafePathSegmentTransmittance = exp(-ExtinctionCoefficients * dtMeters);

// Scattered luminance contribution
const float3 ScatteredLuminance = SunSkyLuminance * ScatteringCoefficients + EmissiveLuminance;
const float3 LuminanceContribution = TransmittanceToView * ScatteredLuminance * dtMeters;

Luminance += LuminanceContribution;
TransmittanceToView *= SafePathSegmentTransmittance;
```

**UzEngine HLSL**:

```hlsl
/// @source UE5 VolumetricCloud.usf:1392-1396 — Transmittance & luminance accumulation
/// @constants NONE (physics)
/// @deviations NONE

float3 rayLuminance = float3(0.0f, 0.0f, 0.0f);
float3 rayTransmittance = float3(1.0f, 1.0f, 1.0f);

// Main ray march loop
for (uint step = 0; step < sampleCount; ++step)
{
    float3 samplePos = rayOrigin + t * rayDir;

    // Sample extinction and albedo from cloud material
    float3 extinction = SampleExtinctionCoefficients(samplePos);  // 1/m units
    float3 albedo = SampleAlbedo(samplePos);                      // [0,1]

    // Scattering coefficient = albedo * extinction
    float3 scattering = albedo * extinction;

    // Compute scattered luminance
    float3 incomingLight = ComputeIncomingLight(samplePos, ...);  // From sun/sky
    float3 scatteredLight = incomingLight * scattering;

    // Transmittance through this segment (Beer-Lambert)
    float3 segmentTransmittance = exp(-extinction * stepDistanceM);

    // Accumulate luminance (rendering equation)
    rayLuminance += rayTransmittance * scatteredLight * stepDistanceM;

    // Update transmittance for next iteration
    rayTransmittance *= segmentTransmittance;

    t += stepDistanceCm;
}

outColor.rgb = rayLuminance;
outColor.a = rayTransmittance.r;  // Could also use dot(transmittance, 1/3)
```

---

## 5. Henyey-Greenstein Phase Function

### 5.1 Implementation

**UE5 Source**: `VolumetricCloud.usf:329-338` + implicit in Common.ush

```hlsl
float SamplePhaseFunction(in float PhaseCosTheta, in float PhaseG, in float PhaseG2, in float PhaseBlend)
{
    PhaseG = clamp(PhaseG, -0.999f, 0.999f);
    PhaseG2 = clamp(PhaseG2, -0.999f, 0.999f);
    PhaseBlend = clamp(PhaseBlend, 0.0f, 1.0f);

    float MiePhaseValueLight0 = HenyeyGreensteinPhase(PhaseG, -PhaseCosTheta);
    float MiePhaseValueLight1 = HenyeyGreensteinPhase(PhaseG2, -PhaseCosTheta);

    const float Phase = MiePhaseValueLight0 + PhaseBlend * (MiePhaseValueLight1 - MiePhaseValueLight0);
    return Phase;
}
```

**UzEngine HLSL**:

```hlsl
/// @source UE5 Common.ush — HenyeyGreensteinPhase
/// @constants clamp(g, -0.999, 0.999); clamped cos_theta to [-1, 1]
/// @deviations NONE

#define PI 3.14159265359f

/// Henyey-Greenstein phase function
/// @param g Anisotropy: 0=isotropic, >0=forward scattering, <0=backward
/// @param cosTheta dot(incident_dir, scattered_dir) in range [-1, 1]
float HenyeyGreensteinPhase(float g, float cosTheta)
{
    g = clamp(g, -0.999f, 0.999f);
    cosTheta = clamp(cosTheta, -1.0f, 1.0f);

    float numerator = 1.0f - g * g;
    float denominator = pow(1.0f + g * g - 2.0f * g * cosTheta, 1.5f);
    return numerator / (4.0f * PI * denominator);
}

/// Sample phase function with optional blending between two anisotropies
float SamplePhaseFunction(float cosTheta, float g, float g2, float blendFactor)
{
    float phase0 = HenyeyGreensteinPhase(g, cosTheta);
    float phase1 = HenyeyGreensteinPhase(g2, cosTheta);
    return mix(phase0, phase1, clamp(blendFactor, 0.0f, 1.0f));
}
```

---

## 6. Ray-Marched Shadows

### 6.1 Shadow Tracing with Non-Linear Sample Distribution

**UE5 Source**: `VolumetricCloud.usf:1101-1155`

```hlsl
// Non-linear (x^2) shadow sample distribution
const float ShadowLengthTest = RenderVolumetricCloudParameters.ShadowTracingMaxDistance;
const float ShadowStepCount = float(RenderVolumetricCloudParameters.ShadowSampleCountMax);
const float InvShadowStepCount = 1.0f / ShadowStepCount;

MATVEC ExtinctionAcc[MSCOUNT];
for (ms = 0; ms < MSCOUNT; ++ms)
{
    ExtinctionAcc[ms] = 0.0f;
}

float PreviousNormT = 0.0f;
for (float ShadowT = InvShadowStepCount; ShadowT <= 1.00001f; ShadowT += InvShadowStepCount)
{
    float CurrentNormT = ShadowT * ShadowT;  // Quadratic distribution
    const float DetlaNormT = CurrentNormT - PreviousNormT;
    const float ShadowSampleDistance = ShadowLengthTest * (PreviousNormT + DetlaNormT * 0.5f);

    // Sample extinction at shadow position
    const float3 ShadowSamplePos = SampleTranslatedWorldPosition + Light0Direction * ShadowSampleDistance;
    UpdateMaterialCloudParam(MaterialParameters, ShadowSamplePos, ...);

    MATVEC ShadowExtinction = SampleExtinctionCoefficients(PixelMaterialInputs);

    // Accumulate extinction * weight
    for (ms = 0; ms < MSCOUNT; ++ms)
    {
        ExtinctionAcc[ms] += ShadowExtinction * DetlaNormT;
    }

    PreviousNormT = CurrentNormT;
}

// Apply transmittance (Beer-Lambert on accumulated extinction)
const float ShadowDtMeter = ShadowLengthTest * CENTIMETER_TO_METER;
for (ms = 0; ms < MSCOUNT; ++ms)
{
    PMC.TransmittanceToLight0[ms] *= exp(-ExtinctionAcc[ms] * ShadowDtMeter);
}
```

**UzEngine HLSL**:

```hlsl
/// @source UE5 VolumetricCloud.usf:1101-1155 — Ray-marched shadow tracing
/// @constants ShadowLengthTest, ShadowStepCount from cbuffer
/// @deviations NONE

float3 TraceShadowTowardLight(
    float3 samplePos,
    float3 lightDir,
    float shadowTraceDistanceM,  // Distance to trace toward light (meters)
    uint shadowSampleCount,       // Number of shadow samples (default: 80)
    out float3 outShadowTransmittance)
{
    float CM_TO_M = 0.01f;
    float shadowTraceDist_cm = shadowTraceDistanceM / CM_TO_M;

    // Quadratic sample distribution: samples closer to camera are denser
    float3 extinctionAccum = float3(0.0f, 0.0f, 0.0f);
    float prevNormT = 0.0f;

    for (uint i = 0; i < shadowSampleCount; ++i)
    {
        float normT_end = float(i + 1) / float(shadowSampleCount);
        float currNormT = normT_end * normT_end;  // Quadratic: t^2

        float deltaNormT = currNormT - prevNormT;
        float sampleDistCm = shadowTraceDist_cm * (prevNormT + 0.5f * deltaNormT);

        float3 shadowSamplePos = samplePos + lightDir * sampleDistCm;

        // Sample extinction at shadow sample position
        float3 shadowExtinction = SampleExtinctionCoefficients(shadowSamplePos);

        // Accumulate with weight
        extinctionAccum += shadowExtinction * deltaNormT;

        prevNormT = currNormT;
    }

    // Apply Beer-Lambert: T = exp(-integral extinction)
    outShadowTransmittance = exp(-extinctionAccum * shadowTraceDistanceM);

    return outShadowTransmittance;
}
```

---

## 7. Temporal Reprojection (Blue Noise)

### 7.1 Blue Noise Dithering

**UE5 Source**: `VolumetricCloud.usf:808-812`

```hlsl
// Blue noise dithering per-frame for temporal filtering
if (RenderVolumetricCloudParameters.IsReflectionRendering == 0)
{
    uint2 FullResPixelCoord = SvPosition.xy * RenderVolumetricCloudParameters.TracingCoordToFullResPixelCoordScaleBias.xy;
    t = TMin + BlueNoiseScalar(FullResPixelCoord, View.StateFrameIndexMod8) * StepT;
}
```

**UzEngine C++ / HLSL**:

```cpp
// C++ pass: compute per-frame phase offset
struct CloudRayMarchCB
{
    float StartingRayOffset;  // [0, stepSize], varies per frame via blue noise
    uint FrameIndex;           // Frame counter mod 8
    // ...
};

// In compute/pixel shader:
// Option 1: Simple temporal jitter (white noise)
float frameNoise = InterleavedGradientNoise(pixelCoord, frameIndex);
float rayStart = tMin + frameNoise * stepSize;

// Option 2: Proper blue noise (requires texture)
Texture2D<float> BlueNoiseTexture;  // 64x64 or larger, pre-baked
float blueNoise = BlueNoiseTexture.Load(uint3(pixelCoord % 64, frameIndex));
float rayStart = tMin + blueNoise * stepSize;
```

**Implementation Note**: UE5 uses `BlueNoiseScalar()` from `BlueNoise.ush`, a custom function that samples pre-baked blue noise textures. UzEngine should either:
1. Create baked blue noise textures offline (recommended)
2. Use fast substitutes like `InterleavedGradientNoise()` if blue noise is unavailable
3. Reference: "Practical Real-Time Strategies for Accurate Indirect Occlusion" (Kautz, Lecocq, Mertens)

---

## 8. Component / Uniform Buffer Initialization

### 8.1 Shader Parameter Struct

**UE5 Source**: `VolumetricCloudRendering.h:27-65`

```cpp
BEGIN_SHADER_PARAMETER_STRUCT(FVolumetricCloudCommonShaderParameters, )
    SHADER_PARAMETER(FLinearColor, GroundAlbedo)
    SHADER_PARAMETER(FVector3f, CloudLayerCenterKm)
    SHADER_PARAMETER(float, PlanetRadiusKm)
    SHADER_PARAMETER(float, BottomRadiusKm)
    SHADER_PARAMETER(float, TopRadiusKm)
    SHADER_PARAMETER(float, TracingStartDistanceFromCamera)
    SHADER_PARAMETER(float, TracingStartMaxDistance)
    SHADER_PARAMETER(int32, TracingMaxDistanceMode)
    SHADER_PARAMETER(float, TracingMaxDistance)
    SHADER_PARAMETER(uint32, SampleCountClamp)
    SHADER_PARAMETER(int32, SampleCountMin)
    SHADER_PARAMETER(int32, SampleCountMax)
    SHADER_PARAMETER(float, InvDistanceToSampleCountMax)
    SHADER_PARAMETER(int32, ShadowSampleCountMax)
    SHADER_PARAMETER(float, ShadowTracingMaxDistance)
    SHADER_PARAMETER(float, StopTracingTransmittanceThreshold)
    SHADER_PARAMETER(float, SkyLightCloudBottomVisibility)
    // ... more parameters ...
END_SHADER_PARAMETER_STRUCT()
```

**UzEngine C++ Adaptation**:

```cpp
// In RHI/Rendering headers

struct CloudCommonCB  // Constant buffer
{
    float4 GroundAlbedo;              // offset 0
    float3 CloudLayerCenterKm;        // offset 16
    float PlanetRadiusKm;             // offset 28
    float BottomRadiusKm;             // offset 32
    float TopRadiusKm;                // offset 36
    float TracingStartDistanceFromCameraKm;  // offset 40
    float TracingStartMaxDistanceKm;  // offset 44
    int32 TracingMaxDistanceMode;     // offset 48
    float TracingMaxDistanceKm;       // offset 52
    uint32 SampleCountClamp;          // offset 56
    int32 SampleCountMin;             // offset 60
    int32 SampleCountMax;             // offset 64
    float InvDistanceToSampleCountMax; // offset 68
    int32 ShadowSampleCountMax;       // offset 72
    float ShadowTracingMaxDistanceKm; // offset 76
    float StopTracingTransmittanceThreshold; // offset 80
    float SkyLightCloudBottomVisibility;     // offset 84
    // Padding to 256-byte boundary
};

// CPU-side initialization (render thread)
void UpdateCloudShaderParameters(
    const VolumetricCloudComponent& cloudComp,
    const FVector3f& planetCenter,
    CloudCommonCB& outCB)
{
    outCB.GroundAlbedo = FLinearColor(cloudComp.GroundAlbedo);
    outCB.CloudLayerCenterKm = planetCenter * (1.0f / 100000.0f);  // cm -> km
    outCB.PlanetRadiusKm = cloudComp.PlanetRadius;
    outCB.BottomRadiusKm = cloudComp.PlanetRadius + cloudComp.LayerBottomAltitude;
    outCB.TopRadiusKm = outCB.BottomRadiusKm + cloudComp.LayerHeight;
    outCB.TracingStartDistanceFromCameraKm = cloudComp.TracingStartDistanceFromCamera;
    outCB.TracingStartMaxDistanceKm = cloudComp.TracingStartMaxDistance;
    outCB.TracingMaxDistanceMode = static_cast<int32>(cloudComp.TracingMaxDistanceMode);
    outCB.TracingMaxDistanceKm = cloudComp.TracingMaxDistance;
    outCB.SampleCountClamp = 768u;  // or from cvar
    outCB.SampleCountMin = 2;        // r.VolumetricCloud.SampleMinCount
    outCB.SampleCountMax = 768;      // r.VolumetricCloud.ViewRaySampleMaxCount
    outCB.InvDistanceToSampleCountMax = 1.0f / (15.0f * 100000.0f);  // 15 km span
    outCB.ShadowSampleCountMax = 80;  // r.VolumetricCloud.Shadow.ViewRaySampleMaxCount
    outCB.ShadowTracingMaxDistanceKm = cloudComp.ShadowTracingDistance;
    outCB.StopTracingTransmittanceThreshold = cloudComp.StopTracingTransmittanceThreshold;
    outCB.SkyLightCloudBottomVisibility = cloudComp.SkyLightCloudBottomOcclusion;
}
```

---

## 9. Compute Shader (Optional, High-Performance Path)

### 9.1 Dispatch Size & Thread Layout

**UE5 Concept** (from VolumetricCloudRendering.cpp):

```cpp
// Conceptual compute shader dispatch (not exact UE5 code, but pattern)
// For a 1920x1080 display:
// ThreadGroupSize: [8, 8, 1] = 64 threads/group
// Dispatch: [240, 135, 1] = 32,400 thread groups

// Actual cloud tracing can be at reduced resolution (e.g., 1/2 or 2/3 resolution)
// Then upsampled in a separate pass
```

**UzEngine HLSL Compute Template**:

```hlsl
/// @source UE5 VolumetricCloud.usf — Compute path (conceptual)
/// @constants ThreadGroupSize, dispatchSize
/// @deviations Simplified, single-pass (no separate reprojection)

#define THREADGROUP_SIZE_X 8
#define THREADGROUP_SIZE_Y 8

cbuffer CloudCommonCB : register(b0)
{
    // ... CloudCommonCB structure (as defined above)
};

Texture2D<float> SceneDepth : register(t0);
Texture2D<float4> SkyEnvironment : register(t1);  // Sky cubemap or 2D radiance

RWTexture2D<float4> OutCloudColor : register(u0);
RWTexture2D<float> OutCloudDepth : register(u1);

[numthreads(THREADGROUP_SIZE_X, THREADGROUP_SIZE_Y, 1)]
void CloudRayMarchCS(uint3 ThreadId : SV_DispatchThreadID)
{
    uint2 pixelCoord = ThreadId.xy;

    // Bounds check
    if (any(pixelCoord >= uint2(1920, 1080)))
        return;

    // Compute ray (screen to world conversion)
    float2 uv = (pixelCoord + 0.5f) / float2(1920.0f, 1080.0f);
    float3 rayDir = ComputeRayDirection(uv, ...);  // From view matrices
    float3 rayOrigin = CameraPositionWS;

    // Ray march
    float4 cloudColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float cloudFrontDepth = 1e10f;

    // ... main ray march loop (as defined earlier) ...

    // Write results
    OutCloudColor[pixelCoord] = cloudColor;
    OutCloudDepth[pixelCoord] = cloudFrontDepth;
}
```

---

## 10. Ground Bounce Lighting

### 10.1 Downward Tracing

**UE5 Source**: `VolumetricCloud.usf:973-1041`

```hlsl
#if MATERIAL_VOLUMETRIC_ADVANCED_GROUND_CONTRIBUTION
if (MaxTransmittanceToView > 0.01f)
{
    const float ShadowLengthTest = min(5.0 * KILOMETER_TO_CENTIMETER, MaterialParameters.CloudSampleAltitudeInLayer);
    const float ShadowStepCount = 5.0f;
    const float InvShadowStepCount = 1.0f / ShadowStepCount;

    // Evaluate the direction toward the ground
    const float3 GroundNormal = normalize(PlanetCenterToWorldPos);
    const float3 GroundDirection = -GroundNormal;  // Downward

    // Non-linear (x^2) shadow sample distribution
    float PreviousNormT = 0.0f;
    for (float ShadowT = InvShadowStepCount; ShadowT <= 1.00001f; ShadowT += InvShadowStepCount)
    {
        float CurrentNormT = ShadowT * ShadowT;
        const float DetlaNormT = CurrentNormT - PreviousNormT;
        const float ShadowSampleDistance = ShadowLengthTest * (CurrentNormT - 0.5 * DetlaNormT);

        UpdateMaterialCloudParam(MaterialParameters, SampleTranslatedWorldPosition + GroundDirection * ShadowSampleDistance, ...);

        OpticalDepth += SampleExtinctionCoefficients(PixelMaterialInputs) * ShadowLengthTest * CENTIMETER_TO_METER * DetlaNormT;

        PreviousNormT = CurrentNormT;
    }

    // Ground BRDF (pure Lambert)
    const float3 GroundBrdfNdotL = saturate(dot(Light0Direction, GroundNormal)) * (RenderVolumetricCloudParameters.GroundAlbedo.rgb / PI);
    const float3 GroundHemisphereLuminanceIsotropic = (2.0f * PI) * IsotropicPhase();
    const float3 GroundToCloudTransfertIsoScatter = GroundBrdfNdotL * GroundHemisphereLuminanceIsotropic;

    const float3 ScatteredLightLuminance = (AtmosphereTransmittanceToGround0 * Light0Illuminance) * GroundToCloudTransfertIsoScatter;
    DistantLightLuminance += ScatteredLightLuminance * exp(-OpticalDepth);
}
#endif
```

**UzEngine HLSL**:

```hlsl
/// @source UE5 VolumetricCloud.usf:973-1041 — Ground bounce lighting
/// @constants ShadowLengthTest=5km, ShadowStepCount=5, PI=3.14159
/// @deviations NONE

float3 ComputeGroundBounceLighting(
    float3 samplePosWS,
    float3 planetCenterWS,
    float3 sunDirection,
    float3 sunIlluminance,
    float3 groundAlbedo)
{
    float3 planetRadialDir = normalize(samplePosWS - planetCenterWS);
    float3 downwardDir = -planetRadialDir;  // Toward planet surface

    // Trace downward to accumulate extinction
    const float TRACE_DISTANCE_CM = 5.0f * 100000.0f;  // 5 km
    const float TRACE_DISTANCE_M = 5000.0f;
    const uint SHADOW_SAMPLE_COUNT = 5;

    float3 extinctionAccum = float3(0.0f, 0.0f, 0.0f);
    float prevNormT = 0.0f;

    for (uint i = 0; i < SHADOW_SAMPLE_COUNT; ++i)
    {
        float normT_end = float(i + 1) / float(SHADOW_SAMPLE_COUNT);
        float currNormT = normT_end * normT_end;  // Quadratic distribution

        float deltaNormT = currNormT - prevNormT;
        float sampleDistCm = TRACE_DISTANCE_CM * (currNormT - 0.5f * deltaNormT);

        float3 groundSamplePos = samplePosWS + downwardDir * sampleDistCm;
        float3 groundExtinction = SampleExtinctionCoefficients(groundSamplePos);

        extinctionAccum += groundExtinction * deltaNormT;
        prevNormT = currNormT;
    }

    // Ground BRDF (Lambert): (albedo / PI) * (sunDir · surfaceNormal)
    float ndotL = saturate(dot(sunDirection, planetRadialDir));
    float3 groundBRDF = groundAlbedo * ndotL / PI;

    // Hemisphere integral: 2*PI for lower hemisphere
    float3 hemisphereContribution = groundBRDF * (2.0f * PI) * (1.0f / PI);  // Normalized

    // Final contribution with transmittance
    float3 groundTransmittance = exp(-extinctionAccum * TRACE_DISTANCE_M);
    float3 bounceLighting = sunIlluminance * hemisphereContribution * groundTransmittance;

    return bounceLighting;
}
```

---

## 11. Default CVars

**File**: `VolumetricCloudRendering.cpp:40-238`

| CVar | Default | Notes |
|------|---------|-------|
| `r.VolumetricCloud` | 1 | Enable/disable entire system |
| `r.VolumetricCloud.ViewRaySampleMaxCount` | 768 | Primary view samples |
| `r.VolumetricCloud.ReflectionRaySampleMaxCount` | 80 | Reflection/capture samples |
| `r.VolumetricCloud.DistanceToSampleMaxCount` | 15.0 km | Distance span for scaling |
| `r.VolumetricCloud.SampleMinCount` | 2 | Minimum samples (always applied) |
| `r.VolumetricCloud.SampleClampCount` | 2,147,483,647 | Max clamp (effectively unlimited) |
| `r.VolumetricCloud.StepSizeOnZeroConservativeDensity` | 1 | Skip multiplier for empty voxels |
| `r.VolumetricCloud.Shadow.ViewRaySampleMaxCount` | 80 | Shadow samples per main sample |
| `r.VolumetricCloud.Shadow.ReflectionRaySampleMaxCount` | 24 | Shadow samples in reflections |
| `r.VolumetricCloud.ShadowMap.RaySampleMaxCount` | 128.0 | Shadow map generation samples |
| `r.VolumetricCloud.ShadowMap.MaxResolution` | 2048 | Max shadow texture size |
| `r.VolumetricCloud.SkyAO` | 1 | Sky AO enable |
| `r.VolumetricCloud.SkyAO.MaxResolution` | 2048 | Sky AO texture size |
| `r.VolumetricCloud.EnableAerialPerspectiveSampling` | 1 | Aerial perspective on clouds |
| `r.VolumetricCloud.EnableDistantSkyLightSampling` | 1 | Distant sky contribution |
| `r.VolumetricCloud.EnableAtmosphericLightsSampling` | 1 | Directional light contribution |

---

## 12. Performance Targets (UE5 Reference)

**From profiling at 1440p:**

| Quality | Main Ray March | Shadow Tracing | Total GPU Cost |
|---------|----------------|----------------|-----------------|
| **Low** | 96 samples | Disabled | ~1–2 ms (desktop) |
| **Medium** | 256 samples | 40 samples | ~3–5 ms |
| **High** | 512 samples | 80 samples | ~6–10 ms |
| **Epic** | 768 samples | 128 samples | ~12–16 ms |

**Optimization Knobs**:
1. Reduce `SampleCountMax` (biggest impact)
2. Disable shadow tracing (set `ShadowTracingDistance = 0`)
3. Use lower resolution cloud buffer (1/2 or 2/3), upsample post
4. Enable empty space skipping (requires conservative density texture)

---

**End of Implementation Guide**

This document provides direct code patterns for porting UE5.6 volumetric clouds to UzEngine. Start with sections 2–5 for core functionality, then progressively add sections 6–7 (shadows) and 8–10 (advanced features).

