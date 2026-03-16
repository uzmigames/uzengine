<!-- doc-meta
title: UE5 Transmittance LUT — HLSL Code Snippets for Direct Porting
author: Unreal Architect
date: 2026-03-15
tags: [hlsl, transmittance-lut, code-snippets, shader]
status: complete
version: 1.0
source: W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf + SkyAtmosphereCommon.ush
-->

# UE5 Transmittance LUT — HLSL Code Snippets for Direct Porting

All HLSL code below is extracted directly from UE5 5.6 source and can be ported to UzEngine with minimal changes (remove UE5 macro dependencies).

## Complete UV Mapping Functions (Copy-Paste Ready)

These functions are from `SkyAtmosphereCommon.ush` lines 156-192. They are **pure math** with no UE5 dependencies.

### Decode: UV → Parameters

```hlsl
/// Transmittance LUT function parameterisation from Bruneton 2017
/// https://github.com/ebruneton/precomputed_atmospheric_scattering
///
/// Decode normalized [0,1] UV coordinates to physical parameters
/// uv in [0,1]
/// ViewZenithCosAngle in [-1,1]
/// ViewHeight in [bottomRadius, topRadius]
void fromTransmittanceLutUVs(
    out float ViewHeight, out float ViewZenithCosAngle,
    in float BottomRadius, in float TopRadius, in float2 UV)
{
    float Xmu = UV.x;
    float Xr = UV.y;

    float H = sqrt(TopRadius * TopRadius - BottomRadius * BottomRadius);
    float Rho = H * Xr;
    ViewHeight = sqrt(Rho * Rho + BottomRadius * BottomRadius);

    float Dmin = TopRadius - ViewHeight;
    float Dmax = Rho + H;
    float D = Dmin + Xmu * (Dmax - Dmin);
    ViewZenithCosAngle = D == 0.0f ? 1.0f : (H * H - Rho * Rho - D * D) / (2.0f * ViewHeight * D);
    ViewZenithCosAngle = clamp(ViewZenithCosAngle, -1.0f, 1.0f);
}
```

### Encode: Parameters → UV

```hlsl
/// Transmittance LUT function parameterisation from Bruneton 2017
/// https://github.com/ebruneton/precomputed_atmospheric_scattering
///
/// Encode physical parameters to normalized [0,1] UV coordinates
/// Inverse of fromTransmittanceLutUVs
void getTransmittanceLutUvs(
    in float viewHeight, in float viewZenithCosAngle, in float BottomRadius, in float TopRadius,
    out float2 UV)
{
    float H = sqrt(max(0.0f, TopRadius * TopRadius - BottomRadius * BottomRadius));
    float Rho = sqrt(max(0.0f, viewHeight * viewHeight - BottomRadius * BottomRadius));

    float Discriminant = viewHeight * viewHeight * (viewZenithCosAngle * viewZenithCosAngle - 1.0f) + TopRadius * TopRadius;
    float D = max(0.0f, (-viewHeight * viewZenithCosAngle + sqrt(Discriminant))); // Distance to atmosphere boundary

    float Dmin = TopRadius - viewHeight;
    float Dmax = Rho + H;
    float Xmu = (D - Dmin) / (Dmax - Dmin);
    float Xr = Rho / H;

    UV = float2(Xmu, Xr);
    //UV = float2(fromUnitToSubUvs(UV.x, TRANSMITTANCE_TEXTURE_WIDTH), fromUnitToSubUvs(UV.y, TRANSMITTANCE_TEXTURE_HEIGHT)); // No real impact so off
}
```

## Complete Compute Shader

Extracted from `SkyAtmosphere.usf` lines 1086-1138.

### Shader Header and UAV Declaration

```hlsl
#ifndef THREADGROUP_SIZE
#define THREADGROUP_SIZE 1
#endif

// see SkyAtmosphereRendering.cpp GetSkyLutTextureFormat
#if SHADING_PATH_MOBILE
RWTexture2D<float4> TransmittanceLutUAV;
#else
RWTexture2D<float3> TransmittanceLutUAV;
#endif
```

### Main Compute Kernel

