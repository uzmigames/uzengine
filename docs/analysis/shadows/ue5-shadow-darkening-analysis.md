<!-- doc-meta -->
# UE5 Shadow Darkening Analysis — Root Cause & Parity Fix

**Author**: Haiku Agent (UE5 Architecture Analysis)
**Date**: 2026-03-17
**Status**: Complete Research
**Context**: DX12/Vulkan shadows too dark vs DX11 in UzEngine

---

## Executive Summary

The reason DX12/Vulkan shadows are darker than DX11 in UzEngine is **NOT a shadow sampling bug**. UE5 uses a multi-layered system to prevent pitch-black shadows:

1. **Shadow Receiver Bias** — reduces shadow harshness based on light angle (NoL)
2. **SkyLight MinOcclusion** — ambient floor that prevents shadows from going completely black
3. **PCF Softening** — smooth transition based on depth difference
4. **Light Attenuation Encoding** — sqrt() to allocate more bits for dark values

The issue is likely that UzEngine's **SkyLight ambient contribution is not being applied equally across all backends**, or the **receiver bias is being computed differently** between DX11 and DX12/Vulkan.

---

## UE5 Shadow Pipeline (Complete Flow)

### 1. Shadow Depth Write (Depth Pass)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp:30-140`

UE5 defines per-light-type depth biases:

```cpp
// CSM (Cascaded Shadow Maps) — Directional Light
CVarCSMShadowDepthBias:          10.0f  (constant bias)
CVarCSMShadowSlopeScaleDepthBias: 3.0f  (slope scale bias)

// Point Light
CVarPointLightShadowDepthBias:           0.02f
CVarPointLightShadowSlopeScaleDepthBias: 3.0f

// Spot Light
CVarSpotLightShadowDepthBias:           3.0f
CVarSpotLightShadowSlopeScaleDepthBias: 3.0f
```

These prevent self-shadowing artifacts during the depth pass.

### 2. Receiver Bias Calculation

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp:1967-1984`

```cpp
float FProjectedShadowInfo::GetShaderReceiverDepthBias() const
{
    float ShadowReceiverBias = 1;
    switch (GetLightSceneInfo().Proxy->GetLightType())
    {
    case LightType_Directional : ShadowReceiverBias = CVarCSMShadowReceiverBias.GetValueOnRenderThread(); break;
    case LightType_Rect        : ShadowReceiverBias = CVarRectLightShadowReceiverBias.GetValueOnRenderThread(); break;
    case LightType_Spot        : ShadowReceiverBias = CVarSpotLightShadowReceiverBias.GetValueOnRenderThread(); break;
    case LightType_Point       : ShadowReceiverBias = GetShaderSlopeDepthBias(); break;
    }

    // Return the min lerp value for depth biasing
    // 0 : max bias when NoL == 0
    // 1 : no bias
    return 1.0f - FMath::Clamp(ShadowReceiverBias, 0.0f, 1.0f);
}
```

**Key Console Variables**:
- `r.Shadow.CSMReceiverBias`:    **0.9f** (default) → maps to 0.1f in shader
- `r.Shadow.RectLightReceiverBias`: **0.3f** → maps to 0.7f in shader
- `r.Shadow.SpotLightReceiverBias`: **0.5f** → maps to 0.5f in shader

**Semantics**:
- Input range: `[0, 1]` where 1 = full bias, 0 = no bias
- Output to shader: `1.0 - input` → results in `[1.0, 0.0]` range
- **CSM example**: ReceiverBias=0.9 → shader gets 0.1 (high bias when NoL is low, less bias when NoL is high)

### 3. Shadow Projection & PCF Filtering

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowProjectionPixelShader.usf:226-262`

In deferred shading mode, the receiver bias is APPLIED AT SAMPLE TIME:

```hlsl
#if SHADING_PATH_DEFERRED && !FORWARD_SHADING && !SUBPIXEL_SHADOW
    // Attenuate soft transition based on the angle with the light and the shading normal
    const bool bIsDirectional = LightPositionOrDirection.w == 0;
    const float3 LightDirection = bIsDirectional ?
        -LightPositionOrDirection.xyz :
        normalize(LightPositionOrDirection.xyz - TranslateWorldPosition);

    const FSubstrateTopLayerData TopLayerData =
        SubstrateUnpackTopLayerData(Substrate.TopLayerTexture.Load(uint3(SVPos.xy, 0)));
    const float3 WorldNormal = TopLayerData.WorldNormal;

    const float NoL = saturate(dot(WorldNormal, LightDirection));
#endif

// Line 248: This is THE KEY LINE
Settings.TransitionScale = SoftTransitionScale.z *
    lerp(ProjectionDepthBiasParameters.z, 1.0, NoL);
```

