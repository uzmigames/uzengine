<!-- doc-meta
title: Porting UE5 Transmittance LUT to UzEngine
author: Architecture Guidance
date: 2026-03-15
tags: [sky-atmosphere, transmittance, implementation, porting, dx12, compute-shader]
status: draft
version: 1.0
-->

# Porting UE5 Transmittance LUT to UzEngine

This guide shows how to adapt UE5's transmittance LUT system for UzEngine's DX12-native architecture.

## Overview

UE5's transmittance LUT system consists of:
1. **UV mapping functions** (encode/decode) — pure math, copy directly
2. **Compute shader** — adapt HLSL from UE5, remove UE5 macro dependencies
3. **C++ texture management** — create once, store permanently
4. **Constant buffer binding** — atmosphere parameters + LUT size
5. **Runtime lookup** — single texture sample in sky rendering pass

## Step 1: Add HLSL UV Mapping Functions

Create `shaders/include/sky_atmosphere.hlsli`:

```hlsl
/// Transmittance LUT parameterization from Bruneton 2017.
/// https://github.com/ebruneton/precomputed_atmospheric_scattering

/// Decode (ViewHeight, ViewZenithCosAngle) from normalized [0,1] UV coordinates.
/// Params:
///   UV: normalized [0,1] coordinates
///   BottomRadius, TopRadius: planet radius (km)
/// Outputs:
///   ViewHeight: distance from planet center (km)
///   ViewZenithCosAngle: cos of angle to zenith [-1, 1]
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

/// Encode (ViewHeight, ViewZenithCosAngle) to normalized [0,1] UV coordinates.
/// Inverse of fromTransmittanceLutUVs.
void getTransmittanceLutUvs(
    in float ViewHeight, in float ViewZenithCosAngle, in float BottomRadius, in float TopRadius,
    out float2 UV)
{
    float H = sqrt(max(0.0f, TopRadius * TopRadius - BottomRadius * BottomRadius));
    float Rho = sqrt(max(0.0f, ViewHeight * ViewHeight - BottomRadius * BottomRadius));

    float Discriminant = ViewHeight * ViewHeight * (ViewZenithCosAngle * ViewZenithCosAngle - 1.0f) + TopRadius * TopRadius;
    float D = max(0.0f, (-ViewHeight * ViewZenithCosAngle + sqrt(Discriminant)));

    float Dmin = TopRadius - ViewHeight;
    float Dmax = Rho + H;
    float Xmu = (D - Dmin) / (Dmax - Dmin);
    float Xr = Rho / H;

    UV = float2(Xmu, Xr);
}
```

## Step 2: Write the Compute Shader

Create `shaders/runtime/compute/transmittance_lut_cs.hlsl`:

