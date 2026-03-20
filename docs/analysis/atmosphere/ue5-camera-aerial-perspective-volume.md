<!-- doc-meta: UE5 CameraAerialPerspectiveVolume System Analysis -->

# UE5 CameraAerialPerspectiveVolume System Analysis

Complete technical specification of UE5's camera-relative aerial perspective volumetric system for atmosphere rendering. All numeric constants, formulas, and data flows are from UE5.6 source.

**Source Files:**
- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf` (compute shader)
- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush` (sampling function)
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp` (CPU setup)

---

## 1. Texture Format & Dimensions

### 1.1 3D Texture Specification

**Format:** `TextureAerialLUTFormat` (determined by `GetSkyLutTextureFormat()`)
- **Desktop**: `PF_FloatRGBA` (float32 RGBA)
- **Mobile**: `PF_FloatRGBA` (same as desktop)

**Layout:** 3 dimensions, all square XY, variable Z depth slices

```cpp
// SkyAtmosphereRendering.cpp:1233-1235
FPooledRenderTargetDesc SkyAtmosphereCameraAerialPerspectiveVolumeDesc =
    FPooledRenderTargetDesc::CreateVolumeDesc(
        CameraAerialPerspectiveVolumeScreenResolution,
        CameraAerialPerspectiveVolumeScreenResolution,
        CameraAerialPerspectiveVolumeDepthResolution,
        TextureAerialLUTFormat,
        FClearValueBinding::None,
        TexCreate_None,
        TexCreate_ShaderResource | TexCreate_UAV,
        false);
```

### 1.2 Resolution Parameters

**CVars controlling dimensions:**

```cpp
// SkyAtmosphereRendering.cpp:279-283
int32 CameraAerialPerspectiveVolumeScreenResolution =
    ValidateLUTResolution(CVarSkyAtmosphereAerialPerspectiveLUTWidth.GetValueOnRenderThread());
int32 CameraAerialPerspectiveVolumeDepthResolution =
    ValidateLUTResolution(CVarSkyAtmosphereAerialPerspectiveLUTDepthResolution.GetValueOnRenderThread());
float CameraAerialPerspectiveVolumeDepthKm =
    CVarSkyAtmosphereAerialPerspectiveLUTDepth.GetValueOnRenderThread();
CameraAerialPerspectiveVolumeDepthKm =
    CameraAerialPerspectiveVolumeDepthKm < 1.0f ? 1.0f : CameraAerialPerspectiveVolumeDepthKm;
    // 1 kilometer minimum
float CameraAerialPerspectiveVolumeDepthSliceLengthKm =
    CameraAerialPerspectiveVolumeDepthKm / CameraAerialPerspectiveVolumeDepthResolution;
```

**Typical defaults (from engine console variables):**
- `CVarSkyAtmosphereAerialPerspectiveLUTWidth`: 32 (XY resolution = 32×32 pixels)
- `CVarSkyAtmosphereAerialPerspectiveLUTDepthResolution`: 16 (Z depth slices = 16 layers)
- `CVarSkyAtmosphereAerialPerspectiveLUTDepth`: 100 km (total depth range from camera)

This yields a **32×32×16** texture with each slice covering **100 km / 16 = 6.25 km depth**.

### 1.3 SizeAndInvSize Parameter

```cpp
// SkyAtmosphereRendering.cpp:1329, 407
// GetSizeAndInvSize = [](int32 Width, int32 Height) {
//   return FVector4f(Width, Height, 1.0f/Width, 1.0f/Height);
// }
OutParameters.CameraAerialPerspectiveVolumeSizeAndInvSize =
    GetSizeAndInvSize(CameraAerialPerspectiveVolumeScreenResolution,
                      CameraAerialPerspectiveVolumeScreenResolution);

// For 32×32: FVector4f(32.0, 32.0, 0.03125, 0.03125)
```

---

## 2. Depth Distribution (Voxel to Distance Mapping)

### 2.1 The Critical Squared Distribution Formula

**UE5 does NOT use linear depth distribution.** It uses **squared (power-of-2) distribution** to pack more detail near the camera and sparse detail far away.

```hlsl
// SkyAtmosphere.usf:1504-1506 (compute shader)
float Slice = ((float(ThreadId.z) + 0.5f) * SkyAtmosphere.CameraAerialPerspectiveVolumeDepthResolutionInv);
// +0.5 to always have a distance to integrate over
Slice *= Slice;  // SQUARED DISTRIBUTION
Slice *= SkyAtmosphere.CameraAerialPerspectiveVolumeDepthResolution;

