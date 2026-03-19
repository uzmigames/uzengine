<!-- doc-meta
title: Transmittance LUT — Quick Reference Card
author: Architecture Team
date: 2026-03-15
tags: [quick-reference, cheat-sheet, transmittance-lut]
status: complete
version: 1.0
-->

# Transmittance LUT — Quick Reference Card

## One-Paragraph Summary

Transmittance = how much light from the sun reaches the observer through the atmosphere. UE5 precomputes this for a 2D grid of (height, zenith_angle) pairs at 256×64 resolution using a compute shader, stores in a float3 texture (~192 KB), and looks up at runtime with O(1) cost. The UV parameterization uses Bruneton's non-linear scheme to maximize fidelity near the horizon where transmittance changes rapidly.

## Key Numbers

| Parameter | Value | Notes |
|-----------|-------|-------|
| **LUT Width** | 256 | Default, configurable CVar |
| **LUT Height** | 64 | Default, configurable CVar |
| **Texture Format** | R32G32B32F | Desktop; R32G32B32A32F on mobile |
| **Memory** | ~192 KB | (256×64×12 bytes) |
| **Thread Group Size** | 8×8 | Compute shader |
| **Dispatch Grid** | 32×8 groups | (256/8) × (64/8) |
| **Total Threads** | 16,384 | One per texel |
| **Ray Samples/Pixel** | 10 | Configurable CVar |
| **Precomputation Time** | 10–50 ms | GPU-dependent |
| **Runtime Cost/Lookup** | 2 cycles | 1 encode + 1 sample |
| **Precomputation Frequency** | Once per scene | Not per-frame |

## Core Equations

### UV → Parameters (Decode)

```glsl
H = sqrt(TopRadius² - BottomRadius²)      // Atmosphere thickness (km)
Rho = H · UV.y                             // Horizontal distance (km)
ViewHeight = sqrt(Rho² + BottomRadius²)   // Radius from planet center (km)

Dmin = TopRadius - ViewHeight
Dmax = Rho + H
D = Dmin + UV.x · (Dmax - Dmin)            // Ray distance to boundary
ViewZenithCosAngle = (H² - Rho² - D²) / (2·ViewHeight·D)
```

### Parameters → UV (Encode)

```glsl
H = sqrt(TopRadius² - BottomRadius²)
Rho = sqrt(ViewHeight² - BottomRadius²)
D = max(0, -ViewHeight·cos(θ) + sqrt(discriminant))

Dmin = TopRadius - ViewHeight
Dmax = Rho + H
UV.x = (D - Dmin) / (Dmax - Dmin)
UV.y = Rho / H
```

## Compute Shader Template

```hlsl
[numthreads(8, 8, 1)]
void RenderTransmittanceLutCS(uint3 ThreadId : SV_DispatchThreadID)
{
    float2 PixPos = float2(ThreadId.xy) + 0.5;
    float2 UV = PixPos * InvLutSize;

    // Decode
    float ViewHeight, ViewZenithCosAngle;
    fromTransmittanceLutUVs(ViewHeight, ViewZenithCosAngle, BottomRadius, TopRadius, UV);

    // Build ray
    float3 WorldPos = float3(0, 0, ViewHeight);
    float SinZenith = sqrt(1 - ViewZenithCosAngle * ViewZenithCosAngle);
    float3 WorldDir = float3(0, SinZenith, ViewZenithCosAngle);

    // Ray march
    float3 OpticalDepth = RayMarch(WorldPos, WorldDir, 10);

    // Convert to transmittance
    float3 Transmittance = exp(-OpticalDepth);

    TransmittanceLutUAV[int2(PixPos)] = Transmittance;
}
```

## Runtime Lookup

```hlsl
// In pixel shader
float2 UV;
getTransmittanceLutUvs(ViewHeight, ViewZenithCosAngle, BottomRadius, TopRadius, UV);
float3 Transmittance = TransmittanceLutTexture.Sample(LinearClampSampler, UV).rgb;

// Apply to sky
float3 SkyColor = ... * Transmittance;
```

## DX12 Integration Checklist

