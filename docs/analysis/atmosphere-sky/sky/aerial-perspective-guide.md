<!-- doc-meta
title: Aerial Perspective Implementation Guide for UzEngine
status: technical-guide
categories: rendering, atmosphere
tags: aerial-perspective, sky-atmosphere, compute-shader, base-pass
-->

# Aerial Perspective Volume Implementation Guide

## Overview

This guide provides the **exact structure and integration points** for implementing UE5's aerial perspective system in UzEngine based on source code analysis.

---

## Part 1: Architecture

### Data Structure: CameraAerialPerspectiveVolume

**Location in UzEngine**: `src/engine/renderer/core/aerial_perspective.h`

```cpp
/// Precomputed atmospheric light scattering for deferred shading
/// @source UE5 SkyAtmosphere.usf:1458-1461
struct AerialPerspectiveVolume {
    /// 3D texture: [ScreenX][ScreenY][DepthSlice]
    /// Format: RGBA16F (RGB=inscattering, A=transmittance)
    /// Size: 32×32×32 (configurable, must match shader constants)
    RhiTextureHandle texture;

    /// Descriptor for sampling in pixel shaders
    RhiSamplerHandle sampler;

    /// Precomputed parameters (CPU-side mirrors of shader constants)
    float volumeDepthStartKm = 0.001f;      /// Start distance from camera
    float volumeDepthSliceLengthKm = 4.0f;  /// km per slice
    int   volumeDepthResolution = 32;       /// Z-dimension size

    /// View-relative parameters (updated per-frame)
    glm::uvec2 volumeResolution{32, 32};    /// XY-dimension size
};
```

### Shader Constants: AerialPerspectiveParams

**Location in UzEngine**: `src/engine/renderer/core/constant_buffers.h`

```hlsl
/// Match UE5 SkyAtmosphere compute shader parameters
/// @source UE5 SceneRenderTargets.cpp, FSceneRenderer::RenderSkyAtmosphereInternal
struct AerialPerspectiveParams {
    float CameraAerialPerspectiveVolumeDepthSliceLengthKm;      /// 4.0 km
    float CameraAerialPerspectiveVolumeDepthSliceLengthKmInv;   /// 1/4.0
    float CameraAerialPerspectiveVolumeDepthResolutionInv;      /// 1/32
    uint  CameraAerialPerspectiveVolumeDepthResolution;         /// 32

    float AerialPerspectiveStartDepthKm;     /// ~0.001 km (1 m)
    float CameraAerialPespectiveViewDistanceScale;  /// ~1.0
    uint  CameraAerialPerspectiveSampleCountPerSlice; /// ~8
    uint  ApplyCameraAerialPerspectiveVolume; /// 0=off, 1=on
};
```

---

## Part 2: Compute Shader

### File: `runtime/shaders/sky/aerial_perspective_volume_cs.hlsl`