// Result: Slice is in range [0, DepthResolution) in squared space
```

**Reverse mapping (sampling from base pass):**

```hlsl
// SkyAtmosphereCommon.ush:69-72
float LinearSlice = tDepth * AerialPerspectiveVolumeDepthSliceLengthKmInv;
float LinearW = LinearSlice * AerialPerspectiveVolumeDepthResolutionInv;
float NonLinW = sqrt(LinearW);  // INVERSE: sqrt for squared distribution
float NonLinSlice = NonLinW * AerialPerspectiveVolumeDepthResolution;
```

### 2.2 Mathematical Breakdown

For a 16-slice texture with 100 km total depth:

**Compute (forward) direction:**
```
For Z thread index (0 to 15):
  Slice = (Z + 0.5) * (1.0/16) = (Z + 0.5) * 0.0625       [linear: 0 to 1]
  Slice = Slice * Slice                                    [squared: 0 to 1]
  Slice = Slice * 16                                       [scaled: 0 to 16]
  Distance_km = Slice * (100/16) = Slice * 6.25 km

Z=0:  Slice_linear=0.03125  →  Slice_squared=0.00098  →  Slice_final=0.0156  →  Distance=0.0977 km (98 m)
Z=1:  Slice_linear=0.09375  →  Slice_squared=0.00879  →  Slice_final=0.1406  →  Distance=0.879 km
Z=8:  Slice_linear=0.53125  →  Slice_squared=0.28223  →  Slice_final=4.512   →  Distance=28.2 km
Z=15: Slice_linear=0.96875  →  Slice_squared=0.93835  →  Slice_final=15.013  →  Distance=93.8 km
```

Near-field slices cluster tightly; far-field slices spread out. This matches screen-space fogging expectations.

### 2.3 Shader Parameters for Sampling

**Passed to shaders as constants:**

```cpp
// SkyAtmosphereRendering.cpp:1289-1292
InternalCommonParameters.CameraAerialPerspectiveVolumeDepthResolution =
    float(CameraAerialPerspectiveVolumeDepthResolution);
InternalCommonParameters.CameraAerialPerspectiveVolumeDepthResolutionInv =
    1.0f / InternalCommonParameters.CameraAerialPerspectiveVolumeDepthResolution;
InternalCommonParameters.CameraAerialPerspectiveVolumeDepthSliceLengthKm =
    CameraAerialPerspectiveVolumeDepthSliceLengthKm;
InternalCommonParameters.CameraAerialPerspectiveVolumeDepthSliceLengthKmInv =
    1.0f / CameraAerialPerspectiveVolumeDepthSliceLengthKm;
```

---

## 3. ThreadId to World Ray Mapping

### 3.1 Compute Shader Thread Organization

**Groupsize:** 4×4×4 threads per threadgroup

```cpp
// SkyAtmosphereRendering.cpp:954
const static uint32 GroupSize = 4;

// SkyAtmosphere.usf:1465
[numthreads(THREADGROUP_SIZE, THREADGROUP_SIZE, THREADGROUP_SIZE)]
void RenderCameraAerialPerspectiveVolumeCS(uint3 ThreadId : SV_DispatchThreadID)
```

**Dispatch size:**

```cpp
// SkyAtmosphereRendering.cpp:1727-1728
FIntVector TextureSize = SkyAtmosphereCameraAerialPerspectiveVolume->Desc.GetSize();
const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize,
    FRenderCameraAerialPerspectiveVolumeCS::GroupSize);
```

For a 32×32×16 texture with GroupSize=4: NumGroups = (8, 8, 4).

### 3.2 ThreadId.xy → Screen UV → World Ray

```hlsl
// SkyAtmosphere.usf:1478-1482
float2 PixPos = float2(ThreadId.xy) + 0.5f;  // [0.5, 31.5] for 32x32
float2 UV = PixPos * SkyAtmosphere.CameraAerialPerspectiveVolumeSizeAndInvSize.zw;
// UV is [0.5/32, 31.5/32] = [0.0156, 0.984]