```hlsl
[numthreads(THREADGROUP_SIZE, THREADGROUP_SIZE, 1)]
void RenderTransmittanceLutCS(uint3 ThreadId : SV_DispatchThreadID)
{
    float2 PixPos = float2(ThreadId.xy) + 0.5f;

    // Compute camera position from LUT coords
    float2 UV = (PixPos) * SkyAtmosphere.TransmittanceLutSizeAndInvSize.zw;
    float ViewHeight;
    float ViewZenithCosAngle;
    UvToLutTransmittanceParams(ViewHeight, ViewZenithCosAngle, UV);

    //  A few extra needed constants
    float3 WorldPos = float3(0.0f, 0.0f, ViewHeight);
    float3 WorldDir = float3(0.0f, sqrt(1.0f - ViewZenithCosAngle * ViewZenithCosAngle), ViewZenithCosAngle);

    SamplingSetup Sampling = (SamplingSetup)0;
    {
        Sampling.VariableSampleCount = false;
        Sampling.SampleCountIni = SkyAtmosphere.TransmittanceSampleCount;
    }
    const bool Ground = false;
    const float DeviceZ = FarDepthValue;
    const bool MieRayPhase = false;
    const float3 NullLightDirection = float3(0.0f, 0.0f, 1.0f);
    const float3 NullLightIlluminance = float3(0.0f, 0.0f, 0.0f);
    const float AerialPespectiveViewDistanceScale = 1.0f;
    SingleScatteringResult ss = IntegrateSingleScatteredLuminance(
        float4(PixPos,0.0f,1.0f), WorldPos, WorldDir,
        Ground, Sampling, DeviceZ, MieRayPhase,
        NullLightDirection, NullLightDirection, NullLightIlluminance, NullLightIlluminance,
        AerialPespectiveViewDistanceScale);

    float3 transmittance = exp(-ss.OpticalDepth);

#if SHADING_PATH_MOBILE
    TransmittanceLutUAV[int2(PixPos)] = float4(transmittance, 0.0f);
#else
    TransmittanceLutUAV[int2(PixPos)] = transmittance;
#endif
}
```

## Runtime Lookup Function

From `SkyAtmosphere.usf` lines 346-356.

### Simple Lookup with Optional White Fallback

```hlsl
float3 GetTransmittance(in float LightZenithCosAngle, in float PHeight)
{
    float2 UV;
    LutTransmittanceParamsToUv(PHeight, LightZenithCosAngle, UV);
#ifdef WHITE_TRANSMITTANCE
    float3 TransmittanceToLight = 1.0f;
#else
    float3 TransmittanceToLight = TransmittanceLutTexture.SampleLevel(TransmittanceLutTextureSampler, UV, 0).rgb;
#endif
    return TransmittanceToLight;
}
```

### Wrapper for Integration (SkyAtmosphereCommon.ush)

```hlsl
float3 GetAtmosphereTransmittance(
    float3 PlanetCenterToWorldPos, float3 WorldDir, float BottomRadius, float TopRadius,
    Texture2D<float4> TransmittanceLutTexture, SamplerState TransmittanceLutTextureSampler)
{
    // For each view height entry, transmittance is only stored from zenith to horizon. Earth shadow is not accounted for.
    // It does not contain earth shadow in order to avoid texel linear interpolation artefact when LUT is low resolution.
    // As such, at the most shadowed point of the LUT when close to horizon, pure black with earth shadow is never hit.
    // That is why we analytically compute the virtual planet shadow here.
    const float2 Sol = RayIntersectSphere(PlanetCenterToWorldPos, WorldDir, float4(float3(0.0f, 0.0f, 0.0f), BottomRadius));
    if (Sol.x > 0.0f || Sol.y > 0.0f)
    {
        return 0.0f;
    }

    const float PHeight = length(PlanetCenterToWorldPos);
    const float3 UpVector = PlanetCenterToWorldPos / PHeight;
    const float LightZenithCosAngle = dot(WorldDir, UpVector);
    float2 TransmittanceLutUv;
    getTransmittanceLutUvs(PHeight, LightZenithCosAngle, BottomRadius, TopRadius, TransmittanceLutUv);
    const float3 TransmittanceToLight = Texture2DSampleLevel(TransmittanceLutTexture, TransmittanceLutTextureSampler, TransmittanceLutUv, 0.0f).rgb;
    return TransmittanceToLight;
}
```

## Supporting Structures and Helpers

### SamplingSetup (from UE5 ParticipatingMediaCommon)

For UzEngine, you can simplify this to:

```hlsl
struct SamplingSetup
{
    bool VariableSampleCount;
    float SampleCountIni;
};
```

### Constant Buffer Layout

From `SkyAtmosphere.usf` (referenced definitions):