**Structure**:
```hlsl
/// @source UE5 SkyAtmosphere.usf:1466
/// Precompute atmospheric scattering into a 3D voxel texture
///
/// Executed: Once per frame, after sky atmosphere parameters updated
/// Dispatch: (32, 32, 32) or equivalent
///
/// Key algorithm:
/// 1. Convert thread ID to voxel position (screen direction × depth slice)
/// 2. Handle below-horizon geometry (planet occlusion)
/// 3. Ray-march through atmosphere
/// 4. Accumulate inscattering (Rayleigh blue + Mie yellow + direct sun)
/// 5. Compute transmittance (how much sky is obscured)
/// 6. Store in CameraAerialPerspectiveVolumeUAV[ThreadId]

[numthreads(8, 8, 4)]  /// Adjust to match volume resolution
void RenderCameraAerialPerspectiveVolumeCS(uint3 ThreadId : SV_DispatchThreadID) {
    // === THREAD BOUNDS CHECK ===
    if (ThreadId.z >= apParams.CameraAerialPerspectiveVolumeDepthResolution) {
        return;
    }
    if (any(ThreadId.xy >= volumeResolution)) {
        return;
    }

    // === CONVERT VOXEL TO WORLD RAY ===
    // ThreadId.xy → Screen UV
    float2 PixPos = float2(ThreadId.xy) + 0.5f;
    float2 UV = PixPos * volumeSizeInvSize.zw;  /// zw = 1/width, 1/height

    // UV → Screen position → Camera ray
    float4 SVPos = float4(viewRectMin.xy + UV * viewSizeInvSize.xy, 0.0f, 1.0f);
    float3 WorldDir = GetScreenWorldDir(SVPos);  /// From camera to screen direction
    float3 CamPos = GetTranslatedCameraPlanetPos();  /// Camera in sky planet-relative coords

    // ThreadId.z → Depth in km
    float Slice = ((float(ThreadId.z) + 0.5f) * apParams.CameraAerialPerspectiveVolumeDepthResolutionInv);
    Slice *= Slice;  /// @source Squared distribution: detail near camera
    Slice *= apParams.CameraAerialPerspectiveVolumeDepthResolution;
    float tMax = Slice * apParams.CameraAerialPerspectiveVolumeDepthSliceLengthKm;

    // === HANDLE BELOW-HORIZON GEOMETRY ===
    // @source UE5 SkyAtmosphere.usf:1517-1571
    // When ray hits ground (distance < horizon distance), detect and reproject
    float3 RayStartWorldPos = CamPos + apParams.AerialPerspectiveStartDepthKm * WorldDir;
    float3 VoxelWorldPos = RayStartWorldPos + tMax * WorldDir;

    // Sphere intersection with planet
    float3 CameraToVoxel = VoxelWorldPos - CamPos;
    float3 CameraToVoxelNorm = normalize(CameraToVoxel);
    float VoxelDistanceKm = length(CameraToVoxel);
    float PlanetIntersectionT = RaySphereIntersectNearest(CamPos, CameraToVoxelNorm,
                                                          float3(0,0,0),
                                                          atmosphere.BottomRadiusKm);
    bool BelowHorizon = (PlanetIntersectionT > 0.0f) && (VoxelDistanceKm > PlanetIntersectionT);

    if (BelowHorizon) {
        // Reproject to opposite side of planet to sample sky color
        // This ensures ground near horizon gets sky-blue tint
        float3 VoxelWorldPosNorm = normalize(VoxelWorldPos);
        float3 CamProjOnGround = normalize(CamPos) * atmosphere.BottomRadiusKm;
        float3 VoxProjOnGround = VoxelWorldPosNorm * atmosphere.BottomRadiusKm;
        float3 MiddlePoint = 0.5f * (CamProjOnGround + VoxProjOnGround);
        float3 MiddlePointOnGround = normalize(MiddlePoint) * atmosphere.BottomRadiusKm;
        VoxelWorldPos = CamPos + 2.0f * (MiddlePointOnGround - CamPos);

        WorldDir = normalize(VoxelWorldPos - CamPos);
        RayStartWorldPos = CamPos + apParams.AerialPerspectiveStartDepthKm * WorldDir;
        tMax = length(VoxelWorldPos - RayStartWorldPos);
    }

    // === RAY MARCH THROUGH ATMOSPHERE ===
    // Use SampleAtmosphereMediumRGB and single-scattering integration
    // Result: inscattering color (RGB) and transmittance (A)

    float3 RayStartToAtmosphereBoundary = WorldDir;  /// Direction (normalized)

    // === SIMPLE APPROXIMATION (for initial implementation) ===
    // @source UE5 uses full Bruneton atmosphere with transmittance/scattering LUTs
    // For MVP: use simplified model or sample precomputed sky cubemap

    float3 Inscattering = float3(0.0, 0.3, 0.8);  /// Sky blue (placeholder)
    float Transmittance = 1.0;                      /// Full transmittance (placeholder)

    // Distance-based fade: closer = more opaque AP
    float DistanceFadeStart = 5.0;   /// km
    float DistanceFadeLength = 20.0; /// km
    float DistanceFade = saturate((tMax - DistanceFadeStart) / DistanceFadeLength);
    Inscattering *= DistanceFade;

    // === WRITE TO UAV ===
    CameraAerialPerspectiveVolumeUAV[ThreadId] = float4(Inscattering, Transmittance);
}
```