float4 SVPos = float4(View.ViewRectMin.xy + UV * View.ViewSizeAndInvSize.xy, 0.0f, 1.0f);
// SVPos.xy is screen position as if rendering at the full view size
float3 WorldDir = GetScreenWorldDir(SVPos);
// Standard screen-space ray reconstruction
```

**Key:** The AP volume is **always 32×32** in screen space (by default), regardless of actual viewport size. The UV is mapped to the viewport's ray direction.

### 3.3 ThreadId.z → Depth Slice

```hlsl
// SkyAtmosphere.usf:1504-1513
float Slice = ((float(ThreadId.z) + 0.5f) * SkyAtmosphere.CameraAerialPerspectiveVolumeDepthResolutionInv);
Slice *= Slice;  // squared
Slice *= SkyAtmosphere.CameraAerialPerspectiveVolumeDepthResolution;

float3 RayStartWorldPos = CamPos + AerialPerspectiveStartDepthKm * WorldDir;
float tMax = Slice * SkyAtmosphere.CameraAerialPerspectiveVolumeDepthSliceLengthKm;
float3 VoxelWorldPos = RayStartWorldPos + tMax * WorldDir;
```

**Final voxel position:** Camera → AerialPerspectiveStartDepthKm → +tMax distance along world ray.

---

## 4. Below-Horizon Reprojection Logic

**Critical for avoiding black regions when objects occlude the horizon.**

### 4.1 Detection

```hlsl
// SkyAtmosphere.usf:1515-1525
float VoxelHeight = length(VoxelWorldPos);
const float UnderGround = VoxelHeight < Atmosphere.BottomRadiusKm;

float3 CameraToVoxel = VoxelWorldPos - CamPos;
float CameraToVoxelLen = length(CameraToVoxel);
float3 CameraToVoxelDir = CameraToVoxel / CameraToVoxelLen;
float PlanetNearT = RaySphereIntersectNearest(CamPos, CameraToVoxelDir,
    float3(0, 0, 0), Atmosphere.BottomRadiusKm);
bool BelowHorizon = PlanetNearT > 0.0f && CameraToVoxelLen > PlanetNearT;
```

### 4.2 Reprojection Strategy

When a voxel is behind the planet (below horizon):

```hlsl
// SkyAtmosphere.usf:1527-1570
if (BelowHorizon || UnderGround)
{
    CamPos += normalize(CamPos) * 0.02f;  // Floating-point stability workaround

    float3 VoxelWorldPosNorm = normalize(VoxelWorldPos);
    float3 CamProjOnGround = normalize(CamPos) * Atmosphere.BottomRadiusKm;
    float3 VoxProjOnGround = VoxelWorldPosNorm * Atmosphere.BottomRadiusKm;
    float3 VoxelGroundToRayStart = CamPos - VoxProjOnGround;

    if (BelowHorizon && dot(normalize(VoxelGroundToRayStart), VoxelWorldPosNorm) < 0.0001f)
    {
        // Voxel is truly below horizon (planet normal pointing away)
        float3 MiddlePoint = 0.5f * (CamProjOnGround + VoxProjOnGround);
        float MiddlePointHeight = length(MiddlePoint);

        // Project to opposite side of planet
        float3 MiddlePointOnGround = normalize(MiddlePoint) * Atmosphere.BottomRadiusKm;
        VoxelWorldPos = CamPos + 2.0f * (MiddlePointOnGround - CamPos);
    }
    else if (UnderGround)
    {
        // Voxel is underground: place on surface
        VoxelWorldPos = normalize(VoxelWorldPos) * (Atmosphere.BottomRadiusKm);
    }

    // Recompute ray with reprojected voxel
    WorldDir = normalize(VoxelWorldPos - CamPos);
    RayStartWorldPos = CamPos + AerialPerspectiveStartDepthKm * WorldDir;
    tMax = length(VoxelWorldPos - RayStartWorldPos);
}
```

**Effect:** Voxels behind the planet are folded to the visible hemisphere, ensuring continuity instead of black fringes at horizon.

---

## 5. Voxel Data Storage Format

### 5.1 Per-Voxel Storage

Each voxel stores **RGB inscattering luminance + alpha as grayscale transmittance.**

```hlsl
// SkyAtmosphere.usf:1632-1639
const float Transmittance = dot(ss.Transmittance, float3(1.0f/3.0f, 1.0f/3.0f, 1.0f/3.0f));
CameraAerialPerspectiveVolumeUAV[ThreadId] = float4(ss.L, Transmittance);

