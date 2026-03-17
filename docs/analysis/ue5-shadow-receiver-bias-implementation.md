<!-- doc-meta -->
# UE5 Shadow Receiver Bias — Implementation Reference

**Purpose**: Exact code snippets from UE5 for implementing receiver bias in UzEngine
**Status**: Ready to implement
**Source**: UE5.6 source analysis

---

## Pattern Overview

Shadow receiver bias in UE5 works in 3 stages:

1. **C++ Calculation**: Map console variable `[0, 1]` → shader parameter `[1, 0]`
2. **Shader Sampling**: Modulate `TransitionScale` by `lerp(bias_min, 1.0, NoL)` per-pixel
3. **PCF Filtering**: Use modulated TransitionScale in depth comparison gradient

---

## Stage 1: C++ Receiver Bias Calculation

**Source**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp:1967-1984`

```cpp
/// Maps console variable to shader parameter
/// @param LightType: directional, spot, rect, or point
/// @return receiver bias min value [0, 1], where:
///   - 0.0 = maximum bias when perpendicular to light
///   - 1.0 = no bias at all
float GetShaderReceiverDepthBias(ELightComponentType LightType)
{
    // Step 1: Get console variable value [0, 1]
    float ShadowReceiverBias = 1.0f;  // Default: no bias
    switch (LightType)
    {
    case LightType_Directional:
        ShadowReceiverBias = 0.9f;  // r.Shadow.CSMReceiverBias (line 76)
        break;
    case LightType_Rect:
        ShadowReceiverBias = 0.3f;  // r.Shadow.RectLightReceiverBias (line 111)
        break;
    case LightType_Spot:
        ShadowReceiverBias = 0.5f;  // r.Shadow.SpotLightReceiverBias (line 150)
        break;
    case LightType_Point:
        ShadowReceiverBias = GetShaderSlopeDepthBias();  // Uses slope bias
        break;
    }

    // Step 2: Clamp to [0, 1]
    ShadowReceiverBias = FMath::Clamp(ShadowReceiverBias, 0.0f, 1.0f);

    // Step 3: Invert to get min lerp value for shader
    // Input range: [0=max_bias, 1=no_bias]
    // Output range: [1.0=no_bias, 0.0=max_bias] for shader
    return 1.0f - ShadowReceiverBias;
}
```

**Console Variables to Add** (in C++ shadow rendering code):

```cpp
static TAutoConsoleVariable<float> CVarCSMShadowReceiverBias(
    TEXT("r.Shadow.CSMReceiverBias"),
    0.9f,
    TEXT("Receiver bias used by CSM. Value between 0 and 1.\n")
    TEXT("  0.0 = Maximum softness (high bias)\n")
    TEXT("  1.0 = No bias (hard shadow edges)\n"),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarSpotLightShadowReceiverBias(
    TEXT("r.Shadow.SpotLightReceiverBias"),
    0.5f,
    TEXT("Receiver bias used by spotlights. Value between 0 and 1."),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRectLightShadowReceiverBias(
    TEXT("r.Shadow.RectLightReceiverBias"),
    0.3f,
    TEXT("Receiver bias used by rect light. Value between 0 and 1."),
    ECVF_RenderThreadSafe);
```

**Pass to Shader** (in SetShaderParameters function):

```cpp
// In your shadow projection pass setup code
FVector4f ProjectionDepthBiasParameters;
ProjectionDepthBiasParameters.X = DepthBias;  // Constant depth bias
ProjectionDepthBiasParameters.Y = SlopeDepthBias;  // Slope scale
ProjectionDepthBiasParameters.Z = GetShaderReceiverDepthBias(LightType);  // Receiver bias min
ProjectionDepthBiasParameters.W = MaxSubjectZ - MinSubjectZ;  // Optional

SetShaderValue(CommandList, PixelShader, ProjectionDepthBiasParameters_Param, ProjectionDepthBiasParameters);
```

---

## Stage 2: Shader Receiver Bias Application

**Source**: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowProjectionPixelShader.usf:226-262`

### Header Definition

```hlsl
/// Shader parameters for shadow projection
// .x: DepthBias
// .y: SlopeDepthBias
// .z: ReceiverBias (min lerp value for NoL modulation)
// .w: MaxSubjectZ - MinSubjectZ
float4 ProjectionDepthBiasParameters;
```

### Per-Pixel Receiver Bias Computation (DEFERRED PATH ONLY)

```hlsl
// File: ShadowProjectionPixelShader.usf
// Lines: 226-251
// This code runs ONLY in deferred shading, NOT forward

#if SHADING_PATH_DEFERRED && !FORWARD_SHADING && !SUBPIXEL_SHADOW
    // Step 1: Compute light direction (different for directional vs local)
    const bool bIsDirectional = LightPositionOrDirection.w == 0;
    const float3 LightDirection = bIsDirectional ?
        -LightPositionOrDirection.xyz :  // Directional: use direction
        normalize(LightPositionOrDirection.xyz - TranslateWorldPosition);  // Local: compute direction

    // Step 2: Fetch world-space normal from GBuffer
    // NOTE: Must match your GBuffer format (Substrate vs traditional)
#if SUBTRATE_GBUFFER_FORMAT==1
    const FSubstrateTopLayerData TopLayerData =
        SubstrateUnpackTopLayerData(Substrate.TopLayerTexture.Load(uint3(SVPos.xy, 0)));
    const float3 WorldNormal = TopLayerData.WorldNormal;
#else
    FGBufferData GBufferData = GetGBufferData(ScreenUV);
    const float3 WorldNormal = GBufferData.WorldNormal;
#endif

    // Step 3: Compute NoL (Normal · Light)
    const float NoL = saturate(dot(WorldNormal, LightDirection));

    // Step 4: Modulate TransitionScale by receiver bias
    // ProjectionDepthBiasParameters.z = bias min value [0, 1]
    // lerp(0.1, 1.0, 0.5) = 0.55  when NoL=0.5
    Settings.TransitionScale = SoftTransitionScale.z *
        lerp(ProjectionDepthBiasParameters.z, 1.0, NoL);
#else
    // FORWARD path: no receiver bias (UE5 limitation)
    Settings.TransitionScale = SoftTransitionScale.z;
#endif
```

### PCF Settings Structure

```hlsl
struct FPCFSamplerSettings
{
    Texture2D ShadowDepthTexture;
    SamplerState ShadowDepthTextureSampler;
    float4 ShadowBufferSize;  // xy=size, zw=1/size
    float SceneDepth;  // Depth in light space
    float TransitionScale;  // **KEY**: Modulated by receiver bias
    bool bSubsurface;
    bool bTreatMaxDepthUnshadowed;
    float DensityMulConstant;
    float2 ProjectionDepthBiasParameters;
};
```

---

## Stage 3: PCF Gradient Application

**Source**: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowFilteringCommon.ush:151-180`

The `TransitionScale` (modified by receiver bias) controls the softness of the shadow edge:

```hlsl
/// @source UE5 ShadowFilteringCommon.ush:151-180
/// Calculates shadow occlusion using soft transition based on depth
float4 CalculateOcclusion(float4 ShadowmapDepth, FPCFSamplerSettings Settings)
{
    // NOTE: This is the CORE of shadow softness
    // TransitionScale parameter controls gradient steepness
    float TransitionScale = Settings.TransitionScale;

    // Unoptimized: saturate((ShadowmapDepth - SceneDepth) * TransitionScale + 1)
    // Optimized form (same result):
    float ConstantFactor = (Settings.SceneDepth * TransitionScale - 1);
    float4 ShadowFactor = saturate(ShadowmapDepth * TransitionScale - ConstantFactor);

    return ShadowFactor;  // [0, 1]: 0=fully shadowed, 1=fully lit
}
```

**Impact of TransitionScale values**:

```
TransitionScale = 10.0 (high, hard edge):
  ShadowmapDepth=0.99: ShadowFactor = saturate(9.9 - 9) = saturate(0.9) = 0.9 → nearly lit
  ShadowmapDepth=0.98: ShadowFactor = saturate(9.8 - 9) = saturate(0.8) = 0.8 → partially lit
  ShadowmapDepth=0.97: ShadowFactor = saturate(9.7 - 9) = saturate(0.7) = 0.7 → mostly shadowed

TransitionScale = 1.0 (low, soft edge):
  ShadowmapDepth=0.99: ShadowFactor = saturate(0.99 - 0.9) = saturate(0.09) = 0.09 → mostly shadowed
  ShadowmapDepth=0.98: ShadowFactor = saturate(0.98 - 0.9) = saturate(0.08) = 0.08 → mostly shadowed
  ShadowmapDepth=0.97: ShadowFactor = saturate(0.97 - 0.9) = saturate(0.07) = 0.07 → mostly shadowed
```

**With Receiver Bias**:

```
NoL = 0.2 (surface at grazing angle to light):
  ReceiverBias = 0.1 (from CSM receiver bias)
  ModulatedBias = lerp(0.1, 1.0, 0.2) = 0.1 + 0.18 = 0.28
  TransitionScale = 60 * 0.28 = 16.8 (SOFT edge)

NoL = 1.0 (surface facing light):
  ModulatedBias = lerp(0.1, 1.0, 1.0) = 1.0
  TransitionScale = 60 * 1.0 = 60.0 (HARD edge)
```

---

## Shadow Attenuation Encoding/Decoding

**Source**: `W:\UE_5.6_Source\Engine\Shaders\Private\Common.ush:1202-1225`

### Encoding (in Shadow Projection Shader)

```hlsl
/// @source UE5 Common.ush:1202-1207
/// Converts linear shadow value to sqrt-encoded for storage
/// Allocates more bits for dark values, reduces banding
MaterialFloat4 EncodeLightAttenuation(MaterialFloat4 InColor)
{
    return sqrt(InColor);
}
```

**Usage in shadow projection**:

```hlsl
float FadedShadow = lerp(1.0f, Shadow, ShadowFadeFraction);  // [0, 1]
OutColor = EncodeLightAttenuation(half4(FadedShadow, ...));  // Store as sqrt
```

### Decoding (in Deferred Lighting Shader)

```hlsl
/// @source UE5 Common.ush:1216-1219
/// Converts sqrt-encoded value back to linear
MaterialFloat4 DecodeLightAttenuation(MaterialFloat4 InColor)
{
    return Square(InColor);  // Equivalent to InColor * InColor
}
```

**Usage in deferred lighting**:

```hlsl
float4 LightAttenuation = GetPerPixelLightAttenuation(ScreenUV);  // Read encoded value
float Shadow = Square(LightAttenuation.x);  // Decode: sqrt(x)^2 = x
float FinalColor = DirectLightColor * Shadow;
```

---

## Integration Checklist

### 1. C++ Side

- [ ] Add receiver bias console variables (CSM, Spot, Rect)
- [ ] Implement `GetShaderReceiverDepthBias()` function
- [ ] Set `ProjectionDepthBiasParameters.z` when setting shadow shader parameters
- [ ] Ensure all light types pass correct receiver bias value

### 2. HLSL Shader Side

**Shadow Projection Shader** (`shadow_projection_ps.hlsl` or equivalent):

- [ ] Define `float4 ProjectionDepthBiasParameters` parameter
- [ ] Add code block for deferred path that computes NoL
- [ ] Modulate `TransitionScale` with `lerp(bias_min, 1.0, NoL)`
- [ ] Pass modulated `TransitionScale` to PCF sampler

**PCF Common Header** (`shadow_filtering.hlsli` or equivalent):

- [ ] Ensure `CalculateOcclusion()` uses `Settings.TransitionScale`
- [ ] Verify gradient calculation uses modulated TransitionScale

**Deferred Lighting Shader** (`deferred_lighting_ps.hlsl`):

- [ ] Verify shadow attenuation is decoded with `Square()` before use
- [ ] Ensure decoded shadow is in [0, 1] range

### 3. Testing

- [ ] Add console variable to override receiver bias for testing
- [ ] Capture RenderDoc frames for both DX11 and DX12
- [ ] Compare attenuation buffer values pixel-by-pixel
- [ ] Verify receiver bias effect on shadow edges:
  - Surface perpendicular to light: soft shadow edge
  - Surface facing light: hard shadow edge

---

## Common Pitfalls

### Pitfall 1: Receiver Bias Not Wired to TransitionScale

**Wrong**:
```hlsl
Settings.TransitionScale = SoftTransitionScale.z;  // No modulation!
float ReceiverBias = ProjectionDepthBiasParameters.z;  // Computed but unused
```

**Correct**:
```hlsl
Settings.TransitionScale = SoftTransitionScale.z *
    lerp(ProjectionDepthBiasParameters.z, 1.0, NoL);
```

### Pitfall 2: Receiver Bias Applied to Forward Shading

UE5 **only applies receiver bias in deferred mode** because it needs access to the GBuffer normal. Forward shading doesn't have this.

**Wrong**:
```hlsl
Settings.TransitionScale = SoftTransitionScale.z *
    lerp(ProjectionDepthBiasParameters.z, 1.0, NoL);  // Always
```

**Correct**:
```hlsl
#if SHADING_PATH_DEFERRED && !FORWARD_SHADING
    Settings.TransitionScale = SoftTransitionScale.z *
        lerp(ProjectionDepthBiasParameters.z, 1.0, NoL);
#else
    Settings.TransitionScale = SoftTransitionScale.z;
#endif
```

### Pitfall 3: Decoding Attenuation Incorrectly

**Wrong** (no decode):
```hlsl
float Shadow = Attenuation.x;  // Using sqrt-encoded value directly
```

**Correct**:
```hlsl
float Shadow = Square(Attenuation.x);  // Decode sqrt(x) → x
```

### Pitfall 4: NoL Computed with Wrong Direction

**Wrong**:
```hlsl
float NoL = dot(WorldNormal, LightPositionOrDirection.xyz);  // Wrong direction
```

**Correct** (directional):
```hlsl
float3 LightDirection = -LightPositionOrDirection.xyz;  // Negate direction
float NoL = saturate(dot(WorldNormal, LightDirection));
```

**Correct** (local light):
```hlsl
float3 LightDirection = normalize(LightPosition - TranslateWorldPosition);
float NoL = saturate(dot(WorldNormal, LightDirection));
```

---

## Validation

### Expected Visual Results

**Before Receiver Bias**:
- All shadows have uniform edge softness
- Grazing angle surfaces have same shadow darkness as front-facing surfaces
- No obvious visual difference between light types

**After Receiver Bias** (Correct Implementation):
- Grazing angle surfaces: soft, lighter shadow edges
- Front-facing surfaces: hard, darker shadow edges
- Light-type variation: CSM softer, Spot/Rect intermediate, Point varies with slope bias

### RenderDoc Validation

1. Capture frame with receiver bias enabled
2. Examine light attenuation buffer
3. Compare a grazing-angle surface pixel vs front-facing pixel
4. Grazing pixel should have HIGHER attenuation value (lighter shadow)
5. Front-facing pixel should have LOWER attenuation value (darker shadow)

---

## References

| Item | File | Line | Purpose |
|------|------|------|---------|
| Receiver Bias C++ | ShadowRendering.cpp | 1967-1984 | Maps CVar to shader |
| CSM ReceiverBias CVar | ShadowRendering.cpp | 74-78 | Default 0.9 |
| Spot ReceiverBias CVar | ShadowRendering.cpp | 148-152 | Default 0.5 |
| Rect ReceiverBias CVar | ShadowRendering.cpp | 110-114 | Default 0.3 |
| NoL Modulation | ShadowProjectionPixelShader.usf | 248 | **KEY LINE** |
| PCF Gradient | ShadowFilteringCommon.ush | 151-180 | Soft edge calc |
| Attenuation Encoding | Common.ush | 1202-1213 | sqrt encode |
| Attenuation Decoding | Common.ush | 1216-1225 | Square decode |

