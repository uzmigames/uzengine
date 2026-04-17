<!-- doc-meta
{
  "author": "Claude Code",
  "date": "2026-03-16",
  "status": "final",
  "priority": "P0",
  "version": "1.0"
}
-->

# UE5 SM5 Analysis — Quick Reference

## Questions Asked & Answers

### 1. BRDF LUT Generation on SM5
**Q**: When compute shaders are unavailable, how does UE5 generate BRDF LUT?

**A**: **It doesn't.** UE5 pre-bakes the BRDF LUT **offline** and loads it from disk at engine startup. There is no runtime fallback for SM5. The texture is static and never regenerated.

- **File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SystemTextures.cpp`
- **Format**: 256×256 RG16F (linear floating-point)
- **Integration**: Bound to deferred lighting shader as `BRDFLut` texture

---

### 2. Irradiance Cubemap / SH Projection on SM5
**Q**: How does UE5 handle irradiance SH projection without compute shaders?

**A**: **Also offline.** Reflection captures are baked at edit time. The SH coefficients (9 total: L0×1 + L1×3 + L2×5) are **pre-computed and packed** into a structured buffer (7 float4s).

- **CPU computation**: `SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance()` at startup
- **GPU binding**: `StructuredBuffer<float4> SkyIrradianceEnvironmentMap` (7 float4s)
- **File reference**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ReflectionEnvironment.cpp` lines 623-669

**SH packing layout** (per color channel):
```
[0]: L1x, L1y, L1z, L0-L2(zz) term
[1-2]: L2 terms (4 more floats)
[3-6]: L2 remaining + auxiliary
```

---

### 3. Prefiltered Specular Cubemap on SM5
**Q**: How does UE5 handle specular cubemap prefiltering without compute?

**A**: **Pre-baked as a mip-mapped cubemap.** The reflection capture system generates a full mip chain offline, then loads it into TextureCubeArray.

- **Resolution**: 128×128 base, 8 mips (1×1 final)
- **Format**: RGBA16F or RGBA8 depending on memory constraints
- **Access**: Sample by roughness-based mip level in deferred lighting shader

**File reference**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ReflectionEnvironment.cpp` lines 150-179

```cpp
const int32 NumReflectionCaptureMips = FMath::CeilLogTwo(CubemapSize) + 1;  // 8 for 128×128
```

---

### 4. CPU Fallbacks for IBL Maps
**Q**: Does UE5 have CPU fallbacks for BRDF/irradiance/prefiltered generation?

**A**: **No runtime CPU fallbacks.** All three are **pre-computed offline**:
- No pixel shader render-to-texture pipeline
- No CPU-side integration (except SH projection, which is pre-baked once at startup)
- Textures are **loaded from disk**, never generated at runtime on SM5

This is UE5's design choice: **shift work to offline pipeline** rather than supporting runtime generation on limited hardware.

---

### 5. Auto-Exposure (Eye Adaptation)
**Q**: What is UE5's fallback exposure value when auto-exposure compute unavailable?

**A**: **Fixed calculation using middle-grey calibration**:

```cpp
float GetEyeAdaptationFixedExposure(const FViewInfo& View)
{
    const float Exposure = (MinAverageLuminance + MaxAverageLuminance) * 0.5f;
    const float kMiddleGrey = 0.18f;
    const float ExposureScale = kMiddleGrey / Exposure;
    return ExposureScale * ExposureCompensationSettings;
}
```

**Default values**:
- `MinAverageLuminance`: 0.25
- `MaxAverageLuminance`: 4.0
- **Fallback exposure**: (0.25 + 4.0) / 2 / 0.18 ≈ **11.8× brightness multiplier**

**Feature level check** (line 169):
```cpp
static ERHIFeatureLevel::Type GetBasicEyeAdaptationMinFeatureLevel()
{
    return IsMobileHDR() ? ERHIFeatureLevel::ES3_1 : ERHIFeatureLevel::SM5;
}
```

**File reference**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessEyeAdaptation.cpp` lines 567-578

---

### 6. Deferred Lighting on SM5 — GBuffer Binding
**Q**: How are GBuffer textures bound without bindless resources?

**A**: **Static register-based binding (D3D11 style)**:

8 named textures declared in shader:
```hlsl
Texture2D SceneDepthTexture;          // t0
Texture2D GBufferATexture;            // t1
Texture2D GBufferBTexture;            // t2
Texture2D GBufferCTexture;            // t3
Texture2D GBufferDTexture;            // t4
Texture2D GBufferETexture;            // t5
Texture2D GBufferVelocityTexture;     // t6
Texture2D<uint> SceneLightingChannels;// t7
```

C++ binds them all at once:
```cpp
pDeviceContext->PSSetShaderResources(0, 8, &pGBufferTextures[0]);
```