#if SEPARATE_MIE_RAYLEIGH_SCATTERING
    const float TransmittanceMieOnly = dot(ss.TransmittanceMieOnly,
        float3(1.0f/3.0f, 1.0f/3.0f, 1.0f/3.0f));
    const float TransmittanceRayOnly = dot(ss.TransmittanceRayOnly,
        float3(1.0f/3.0f, 1.0f/3.0f, 1.0f/3.0f));
    CameraAerialPerspectiveVolumeMieOnlyUAV[ThreadId] = float4(ss.LMieOnly, TransmittanceMieOnly);
    CameraAerialPerspectiveVolumeRayOnlyUAV[ThreadId] = float4(ss.LRayOnly, TransmittanceRayOnly);
#endif
```

**Layout:**
- **R, G, B**: Pre-exposed inscattering luminance (nits)
- **A**: Grayscale transmittance (0 = fully absorbing, 1 = fully transparent)

### 5.2 Optional Separated Scattering (Volumetric Clouds)

When volumetric clouds are enabled and want per-scattering-type compositing:

```cpp
// SkyAtmosphereRendering.cpp:1241, 1247-1250
const bool bSeparatedAtmosphereMieRayLeigh = VolumetricCloudWantsSeparatedAtmosphereMieRayLeigh(Scene);
if (bSeparatedAtmosphereMieRayLeigh)
{
    View.SkyAtmosphereCameraAerialPerspectiveVolumeMieOnly = ...
    View.SkyAtmosphereCameraAerialPerspectiveVolumeRayOnly = ...
}
```

**3 textures created:**
1. `CameraAerialPerspectiveVolume` — Total (Mie + Rayleigh) scattering
2. `CameraAerialPerspectiveVolumeMieOnlyUAV` — Mie scattering only
3. `CameraAerialPerspectiveVolumeRayOnlyUAV` — Rayleigh scattering only

Clouds can composite these separately for better optical accuracy.

---

## 6. Ray Marching Parameters Per Voxel

Each voxel performs **single-scattering ray marching** from camera to voxel position.

### 6.1 Ray Marching Setup

```hlsl
// SkyAtmosphere.usf:1605-1621
SamplingSetup Sampling = (SamplingSetup)0;
{
    Sampling.VariableSampleCount = false;
    Sampling.SampleCountIni = max(1.0f, (float(ThreadId.z) + 1.0f) *
        SkyAtmosphere.CameraAerialPerspectiveSampleCountPerSlice);
}

const bool Ground = false;
const float DeviceZ = FarDepthValue;
const bool MieRayPhase = true;
const float AerialPespectiveViewDistanceScale =
    SkyAtmosphere.AerialPespectiveViewDistanceScale;

SingleScatteringResult ss = IntegrateSingleScatteredLuminance(
    float4(PixPos, 0.0f, 1.0f),
    RayStartWorldPos,           // Start position (camera + start depth + ray)
    WorldDir,                   // Ray direction
    Ground,                     // Ground not visible in AP volume
    Sampling,                   // Sample count per slice
    DeviceZ,                    // Far depth (no occlusion)
    MieRayPhase,                // Include Mie phase function
    View.AtmosphereLightDirection[0].xyz,
    View.AtmosphereLightDirection[1].xyz,
    View.AtmosphereLightIlluminanceOuterSpace[0].rgb *
        SkyAtmosphere.SkyAndAerialPerspectiveLuminanceFactor,
    View.AtmosphereLightIlluminanceOuterSpace[1].rgb *
        SkyAtmosphere.SkyAndAerialPerspectiveLuminanceFactor,
    AerialPespectiveViewDistanceScale,
    tMaxMax);                   // Max marching distance
```

### 6.2 Sample Count Per Slice

**Critical detail:** Sample count increases linearly with depth!

```hlsl
// SkyAtmosphere.usf:1608
Sampling.SampleCountIni = max(1.0f, (float(ThreadId.z) + 1.0f) *
    SkyAtmosphere.CameraAerialPerspectiveSampleCountPerSlice);

// For ThreadId.z = 0 to 15 and SampleCountPerSlice = 2.0:
// Z=0: 1.0 sample
// Z=1: 2.0 samples
// Z=15: 16.0 samples
```

This is configured via:
```cpp
// SkyAtmosphereRendering.cpp:1293
InternalCommonParameters.CameraAerialPerspectiveSampleCountPerSlice =
    FMath::Max(AerialPerspectiveBaseSampleCountPerSlice,
        FMath::Min(2.0f * SkyInfo.GetSkyAtmosphereSceneProxy().GetTraceSampleCountScale(),
            float(CVarSkyAtmosphereAerialPerspectiveLUTSampleCountMaxPerSlice.GetValueOnRenderThread())));