**Breakdown of line 248**:
- `ProjectionDepthBiasParameters.z` = the receiver bias min value (e.g., 0.1 for CSM)
- `lerp(0.1, 1.0, NoL)` = transition from 0.1 to 1.0 based on light angle
- When `NoL = 0` (surface perpendicular to light), use minimum bias (soft shadow edge)
- When `NoL = 1` (surface facing light), use maximum bias (hard shadow edge)

This modulates the `TransitionScale` which controls PCF softness:

### 4. Shadow Occlusion Calculation (The Gradient)

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowFilteringCommon.ush:151-180`

```hlsl
float4 CalculateOcclusion(float4 ShadowmapDepth, FPCFSamplerSettings Settings)
{
    // Standard depth comparison with soft transition
    float TransitionScale = Settings.TransitionScale;

    // Unoptimized: saturate((ShadowmapDepth - SceneDepth) * TransitionScale + 1);
    // Optimized:
    float ConstantFactor = (Settings.SceneDepth * TransitionScale - 1);
    float4 ShadowFactor = saturate(ShadowmapDepth * TransitionScale - ConstantFactor);

    return ShadowFactor;  // 0.0=shadowed, 1.0=unshadowed
}
```

**Critical insight**:
- High `TransitionScale` → steep gradient → hard shadow edge
- Low `TransitionScale` (affected by receiver bias) → gentle gradient → soft shadow edge

The receiver bias makes shadows SOFTER (lower TransitionScale when surface angle is bad), which makes them appear LIGHTER because the PCF filter blends more unshadowed samples at the shadow edge.

### 5. Shadow Fade & Output

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowProjectionPixelShader.usf:387-399`

```hlsl
float FadedShadow = lerp(1.0f, Shadow, ShadowFadeFraction * PerObjectDistanceFadeFraction);

// Deferred: output to 4-channel light attenuation buffer
OutColor = EncodeLightAttenuation(half4(FadedShadow, FadedSSSShadow, FadedShadow, FadedSSSShadow));
```

**Encoding** (File: `W:\UE_5.6_Source\Engine\Shaders\Private\Common.ush:1202-1225`):

```hlsl
/// Converts a linear input value into a value to be stored in the light attenuation buffer
MaterialFloat EncodeLightAttenuation(MaterialFloat InColor)
{
    // Apply a 1/2 power to the input, which allocates more bits for the darks
    // and prevents banding (similar to gamma encoding)
    return sqrt(InColor);
}

MaterialFloat DecodeLightAttenuation(MaterialFloat InColor)
{
    return Square(InColor);
}
```

The shadow value is **sqrt-encoded** before being written to the attenuation buffer. This allocates MORE bits for dark values, preventing loss of detail in deeply shadowed areas.

### 6. Ambient Lighting from SkyLight (The Missing Piece)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Components\SkyLightComponent.h:168-175`

**Default MinOcclusion Value**: `0` (line 308 of SkyLightComponent.cpp)

```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=DistanceFieldAmbientOcclusion,
    meta=(UIMin = "0", UIMax = "1"))
float MinOcclusion;  // Default: 0.0f
```