---

## Part 3: Base Pass Integration

### File: `runtime/shaders/deferred/deferred_lighting_ps.hlsl` (or your geometry/base pass)

**Location in pixel shader after all lighting/fog computed**:

```hlsl
/// Apply aerial perspective AFTER height fog, BEFORE final color output
/// @source UE5 BasePassPixelShader.usf:1435
void ApplyAerialPerspective(inout float3 LitColor,
                            float3 WorldPositionRelativeToCamera,
                            float4 NDCPosition) {
    if (apParams.ApplyCameraAerialPerspectiveVolume == 0) {
        return;  /// Feature disabled
    }

    // === COMPUTE VOXEL LOOKUP ===
    // Convert NDC to screen UV
    float2 ScreenUv = (NDCPosition.xy / NDCPosition.ww) * float2(0.5f, -0.5f) + 0.5f;

    // Compute depth in km
    float DistanceKm = length(WorldPositionRelativeToCamera) * 0.00001;  /// Convert cm to km

    // Compute slice index (with squared distribution)
    float LinearSlice = DistanceKm * apParams.CameraAerialPerspectiveVolumeDepthSliceLengthKmInv;
    float LinearW = LinearSlice * apParams.CameraAerialPerspectiveVolumeDepthResolutionInv;
    float NonLinW = sqrt(LinearW);  /// Inverse of squared distribution

    // Clamp to valid range [0, 1]
    NonLinW = saturate(NonLinW);

    // === SAMPLE AERIAL PERSPECTIVE VOLUME ===
    float3 VolumeUV = float3(ScreenUv, NonLinW);
    float4 AP = CameraAerialPerspectiveVolume.SampleLevel(APSampler, VolumeUV, 0.0);

    // AP.rgb = inscattering (what you see through the atmosphere)
    // AP.a   = transmittance (how much geometry shows through)

    // === APPLY WITH NEAR FADE ===
    // Don't apply AP right at camera
    const float NearFadeOutRangeKm = 0.1;  /// 100 m fade region
    float NearFadeWeight = saturate(DistanceKm / NearFadeOutRangeKm);
    AP.rgb *= NearFadeWeight;

    // === BLEND OVER GEOMETRY ===
    // Composite: (Geometry * Transmittance) + Inscattering
    LitColor = LitColor * AP.a + AP.rgb;
}

/// In your main lighting function:
// float3 LitColor = ComputeLighting(...);
// ApplyAerialPerspective(LitColor, WorldPositionRelativeToCamera, NDCPosition);
// return LitColor;
```

---

## Part 4: Integration with Fog System

### The Complete Fog + AP Pipeline

```hlsl
// @source UE5 BasePassPixelShader.usf:1379-1445
void ComputeFinalFogging(inout float3 LitColor,
                         float3 WorldPositionRelativeToCamera,
                         float4 NDCPosition) {

    // === STAGE 1: HEIGHT FOG ===
    // Standard exponential height fog at camera height produces near-zero opacity
    float4 HeightFog = CalculateHeightFog(WorldPositionRelativeToCamera);

    // === STAGE 2: VOLUMETRIC FOG (optional) ===
    float4 Fogging = HeightFog;
    if (bApplyVolumetricFog) {
        float3 VolumeUV = ComputeVolumeUV(WorldPositionRelativeToCamera);
        Fogging = CombineVolumetricFog(HeightFog, VolumeUV, SceneDepth);
    }

    // === STAGE 3: LOCAL FOG VOLUMES (optional) ===
    if (bApplyLocalFogVolumes) {
        float4 LocalFog = SampleLocalFogVolumes(WorldPositionRelativeToCamera);
        Fogging.rgb = LocalFog.rgb + Fogging.rgb * LocalFog.a;
        Fogging.a *= LocalFog.a;
    }

    // === STAGE 4: AERIAL PERSPECTIVE ===
    // Applied OVER all other fog systems
    if (bApplyAerialPerspective) {
        float4 AP = SampleAerialPerspective(NDCPosition, WorldPositionRelativeToCamera);
        Fogging.rgb = AP.rgb + Fogging.rgb * AP.a;  /// Add AP color over fog
        Fogging.a *= AP.a;                            /// Reduce transmittance
    }

    // === STAGE 5: APPLY COMBINED FOG TO GEOMETRY ===
    LitColor = LitColor * Fogging.a + Fogging.rgb;
}
```