```hlsl
// Declare these in your constant buffer:
cbuffer FSkyAtmosphereInternalCommonParameters
{
    float4 TransmittanceLutSizeAndInvSize;  // (width, height, 1/width, 1/height)
    float TransmittanceSampleCount;         // 10.0 by default
    // ... other parameters ...
};

cbuffer FAtmosphereUniformShaderParameters
{
    float BottomRadiusKm;                   // 6360
    float TopRadiusKm;                      // 6460
    float3 RayleighScattering;
    float RayleighDensityExpScale;
    float3 MieScattering;
    float3 MieAbsorption;
    float MieDensityExpScale;
    float3 OzoneAbsorption;
    float AbsorptionDensity0LayerWidth;
    float AbsorptionDensity0LinearTerm;
    float AbsorptionDensity0ConstantTerm;
    float AbsorptionDensity1LinearTerm;
    float AbsorptionDensity1ConstantTerm;
    // ... additional parameters ...
};
```

## UV Coordinate Sub-Texel Adjustment (Optional)

From `SkyAtmosphereCommon.ush` lines 38-39. Use these if you want to avoid edge artifacts:

```hlsl
/// Convert unit [0,1] coordinates to sub-texel coordinates (adds 0.5 offset)
float2 FromUnitToSubUvs(float2 uv, float4 SizeAndInvSize)
{
    return (uv + 0.5f * SizeAndInvSize.zw) * (SizeAndInvSize.xy / (SizeAndInvSize.xy + 1.0f));
}

/// Inverse: convert sub-texel coordinates back to unit [0,1]
float2 FromSubUvsToUnit(float2 uv, float4 SizeAndInvSize)
{
    return (uv - 0.5f * SizeAndInvSize.zw) * (SizeAndInvSize.xy / (SizeAndInvSize.xy - 1.0f));
}
```

**Note**: In UE5, the sub-texel adjustment is commented as "No real impact so off" (line 215, 191). For UzEngine, you can skip this unless you observe edge artifacts.

## Dispatcher Example (C++)

From `SkyAtmosphereRendering.cpp` lines 1368-1378.

```cpp
// Assuming you have GlobalShaderMap and GraphBuilder
FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
if (CVarSkyAtmosphereTransmittanceLUT.GetValueOnRenderThread() > 0)
{
    TShaderMapRef<FRenderTransmittanceLutCS> ComputeShader(GlobalShaderMap);

    FRenderTransmittanceLutCS::FParameters * PassParameters =
        GraphBuilder.AllocParameters<FRenderTransmittanceLutCS::FParameters>();
    PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
    PassParameters->SkyAtmosphere = SkyInfo.GetInternalCommonParametersUniformBuffer();
    PassParameters->TransmittanceLutUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(TransmittanceLut, 0));

    FIntVector TextureSize = TransmittanceLut->Desc.GetSize();
    TextureSize.Z = 1;
    const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderTransmittanceLutCS::GroupSize);
    FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TransmittanceLut"),
        PassFlag, ComputeShader, PassParameters, NumGroups);
}
```

**For UzEngine (simplified, no RenderGraph)**:

```cpp
// Compute shader dispatcher
UINT GroupCountX = (TransmittanceLutWidth + 7) / 8;  // Round up to 8-wide groups
UINT GroupCountY = (TransmittanceLutHeight + 7) / 8;
CommandList->Dispatch(GroupCountX, GroupCountY, 1);
```

## Key Differences for UzEngine Porting

1. **Remove UE5 macros**: `TRANSMITTANCE_PASS`, `SHADING_PATH_MOBILE`, `WHITE_TRANSMITTANCE`
2. **Simplify atmospheric integration**: `IntegrateSingleScatteredLuminance` is heavy; you'll need your own ray-march implementation
3. **No render graph**: Use DX12 API directly for texture creation and UAV binding
4. **No global shader system**: Manually manage PSO/root sig registration
5. **Direct constant buffers**: No UE5's `UniformBuffer` abstraction; use `ID3D12Resource` buffers directly

## Quick Checklist

- [ ] Copy `getTransmittanceLutUvs` / `fromTransmittanceLutUVs` unchanged
- [ ] Copy compute shader kernel, remove UE5 macro branches
- [ ] Replace `IntegrateSingleScatteredLuminance` with your own ray-march function
- [ ] Create DX12 texture (256×64, R32G32B32_FLOAT)
- [ ] Create UAV for compute write
- [ ] Create SRV for runtime sampling
- [ ] Dispatch with (32, 8, 1) thread groups (for 256×64 LUT with 8×8 threads)
- [ ] Transition resource to SRV after compute dispatch
- [ ] Add runtime lookup in sky pixel shader
- [ ] Test with white transmittance first (define WHITE_TRANSMITTANCE)
- [ ] Enable real transmittance computation and verify sun darkens at horizon