// AerialPerspectiveBaseSampleCountPerSlice = 1.0f (line 1279)
```

---

## 7. Sampling from Base Pass

### 7.1 GetAerialPerspectiveLuminanceTransmittance Function

Called from base pass (deferred lighting, etc.) to composite aerial perspective.

```hlsl
// SkyAtmosphereCommon.ush:42-121
float4 GetAerialPerspectiveLuminanceTransmittance(
    bool ViewIsRealTimeReflectionCapture,
    float4 CameraAerialPerspectiveVolumeSizeAndInvSize,
    float4 NDC,                                       // Clip-space position
    float3 WorldPositionRelativeToCamera,            // World position - camera
    Texture3D<float4> AerialPerspectiveVolumeTexture,
    SamplerState AerialPerspectiveVolumeTextureSampler,
    float AerialPerspectiveVolumeDepthResolutionInv,
    float AerialPerspectiveVolumeDepthResolution,
    float AerialPerspectiveVolumeStartDepth,        // In kilometers
    float AerialPerspectiveVolumeDepthSliceLengthKm,
    float AerialPerspectiveVolumeDepthSliceLengthKmInv,
    float OneOverExposure,
    float NearFadeOutRangeInvDepthKm)
{
    // ... code ...
}
```

### 7.2 Depth-to-Slice Conversion (Sampling Direction)

```hlsl
// SkyAtmosphereCommon.ush:67-72
float tDepth = max(0.0f, length(WorldPositionRelativeToCamera) -
    AerialPerspectiveVolumeStartDepth);
float LinearSlice = tDepth * AerialPerspectiveVolumeDepthSliceLengthKmInv;
float LinearW = LinearSlice * AerialPerspectiveVolumeDepthResolutionInv;
float NonLinW = sqrt(LinearW);  // INVERSE OF SQUARED
float NonLinSlice = NonLinW * AerialPerspectiveVolumeDepthResolution;
```

**Example:** For tDepth = 28.2 km with DepthSliceLengthKm = 6.25:
```
LinearSlice = 28.2 / 6.25 = 4.512
LinearW = 4.512 / 16 = 0.282
NonLinW = sqrt(0.282) = 0.531
NonLinSlice = 0.531 * 16 = 8.5
```
This matches the Z=8 voxel layer (which samples to ~28.2 km as computed above).

### 7.3 Screen UV Computation

```hlsl
// SkyAtmosphereCommon.ush:65, 104
float2 ScreenUv = (NDC.xy / NDC.ww) * float2(0.5f, -0.5f) + 0.5f;
// Standard NDC → [0,1] screen UV conversion
float4 AP = Texture3DSampleLevel(AerialPerspectiveVolumeTexture,
    AerialPerspectiveVolumeTextureSampler,
    float3(ScreenUv, NonLinW), 0.0f);
```

### 7.4 Near-Fade-Out Weighting

**Prevents harsh transitions very close to camera.**

```hlsl
// SkyAtmosphereCommon.ush:74-81
const float HalfSliceDepth = 0.70710678118654752440084436210485f; // sqrt(0.5f)
float Weight = 1.0f;
if (NonLinSlice < HalfSliceDepth)
{
    Weight = saturate(NonLinSlice * NonLinSlice * 2.0f);
}
Weight *= saturate(tDepth * NearFadeOutRangeInvDepthKm);