---

## Part 5: CPU-Side Setup

### FrameData Update

```cpp
/// Called once per frame, after scene capture, before rendering
/// @source UE5 FSceneRenderer::RenderSkyAtmosphereInternal
void UpdateAerialPerspectiveVolume(FrameData& Frame,
                                   const SkyAtmosphereComponent& Sky) {
    if (!Sky.IsEnabled() || !ApParams.ApplyCameraAerialPerspectiveVolume) {
        return;
    }

    // === CREATE/RECREATE VOLUME TEXTURE IF NEEDED ===
    if (!Frame.AerialPerspectiveVolume.texture.IsValid()) {
        RhiTextureDesc VolDesc;
        VolDesc.width = 32;
        VolDesc.height = 32;
        VolDesc.depth = 32;
        VolDesc.format = RhiFormat::RGBA16F;
        VolDesc.usage = RhiResourceUsage::UnorderedAccess;  /// UAV
        VolDesc.state = RhiResourceState::UnorderedAccess;

        Frame.AerialPerspectiveVolume.texture =
            Rhi::CreateTexture(VolDesc);
        Frame.AerialPerspectiveVolume.sampler =
            Rhi::CreateSampler(RhiSamplerDesc::LinearClamp());
    }

    // === UPDATE SHADER CONSTANTS ===
    ApParams.CameraAerialPerspectiveVolumeDepthSliceLengthKm = 4.0f;
    ApParams.CameraAerialPerspectiveVolumeDepthSliceLengthKmInv = 1.0f / 4.0f;
    ApParams.CameraAerialPerspectiveVolumeDepthResolution = 32;
    ApParams.CameraAerialPerspectiveVolumeDepthResolutionInv = 1.0f / 32.0f;
    ApParams.AerialPerspectiveStartDepthKm = 0.001f;  /// 1 m from camera
    ApParams.CameraAerialPerspectiveSampleCountPerSlice = 8;
    ApParams.ApplyCameraAerialPerspectiveVolume = 1;

    // === DISPATCH COMPUTE SHADER ===
    // Dispatch once per frame with (32, 32, 32) threads
    Rhi::DispatchCompute(
        Frame.ComputeGraphHandle,
        "RenderCameraAerialPerspectiveVolumeCS",
        32, 32, 32,  /// Grid dimensions
        Frame.AerialPerspectiveVolume.texture  /// Output UAV
    );
}
```

---

## Part 6: Testing Verification

### Visual Checklist

1. **Setup Scene**:
   - Plane at Z=0 with neutral gray material
   - SkyAtmosphere with default Bruneton parameters (Rayleigh blue)
   - DirectionalLight
   - No height fog, or height fog with Z=-5000 + falloff 0.2

2. **Before AP Implementation**: Ground is gray/brown at horizon

3. **After AP Implementation**: Ground at horizon has blue tint that:
   - Fades from blue (far distance) to neutral (near camera)
   - Matches UE5 Basic scene visually
   - Increases intensity when SkyAtmosphere is brighter
   - Disappears when SkyAtmosphere is disabled

4. **Advanced Verification**:
   - Disable below-horizon reprojection → visual artifacts at horizon line
   - Reduce AP volume resolution → more pixelated/banding artifacts
   - Change atmosphere Rayleigh color → AP changes accordingly

---

## References

- **SkyAtmosphere compute**: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf:1466`
- **Base pass AP apply**: `W:\UE_5.6_Source\Engine\Shaders\Private\BasePassPixelShader.usf:1435`
- **Sampling function**: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush:42`
- **Below-horizon logic**: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf:1517-1571`