```hlsl
#include "../include/sky_atmosphere.hlsli"
#include "../include/sky_common.hlsli"  // or whatever your atmosphere marching is in

// Constants
static const uint ThreadGroupSize = 8;

// Constant buffer: atmosphere parameters
cbuffer AtmosphereParams : register(b0)
{
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

    float BottomRadiusKm;
    float TopRadiusKm;
    float PlanetCenterZ;  // Z offset for planet center

    // Padding to 256 bytes
    float _pad[6];
};

// Constant buffer: LUT metadata
cbuffer LutParams : register(b1)
{
    uint2 TransmittanceLutSize;
    float2 TransmittanceLutInvSize;  // 1.0 / size
};

// Output LUT
RWTexture2D<float3> TransmittanceLutUAV : register(u0);

/// Sample atmosphere medium (simplified; adapt to your actual sky shader)
struct MediumSample
{
    float3 Scattering;
    float3 Absorption;
    float3 Extinction;
};

MediumSample SampleMedium(float Height)
{
    MediumSample s;

    // Height-dependent densities
    float DensityRay = exp(RayleighDensityExpScale * Height);
    float DensityMie = exp(MieDensityExpScale * Height);
    float DensityOzo = Height < AbsorptionDensity0LayerWidth
        ? saturate(AbsorptionDensity0LinearTerm * Height + AbsorptionDensity0ConstantTerm)
        : saturate(AbsorptionDensity1LinearTerm * Height + AbsorptionDensity1ConstantTerm);

    s.Scattering = DensityRay * RayleighScattering + DensityMie * MieScattering;
    s.Absorption = DensityMie * MieAbsorption + DensityOzo * OzoneAbsorption;
    s.Extinction = s.Scattering + s.Absorption;

    return s;
}

/// Ray-march from ViewPos along ViewDir to compute optical depth
/// Simplified: integrate extinction along the ray toward atmosphere boundary
float3 ComputeOpticalDepth(float3 ViewPos, float3 ViewDir, uint SampleCount)
{
    float3 OpticalDepth = float3(0.0f, 0.0f, 0.0f);

    float RayLength = 0.0f;  // Distance to atmosphere boundary (compute this properly)
    float Step = RayLength / SampleCount;

    for (uint i = 0; i < SampleCount; ++i)
    {
        float3 SamplePos = ViewPos + ViewDir * (RayLength * float(i) / float(SampleCount));
        float Height = length(SamplePos) - BottomRadiusKm;

        MediumSample Medium = SampleMedium(Height);
        OpticalDepth += Medium.Extinction * Step;
    }

    return OpticalDepth;
}

[numthreads(ThreadGroupSize, ThreadGroupSize, 1)]
void main(uint3 ThreadId : SV_DispatchThreadID)
{
    uint2 PixPos = ThreadId.xy;

    // Out of bounds check
    if (any(PixPos >= TransmittanceLutSize))
        return;

    // Convert pixel position to normalized UV
    float2 UV = (float2(PixPos) + 0.5f) * TransmittanceLutInvSize;

    // Decode to physical parameters
    float ViewHeight;
    float ViewZenithCosAngle;
    fromTransmittanceLutUVs(ViewHeight, ViewZenithCosAngle, BottomRadiusKm, TopRadiusKm, UV);

    // Build ray from decoded parameters
    float3 WorldPos = float3(0.0f, 0.0f, ViewHeight);
    float ViewZenithSinAngle = sqrt(1.0f - ViewZenithCosAngle * ViewZenithCosAngle);
    float3 WorldDir = float3(0.0f, ViewZenithSinAngle, ViewZenithCosAngle);

    // Ray march to compute optical depth
    float3 OpticalDepth = ComputeOpticalDepth(WorldPos, WorldDir, 10);

    // Convert optical depth to transmittance via Beer-Lambert law
    float3 Transmittance = exp(-OpticalDepth);

    // Store result
    TransmittanceLutUAV[int2(PixPos)] = Transmittance;
}
```

## Step 3: C++ Texture Creation and Dispatch

Add to your sky atmosphere system (e.g., `engine/renderer/sky_atmosphere.h/cpp`):

```cpp
/// Transmittance LUT precomputation
class TransmittanceLut
{
public:
    static constexpr uint32_t DefaultWidth = 256;
    static constexpr uint32_t DefaultHeight = 64;

    /// Create LUT texture (call once at startup)
    void Create(ID3D12Device* Device, uint32_t Width = DefaultWidth, uint32_t Height = DefaultHeight)
    {
        LutWidth = Width;
        LutHeight = Height;

        D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32G32B32_FLOAT,
            Width, Height,
            1,  // array size
            1   // mip levels
        );
        Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES HeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(Device->CreateCommittedResource(
            &HeapProps,
            D3D12_HEAP_FLAG_NONE,
            &Desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&LutResource)));

        // Create UAV
        D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc{};
        UavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        UavDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        UavDesc.Texture2D.MipSlice = 0;

        LutUavHandle = DescriptorAllocator::Allocate(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        Device->CreateUnorderedAccessView(LutResource.Get(), nullptr, &UavDesc, LutUavHandle.Cpu);

        // Create SRV for runtime sampling
        D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc{};
        SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        SrvDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SrvDesc.Texture2D.MipLevels = 1;

        LutSrvHandle = DescriptorAllocator::Allocate(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        Device->CreateShaderResourceView(LutResource.Get(), &SrvDesc, LutSrvHandle.Cpu);
    }

    /// Dispatch compute shader to generate LUT
    void Compute(ID3D12GraphicsCommandList* CmdList, const AtmosphereParams& Params,
                 ID3D12PipelineState* ComputePso, ID3D12RootSignature* RootSig)
    {
        // Setup command list state
        CmdList->SetPipelineState(ComputePso);
        CmdList->SetComputeRootSignature(RootSig);

        // Upload atmosphere params
        // (Assume you have a constant buffer upload mechanism)
        D3D12_GPU_VIRTUAL_ADDRESS AtmosphereCbvGpu = UploadConstantBuffer(Params);
        CmdList->SetComputeRootConstantBufferView(0, AtmosphereCbvGpu);

        // Upload LUT metadata
        struct LutMetadata
        {
            uint32_t Width, Height;
            float InvWidth, InvHeight;
        } LutMeta{ LutWidth, LutHeight, 1.0f / LutWidth, 1.0f / LutHeight };

        D3D12_GPU_VIRTUAL_ADDRESS LutCbvGpu = UploadConstantBuffer(LutMeta);
        CmdList->SetComputeRootConstantBufferView(1, LutCbvGpu);

        // Set UAV
        CmdList->SetComputeRootDescriptorTable(2, LutUavHandle.Gpu);

        // Dispatch: 8x8 thread groups
        uint32_t GroupCountX = (LutWidth + 7) / 8;
        uint32_t GroupCountY = (LutHeight + 7) / 8;
        CmdList->Dispatch(GroupCountX, GroupCountY, 1);

        // Transition back to SRV for sampling
        D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            LutResource.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
        );
        CmdList->ResourceBarrier(1, &Barrier);
    }

    /// Get SRV handle for sampling in sky rendering pass
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle() const { return LutSrvHandle.Gpu; }

private:
    ComPtr<ID3D12Resource> LutResource;
    DescriptorHandle LutUavHandle, LutSrvHandle;
    uint32_t LutWidth, LutHeight;
};
```