- [ ] Create texture: `D3D12_RESOURCE_DESC` for 256×64 R32G32B32_FLOAT
- [ ] Create UAV: `D3D12_UNORDERED_ACCESS_VIEW_DESC` for compute write
- [ ] Create SRV: `D3D12_SHADER_RESOURCE_VIEW_DESC` for pixel shader sample
- [ ] Create PSO: Compute pipeline state for shader
- [ ] Create root signature: 2 CBVs (Atmosphere, LutMetadata) + 1 UAV
- [ ] Dispatch: `Dispatch(32, 8, 1)` thread groups
- [ ] Barrier: Transition UAV → SRV after compute
- [ ] Sample: Use `Sample(LinearClampSampler, UV)` in PS

## Performance Tips

1. **Precompute once** — Store result in permanent texture pool
2. **Async compute** — Dispatch on async queue if available
3. **Sample count trade-off** — 6 = minimum, 10 = balanced, 20+ = overkill
4. **Resolution trade-off** — 128×32 for mobile, 256×64 for desktop
5. **Format trade-off** — R8G8B8A8 loses 24-bit precision but saves 75% memory
6. **Caching** — Scene-static; recompute only if atmosphere parameters change

## Atmosphere Parameters (Earth)

```cpp
BottomRadiusKm = 6360    // Sea level
TopRadiusKm = 6460       // 100 km atmosphere
RayleighScattering = { 5.8e-6, 13.5e-6, 33.1e-6 }  // RGB per km
MieScattering = { 2.1e-5, 2.1e-5, 2.1e-5 }  // Per km (colorless)
MieAbsorption = { 4.4e-6, 4.4e-6, 4.4e-6 }  // Per km
```

## Expected Output

| Zenith Angle | Height | Expected Transmittance | Appearance |
|--------------|--------|------------------------|------------|
| 0° (zenith) | Sea level | (1.0, 1.0, 1.0) | White |
| 0° (zenith) | 50 km | (0.95, 0.95, 0.95) | Nearly white |
| 90° (horizon) | Sea level | (0.4, 0.65, 0.95) | Orange/reddish |
| 90° (horizon) | 50 km | (0.8, 0.85, 0.9) | Pale blue |

(These are approximate; exact values depend on atmosphere model parameters)

## Validation Tests

1. **White transmittance** — Return 1.0, sky should appear fully bright
2. **Single channel** — Return (1,0,0) for red, verify LUT channel
3. **Zenith vs horizon** — Transmittance should decrease moving toward horizon
4. **Sun disk test** — Sun should appear orange/red at sunset when transmittance applied

## References

- **Full Analysis**: `docs/analysis/rendering/ue5-sky-atmosphere-transmittance-lut.md`
- **HLSL Snippets**: `docs/analysis/rendering/ue5-transmittance-lut-hlsl-snippets.md`
- **Porting Guide**: `docs/guides/ue5-transmittance-lut-porting.md`
- **Paper**: Bruneton et al., SIGGRAPH 2017

## File Paths (UE5 Source)

```
W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf
  └─ Lines 1099–1138: RenderTransmittanceLutCS compute shader
  └─ Lines 346–356: GetTransmittance() lookup function

W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush
  └─ Lines 156–172: fromTransmittanceLutUVs() (decode)
  └─ Lines 175–192: getTransmittanceLutUvs() (encode)

W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp
  └─ Lines 151–174: CVar definitions
  └─ Lines 1364–1379: Compute dispatch code
```

## Common Mistakes

❌ **Don't** recompute every frame — precompute once per scene
❌ **Don't** forget resource barriers — transition UAV → SRV
❌ **Don't** use nearest filtering — use linear clamp for smooth interpolation
❌ **Don't** skip the clamping in `ViewZenithCosAngle` — can cause NaN from sqrt
❌ **Don't** ignore the planet shadow — implement ray-sphere intersection check

✅ **Do** validate with white transmittance first
✅ **Do** test horizon darkening visually
✅ **Do** profile precomputation time
✅ **Do** use correct dispatch grid: (Width+7)/8, (Height+7)/8