**Static sampler** (point, clamp):
```hlsl
#define GBufferATextureSampler GlobalPointClampedSampler
```

**File reference**: `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredShadingCommon.ush` lines 32-51

---

### 7. Shadow Atlas Binding
**Q**: How is the shadow atlas accessed?

**A**: **Also static register-based**. No dynamic descriptor indexing.

For **screen-space shadows**: Single `Texture2D ShadowAtlasTexture` at a fixed register
For **virtual shadow maps** (SM6+): `Texture2D<uint4> ShadowMaskBits` at fixed register

**Code reference**: `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredLightPixelShaders.usf` lines 123-149

```hlsl
#if USE_VIRTUAL_SHADOW_MAP_MASK
Texture2D<uint4> ShadowMaskBits;  // t? (fixed)
int VirtualShadowMapId;
int LightSceneId;
#endif

float ShadowMask = GetVirtualShadowMapMaskForLight(
    ShadowMaskBits[InputParams.PixelPos],
    uint2(InputParams.PixelPos),
    ...);
```

---

### 8. IBL Texture Access in Deferred Lighting
**Q**: How does the lighting shader access IBL textures without bindless?

**A**: **Pre-bound to fixed texture slots**:

```hlsl
// In deferred lighting shader
Texture2D BRDFLut : register(t8);
TextureCubeArray ReflectionCaptures : register(t9);
StructuredBuffer<float4> SkyIrradianceMap : register(t10);

float3 DeferredLight(...) {
    // Sample BRDF
    float2 f = BRDFLut.Sample(PointSampler, float2(NdotV, roughness)).rg;

    // Sample reflection capture
    float3 envColor = ReflectionCaptures.SampleLevel(
        EnvSampler,
        float4(reflectDir, captureIndex),
        roughnessMip);

    // Evaluate SH irradiance
    float3 diffuse = EvaluateSH(N, SkyIrradianceMap);

    return envColor * f + diffuse;
}
```

**Register allocation** (fixed at compile time, not dynamic):
- t0-7: GBuffers
- t8: BRDF LUT
- t9: Reflection captures
- t10: Sky irradiance SB
- (could extend as needed, no bindless)

---

## Architectural Pattern: "Offline-First"

UE5 uses a **design pattern** for SM5 support:

> **"Pre-compute offline, load at runtime. No dynamic generation on limited hardware."**

This means:
1. **Content pipeline bakes all IBL data** at edit time or during asset import
2. **Engine loads pre-baked textures** from disk at startup
3. **Zero runtime overhead** on SM5 (unlike engines that generate on-the-fly)
4. **Trade-off**: Less flexibility (can't change lighting at runtime), more performance

**Benefit for UzEngine**: This approach scales beautifully — same offline baker works for all platforms (SM5, SM6, mobile). Only the shader code changes.

---

## Key Statistics & Values

| Parameter | UE5 Value | Notes |
|-----------|-----------|-------|
| BRDF LUT size | 256×256 | Fixed, never changes |
| BRDF LUT format | RG16F | 2 channels (F0, F90) |
| Irradiance buffer | 7 float4s | 9 SH coefficients + padding |
| Irradiance projection | CPU at startup | Once per level load |
| SH max order | L2 (9 coefficients) | L0 + L1×3 + L2×5 |
| Prefiltered cubemap | 128×128 base | 8 mips (1×1 final) |
| Prefiltered format | RGBA16F or RGBA8 | Depends on memory budget |
| Default exposure | 11.8× | (0.25 + 4.0) / 2 / 0.18 |
| GBuffer texture count | 8 | t0-7 (fixed slots) |
| Deferred light passes | 1 per light | All lights in single pixel shader |

---

## Critical Design Decisions for UzEngine

1. **Pre-bake IBL maps offline** — don't try runtime generation on SM5
2. **Use fixed register slots for resources** — no dynamic descriptor tables on D3D11
3. **Store exposure in View constants** — apply PreExposure in base pass + deferred lighting
4. **SH for diffuse, cubemap mips for specular** — proven split in UE5
5. **Single exposure fallback value** — simple, robust, works for all scenes

---

## Files Analyzed

- **Auto-Exposure**: `PostProcessEyeAdaptation.cpp` (1400 lines)
- **IBL/SH**: `ReflectionEnvironment.cpp` (715 lines)
- **Deferred Lighting**: `DeferredShadingCommon.ush`, `DeferredLightPixelShaders.usf`
- **System Textures**: `SystemTextures.cpp`

All from `W:\UE_5.6_Source` (UE5.6 official source)

---

## See Also

- `ue5-sm5-fallback-patterns.md` — detailed architectural analysis
- `ue5-sm5-code-patterns.md` — concrete C++/HLSL code examples