## Step 4: Integration into Sky Renderer

In your sky rendering pass:

```cpp
void SkyRenderer::RenderSky(ID3D12GraphicsCommandList* CmdList, const Scene& Scene)
{
    // Ensure transmittance LUT is computed (once per scene)
    if (!SkyAtmosphere.IsLutComputed())
    {
        SkyAtmosphere.ComputeTransmittanceLut(CmdList);
    }

    // Sample transmittance in pixel shader
    // In HLSL:
    // float3 transmittance = TransmittanceLutTexture.Sample(LinearClampSampler, uv);
}
```

In your sky pixel shader (`shaders/runtime/geometry/sky_ps.hlsl`):

```hlsl
Texture2D TransmittanceLutTexture : register(t0);
SamplerState LinearClampSampler : register(s0);

float3 GetTransmittance(float ViewHeight, float ViewZenithCosAngle, float BottomRadius, float TopRadius)
{
    float2 UV;
    getTransmittanceLutUvs(ViewHeight, ViewZenithCosAngle, BottomRadius, TopRadius, UV);
    return TransmittanceLutTexture.Sample(LinearClampSampler, UV).rgb;
}

float4 main(PixelInput Input) : SV_Target
{
    // ... sky computation ...

    // Apply transmittance attenuation to sun disk
    float3 TransmittanceToSun = GetTransmittance(ViewHeight, ViewZenithCosAngle, 6360.0f, 6460.0f);
    float3 SunDiskColor = /* ... */ * TransmittanceToSun;

    // ... rest of sky computation ...
}
```

## Step 5: Configuration and CVars

Add configuration options:

```cpp
// In your cvar system
ConsoleVariable_Int CVar_TransmittanceLutEnabled("sky.transmittance_lut_enabled", 1, "Enable transmittance LUT");
ConsoleVariable_Int CVar_TransmittanceLutWidth("sky.transmittance_lut_width", 256, "LUT width");
ConsoleVariable_Int CVar_TransmittanceLutHeight("sky.transmittance_lut_height", 64, "LUT height");
ConsoleVariable_Int CVar_TransmittanceLutSampleCount("sky.transmittance_lut_samples", 10, "Ray march samples");
```

## Checklist

- [ ] Copy UV mapping functions to `sky_atmosphere.hlsli`
- [ ] Adapt compute shader, remove UE5-specific macros
- [ ] Create DX12 texture and UAV/SRV descriptors
- [ ] Implement compute dispatch with proper thread group count
- [ ] Add constant buffer binding for atmosphere params
- [ ] Transition resource state from UAV → SRV after compute
- [ ] Integrate into sky rendering pass
- [ ] Add runtime lookup function in pixel shader
- [ ] Test with single-color atmosphere first (white transmittance)
- [ ] Verify transmittance darkens sun at horizon
- [ ] Profile precomputation time (should be <50ms)
- [ ] Document transmittance LUT in architecture docs

## Performance Tips

1. **Precompute once**: Don't recompute every frame. Store in a persistent texture pool.
2. **Async compute**: Dispatch on async compute queue if available (doesn't block rendering).
3. **Downsampling**: For mobile/low-end, try 128×32 instead of 256×64.
4. **8-bit format fallback**: If memory is tight, use R8G8B8A8 instead of R32G32B32F (loses precision but still reasonable).
5. **Sample count trade-off**: 6 samples is minimum, 20+ is overkill. 10 is the sweet spot.

## References

- **UE5 Source**: W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf
- **Bruneton 2017**: https://hal.science/hal-01985130
- **Reference Implementation**: https://github.com/ebruneton/precomputed_atmospheric_scattering