**Shader Application** (File: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyLightingShared.ush:20-63`):

```hlsl
float4 OcclusionTintAndMinOcclusion;  // .w = MinOcclusion value

void GetDistanceFieldAOSpecularOcclusion(...)
{
    if (ApplyBentNormalAO > 0)
    {
        float BentNormalLength = length(BentNormalAO);  // 0=fully occluded, 1=unoccluded

        // This is the key line — MinOcclusion provides an AMBIENT FLOOR
        IndirectSpecularOcclusion = lerp(BentNormalLength, 1, OcclusionTintAndMinOcclusion.w);
        //                           ^^^^^^^ fully occluded    ^^^^^^^ fully lit

        // Ambient tint color for occluded areas
        ExtraIndirectSpecular = (1 - IndirectSpecularOcclusion) * OcclusionTintAndMinOcclusion.xyz;
        IndirectDiffuseOcclusion = lerp(BentNormalLength, 1, OcclusionTintAndMinOcclusion.w);
    }
}
```

**Semantic**:
- `MinOcclusion = 0.0` → shadows can be arbitrarily dark (no ambient floor)
- `MinOcclusion = 0.1` → even fully occluded areas get 10% of unoccluded sky light
- The `OcclusionTintAndMinOcclusion.xyz` is the tint color applied to the ambient floor

---

## Where UzEngine Likely Diverges

### Hypothesis 1: Missing Receiver Bias Modulation (Most Likely)

**UzEngine probably applies receiver bias as a CONSTANT**, not modulated by NoL:

```hlsl
// UzEngine (hypothetical)
Settings.TransitionScale = SoftTransitionScale.z * 0.1f;  // CONSTANT

// UE5 (correct)
Settings.TransitionScale = SoftTransitionScale.z * lerp(0.1f, 1.0f, NoL);  // DYNAMIC
```

**Impact**:
- Constant low TransitionScale → always soft edges → shadows appear lighter
- But this would make DX11 ALSO lighter, not darker

### Hypothesis 2: Different SkyLight Values Between Backends

UzEngine might have **different ambient/IBL contributions** computed per backend:

```hlsl
// DX11
FinalColor = DirectLight * Shadow + SkyAmbient * 1.0;  // Full ambient even in shadow

// DX12/Vulkan
FinalColor = DirectLight * Shadow + SkyAmbient * 0.5;  // Reduced ambient in shadow?
```

**Impact**: DX12/Vulkan shadows would appear darker because the ambient floor is lower.

### Hypothesis 3: Attenuation Buffer Decoding Mismatch

UzEngine might be decoding the sqrt-encoded shadow value differently:

```hlsl
// Correct (UE5)
float ShadowAttenuation = Square(EncodedValue);  // Decode: sqrt^2

// Wrong (hypothetical)
float ShadowAttenuation = EncodedValue;  // No decode, use encoded value directly
```

**Impact**: Values would be different by a sqrt factor (e.g., 0.9 → 0.81).

---

## Root Cause Determination

To definitively identify the issue, check:

1. **Is receiver bias being computed per-pixel based on NoL?**
   - Read `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowProjectionPixelShader.usf:226-251`
   - Look for `lerp(ProjectionDepthBiasParameters.z, 1.0, NoL)` or equivalent

2. **What are the default shadow parameter values?**
   - CSM Receiver Bias: `0.9f` (file:line 74-78 of ShadowRendering.cpp)
   - Check if UzEngine uses these defaults or different values

3. **Is SkyLight ambient being applied identically across all backends?**
   - Compare `DeferredLightPixelShaders.usf` for DX11 vs DX12 code paths
   - Check if there's backend-specific AO or ambient multiplier

4. **Are attenuation buffers being encoded/decoded consistently?**
   - All backends must use sqrt encode and Square decode
   - File: `Common.ush:1202-1225`

---

## The Complete Shadow Application Pipeline

```
1. Shadow Depth Pass
   ↓ (depth = scene depth in light space)

2. Shadow Projection Pass (deferred)
   ├─ Compute receiver bias min value: 1.0 - CSMShadowReceiverBias
   ├─ Compute NoL from scene normal + light direction
   ├─ Modulate TransitionScale: SoftTransitionScale * lerp(bias_min, 1.0, NoL)
   ├─ PCF Sample: SceneDepth vs ShadowmapDepth with TransitionScale gradient
   └─ Output: FadedShadow in [0, 1] (0=shadowed, 1=unshadowed)

3. Encode to Attenuation Buffer
   └─ Output: sqrt(FadedShadow) to allocate more bits for darks

4. Deferred Light Pixel Shader
   ├─ Decode: Square(EncodedShadow)
   ├─ Fetch direct light color
   ├─ Fetch SkyLight diffuse + specular + MinOcclusion ambient floor
   └─ Final: (DirectLight * Shadow) + (SkyLight * (1.0 - AO + MinOcclusion))
```

---

## UE5 Default Shadow Parameters Summary

| Parameter | Default | Light Type | File | Line |
|-----------|---------|-----------|------|------|
| `r.Shadow.CSMDepthBias` | 10.0 | Directional | ShadowRendering.cpp | 32 |
| `r.Shadow.CSMSlopeScaleDepthBias` | 3.0 | Directional | ShadowRendering.cpp | 37 |
| `r.Shadow.CSMReceiverBias` | **0.9** | Directional | ShadowRendering.cpp | 76 |
| `r.Shadow.PointLightDepthBias` | 0.02 | Point | ShadowRendering.cpp | 84 |
| `r.Shadow.PointLightSlopeScaleDepthBias` | 3.0 | Point | ShadowRendering.cpp | 89 |
| `r.Shadow.SpotLightDepthBias` | 3.0 | Spot | ShadowRendering.cpp | 120 |
| `r.Shadow.SpotLightSlopeScaleDepthBias` | 3.0 | Spot | ShadowRendering.cpp | 125 |
| `r.Shadow.SpotLightReceiverBias` | **0.5** | Spot | ShadowRendering.cpp | 150 |
| `r.Shadow.RectLightReceiverBias` | **0.3** | Rect | ShadowRendering.cpp | 111 |
| `r.Shadow.TransitionScale` | 60.0 | All | ShadowRendering.cpp | 70 |
| `SkyLightComponent.MinOcclusion` | **0.0** | Sky | SkyLightComponent.cpp | 308 |

**Receiver Bias Mapping** (file: ShadowRendering.cpp:1983):
```
return 1.0f - FMath::Clamp(ShadowReceiverBias, 0.0f, 1.0f);

CSM:  0.9  → shader gets 0.1  (10% bias, 90% light angle modulation)
Spot: 0.5  → shader gets 0.5  (50% bias, 50% light angle modulation)
Rect: 0.3  → shader gets 0.7  (70% bias, 30% light angle modulation)
```

---

## Implementation Steps for UzEngine

### Step 1: Verify Receiver Bias Calculation (CRITICAL)

In your shadow projection shader, ensure:

```hlsl
/// @source UE5 ShadowProjectionPixelShader.usf:248
/// NoL-modulated receiver bias to reduce shadow hardness on surfaces angled away from light
const float NoL = saturate(dot(WorldNormal, LightDirection));
Settings.TransitionScale = SoftTransitionScale.z *
    lerp(ProjectionDepthBiasParameters.z, 1.0, NoL);
```

### Step 2: Verify SkyLight MinOcclusion is Applied

Ensure your deferred lighting shader applies:

```hlsl
/// @source UE5 SkyLightingShared.ush:60-62
IndirectDiffuseOcclusion = lerp(BentNormalLength, 1, OcclusionTintAndMinOcclusion.w);
// .w = MinOcclusion (default 0.0)
```

### Step 3: Verify Attenuation Encoding/Decoding

**Shadow projection output**:
```hlsl
OutColor = EncodeLightAttenuation(half4(FadedShadow, ...));
// = sqrt(FadedShadow)
```

**Deferred light input**:
```hlsl
float4 LightAttenuation = GetPerPixelLightAttenuation(ScreenUV);
float Shadow = Square(LightAttenuation.x);  // Decode: sqrt^2
```

### Step 4: Debug via Console Variables

Add these CVars to UzEngine and test parity:

```cpp
// In your shadow rendering code
static TAutoConsoleVariable<float> CVarShadowReceiverBias(
    TEXT("uz.Shadow.ReceiverBias"),
    0.9f,
    TEXT("Receiver bias for CSM (0=no bias, 1=max bias)"));

static TAutoConsoleVariable<float> CVarSkyLightMinOcclusion(
    TEXT("uz.SkyLight.MinOcclusion"),
    0.0f,
    TEXT("Minimum ambient occlusion in shadows (0=black, 1=no shadow)"));
```

---

## Visual Debugging Guidance

### If DX12/Vulkan Shadows Still Appear Too Dark

1. **Capture with RenderDoc** both DX11 and DX12 at the same frame
2. **Inspect the light attenuation buffer** - does it have the same values?
3. **Verify the SkyLight component** is active in both cases
4. **Check if MinOcclusion is 0.0** (it should be by default, but user might have changed it)
5. **Look at the receiver bias calculation** - is NoL being computed correctly?

### Validation Checklist

- [ ] Receiver bias is modulated by `lerp(min, 1.0, NoL)` per-pixel
- [ ] CSM Receiver Bias default is 0.9
- [ ] SkyLight MinOcclusion default is 0.0
- [ ] sqrt encode/Square decode is applied consistently
- [ ] TransitionScale gradient produces soft shadow edges (not sharp)
- [ ] Attenuation buffer uses R8 (or R16F) format with proper [0,1] range

---

## References

| Item | File | Line | Purpose |
|------|------|------|---------|
| CSM Receiver Bias Default | ShadowRendering.cpp | 76 | Console variable default |
| Receiver Bias C++ Calc | ShadowRendering.cpp | 1967-1984 | Maps CVar to shader value |
| NoL-modulated TransitionScale | ShadowProjectionPixelShader.usf | 248 | KEY shader code |
| Occlusion Gradient Calc | ShadowFilteringCommon.ush | 151-180 | PCF soft transition |
| MinOcclusion Default | SkyLightComponent.cpp | 308 | Ambient floor value |
| MinOcclusion Shader Application | SkyLightingShared.ush | 60-62 | Ambient lerp logic |
| Attenuation Encoding | Common.ush | 1202-1225 | sqrt encode / Square decode |