AP.rgb *= Weight;
AP.a = 1.0 - (Weight * (1.0f - AP.a));
```

**Weight function:**
- For NonLinSlice < sqrt(0.5) ≈ 0.707: `Weight = (NonLinSlice²) * 2.0`
- For NonLinSlice ≥ 0.707: `Weight = 1.0`
- Finally: `Weight *= saturate(tDepth / NearFadeOutRangeInvKm)`

This creates smooth fade-in from camera, preventing popping.

### 7.5 Called from Base Pass

```cpp
// SkyAtmosphereRendering.cpp:1002-1009
PassParameters->Atmosphere = ...;
PassParameters->SkyAtmosphere = ...;
PassParameters->CameraAerialPerspectiveVolumeTexture = SkyRC.SkyAtmosphereCameraAerialPerspectiveVolume;
PassParameters->CameraAerialPerspectiveVolumeTextureSampler = SamplerLinearClamp;
PassParameters->AerialPerspectiveVolumeDepthResolutionInv = ...;
PassParameters->AerialPerspectiveVolumeDepthResolution = ...;
PassParameters->AerialPerspectiveVolumeStartDepth = ...;
PassParameters->AerialPerspectiveVolumeDepthSliceLengthKm = ...;
PassParameters->AerialPerspectiveVolumeDepthSliceLengthKmInv = ...;
PassParameters->OneOverExposure = ...;
```

---

## 8. Real-Time Reflection Capture (360° AP)

### 8.1 Latlong Mapping for Cubemaps

When rendering sky atmosphere for real-time reflection captures (360°):

```hlsl
// SkyAtmosphere.usf:1490-1502 (compute shader)
if (RealTimeReflection360Mode)
{
    float2 UnitUV = FromSubUvsToUnit(UV, SkyAtmosphere.CameraAerialPerspectiveVolumeSizeAndInvSize);

    float SinPhi = 2.0f * UnitUV.y - 1.0f;           // [-1, 1]
    float CosPhi = sqrt(1.0f - SinPhi * SinPhi);
    float Theta = 2.0f * PI * UnitUV.x;              // [0, 2π]
    float CosTheta = cos(Theta);
    float SinTheta = sqrt(1.0f - CosTheta * CosTheta) * (Theta > PI ? -1.0f : 1.0f);
    WorldDir = float3(CosTheta * CosPhi, SinTheta * CosPhi, SinPhi);
    WorldDir = normalize(WorldDir);
}
```

**Mapping:**
- **UnitUV.x ∈ [0,1]**: Azimuthal angle θ ∈ [0, 2π]
- **UnitUV.y ∈ [0,1]**: Elevation angle φ ∈ [-π/2, π/2] encoded as sin(φ)

### 8.2 Inverse Mapping (for sampling in base pass)

```hlsl
// SkyAtmosphereCommon.ush:84-101 (AP_SUPPORT_REALTIME_REFLECTION_CAPTURE)
if (ViewIsRealTimeReflectionCapture)
{
    float3 WorldDir = normalize(WorldPositionRelativeToCamera);

    float SinPhi = WorldDir.z;
    float CosPhi = sqrt(1.0f - SinPhi * SinPhi);
    ScreenUv.y = WorldDir.z * 0.5f + 0.5f;

    float CosTheta = WorldDir.x / CosPhi;
    float SinTheta = WorldDir.y / CosPhi;
    float Theta = acos(CosTheta);
    Theta = SinTheta < 0.0f ? (PI - Theta) + PI : Theta;
    ScreenUv.x = Theta / (2.0 * PI);

    ScreenUv = FromUnitToSubUvs(ScreenUv, CameraAerialPerspectiveVolumeSizeAndInvSize);
}
```

---

## 9. Shader Parameter Binding

### 9.1 Compute Shader Parameters

```cpp
// SkyAtmosphereRendering.cpp:956-983
class FRenderCameraAerialPerspectiveVolumeCS : public FGlobalShader
{
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
        SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, CameraAerialPerspectiveVolumeUAV)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, CameraAerialPerspectiveVolumeMieOnlyUAV)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, CameraAerialPerspectiveVolumeRayOnlyUAV)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, MultiScatteredLuminanceLutTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudShadowMapTexture0)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudShadowMapTexture1)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudSkyAOTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
        SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteredLuminanceLutTextureSampler)
        SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudShadowMapTexture0Sampler)
        SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudShadowMapTexture1Sampler)
        SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudSkyAOTextureSampler)
        SHADER_PARAMETER(float, AerialPerspectiveStartDepthKm)
        SHADER_PARAMETER(float, RealTimeReflection360Mode)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVolumeShadowingShaderParametersGlobal0, Light0Shadow)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVolumeShadowingShaderParametersGlobal1, Light1Shadow)
        SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMap)
        SHADER_PARAMETER(int32, VirtualShadowMapId0)
        SHADER_PARAMETER(int32, VirtualShadowMapId1)
        SHADER_PARAMETER(float, VolumetricCloudShadowStrength0)
        SHADER_PARAMETER(float, VolumetricCloudShadowStrength1)
        SHADER_PARAMETER_STRUCT_REF(FVolumetricCloudCommonGlobalShaderParameters, VolumetricCloudCommonGlobalParams)
    END_SHADER_PARAMETER_STRUCT()
};
```

### 9.2 View Uniform Parameters (for base pass sampling)

```cpp
// SkyAtmosphereRendering.cpp:243-247
SHADER_PARAMETER(FVector4f, CameraAerialPerspectiveVolumeSizeAndInvSize)
SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthResolution)
SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthResolutionInv)
SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthSliceLengthKm)
SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthSliceLengthKmInv)
```

---

## 10. Constants & CVars Summary

| Parameter | CVar | Default | Unit | Purpose |
|-----------|------|---------|------|---------|
| XY Resolution | `r.SkyAtmosphere.AerialPerspectiveLUTWidth` | 32 | pixels | Screen-space grid resolution |
| Depth Slices | `r.SkyAtmosphere.AerialPerspectiveLUTDepthResolution` | 16 | slices | Number of depth layers |
| Max Depth | `r.SkyAtmosphere.AerialPerspectiveLUTDepth` | 100 | km | Total depth range |
| KM per Slice | computed | 6.25 | km | `DepthKm / DepthResolution` |
| AP Start Depth | `USkyAtmosphereComponent::AerialPerspectiveStartDepthKm` | 0.0 | km | Distance to start sampling |
| Sample Count Base | — | 1.0 | — | Base for depth-dependent sampling |
| Sample Count Max | `r.SkyAtmosphere.AerialPerspectiveLUTSampleCountMaxPerSlice` | 2.0 | — | Multiplied by trace scale |
| Near Fade Range | — | 0.00001 | km | 1 cm fade region at camera |

**Unit conversions:**
```cpp
#define CM_TO_SKY_UNIT 0.00001f  // 1 cm = 0.00001 km
#define SKY_UNIT_TO_CM 100000.0f // 1 km = 100000 cm
#define AP_KM_PER_SLICE 4.0f     // Constant for reference (unused in new system)
```

---

## 11. Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│ Compute Shader: RenderCameraAerialPerspectiveVolumeCS           │
├─────────────────────────────────────────────────────────────────┤
│ ThreadId.xy (0-31, 0-31)  ─→  Screen UV  ─→  WorldDir          │
│ ThreadId.z (0-15)         ─→  Slice      ─→  Squared distance  │
│ Camera position + Ray     ─→  Voxel position in world           │
│                                                                  │
│ Check: Is voxel below horizon? ─→ Reproject to visible side     │
│ Check: Is ray in atmosphere?   ─→ Clamp to atmosphere boundary  │
│                                                                  │
│ Ray March: (SampleCount = (Z+1) * SampleCountPerSlice)          │
│   ├─ integrate Rayleigh + Mie scattering                        │
│   ├─ account for light transmittance (LUT lookup)               │
│   ├─ apply cloud shadows (optional)                             │
│   └─ compute transmittance                                      │
│                                                                  │
│ Store: RGB = inscattering, A = grayscale transmittance          │
│        (Optional: separate Mie and Rayleigh)                    │
└─────────────────────────────────────────────────────────────────┘
                              ↓
                        [Texture3D created]
                    (32×32×16 float RGBA)
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│ Base Pass: Sample AerialPerspectiveVolume                       │
├─────────────────────────────────────────────────────────────────┤
│ For each pixel (screen UV, depth):                              │
│   1. Compute distance from camera: tDepth                       │
│   2. Convert to linear slice: LinearSlice = tDepth / KmPerSlice │
│   3. Normalize to [0,1]: LinearW = LinearSlice / DepthResolution│
│   4. Apply squared distribution: NonLinW = sqrt(LinearW)        │
│   5. Sample texture at (ScreenUv, NonLinW)                      │
│   6. Apply near-fade weight                                     │
│   7. Composite: RGB blended, A = transmittance composite        │
└─────────────────────────────────────────────────────────────────┘
```

---

## 12. Implementation Checklist for UzEngine

- [ ] Create RWTexture3D (32×32×16, float32 RGBA)
- [ ] Implement squared depth distribution in compute shader
- [ ] Handle below-horizon reprojection logic
- [ ] Perform ray marching with variable sample count per slice
- [ ] Separate Mie/Rayleigh scattering (optional but recommended)
- [ ] Implement `GetAerialPerspectiveLuminanceTransmittance` function
- [ ] Bind texture + sampler to base pass constant buffers
- [ ] Test with reflection captures (360° latlong AP)
- [ ] Validate near-fade weighting matches UE5
- [ ] Verify squared distribution produces depth-clustered voxels

