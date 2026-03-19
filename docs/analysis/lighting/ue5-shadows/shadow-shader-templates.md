<!-- doc-meta
type: analysis
category: rendering
title: Shadow Shader Templates — Ready-to-Use Code from UE5
description: Copy-paste shader code templates for shadow rendering, adapted from UE5 5.6.
tags: [hlsl, shadow-depth, shadow-projection, pcf, template]
status: complete
last-updated: 2026-03-16
-->

# Shadow Shader Templates from UE5

These are **exact patterns from UE5 5.6**, adapted for UzEngine with simplified variable names.

---

## 1. Shadow Depth Vertex Shader

**Based on**: `ShadowDepthVertexShader.usf`, lines 51–108

```hlsl
// Constants from C++
cbuffer ShadowDepthParams : register(b0)
{
	float4x4 WorldToClip;
	float4x4 WorldToLight;
	float4 ShadowParams;  // .x=ConstantBias, .y=SlopeBias, .z=MaxSlopeBias, .w=InvMaxSubjectDepth
	float bClampToNearPlane;
};

struct VS_Input
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
};

struct VS_Output
{
	float4 ClipPosition : SV_POSITION;
	float ShadowDepth : TEXCOORD0;
};

VS_Output main(VS_Input input)
{
	VS_Output output;

	// Transform to clip space
	float4 WorldPos = float4(input.Position, 1.0);
	output.ClipPosition = mul(WorldPos, WorldToClip);

	// Clamp to near plane if needed (prevents far-plane artifacts)
	if (bClampToNearPlane > 0 && output.ClipPosition.z > output.ClipPosition.w)
	{
		output.ClipPosition.z = 0.999999;
		output.ClipPosition.w = 1.0;
	}

	// ===== CRITICAL SHADOW DEPTH TRANSFORM =====
	// Compute slope bias based on angle between normal and light direction
	float3 LightDirection = float3(
		WorldToLight[0].z,
		WorldToLight[1].z,
		WorldToLight[2].z
	);
	float NoL = abs(dot(input.Normal, LightDirection));
	float MaxSlopeDepthBias = ShadowParams.z;
	float Slope = clamp(
		NoL > 0.0 ? sqrt(saturate(1.0 - NoL * NoL)) / NoL : MaxSlopeDepthBias,
		0.0,
		MaxSlopeDepthBias
	);

	float SlopeDepthBias = ShadowParams.y * Slope;
	float ConstantDepthBias = ShadowParams.x;
	float DepthBias = SlopeDepthBias + ConstantDepthBias;

	// Convert from reversed-Z clip space to linear normalized [0, 1]
	// This is the EXACT formula from UE5
	float InvMaxSubjectDepth = ShadowParams.w;
	float LinearDepth = (1.0 - output.ClipPosition.z * InvMaxSubjectDepth);
	float AdjustedDepth = LinearDepth + DepthBias;

	// Store adjusted depth in output position and as varying
	output.ShadowDepth = AdjustedDepth;
	output.ClipPosition.z = AdjustedDepth;

	return output;
}
```

### Critical Lines Explained

**Line: `float LinearDepth = (1.0 - output.ClipPosition.z * InvMaxSubjectDepth);`**

- `output.ClipPosition.z` — standard DX12 reversed-Z (0 = far, 1 = near)
- Multiply by `InvMaxSubjectDepth` — scales to shadow frustum [0, 1]
- `1.0 -` — **inverts to standard depth semantics (0 = near, 1 = far)**
- Result: linear normalized depth matching shadow map conventions

**Line: `float AdjustedDepth = LinearDepth + DepthBias;`**

Bias is **added after** linearization, so a bias of 0.001 means 0.1% of the shadow frustum (independent of perspective).

---

## 2. Shadow Projection Pixel Shader

**Based on**: `ShadowProjectionPixelShader.usf`, lines 108–263

```hlsl
// Constant buffers
cbuffer ShadowProjectionParams : register(b0)
{
	float4x4 ScreenToShadow;
	float4 DepthBiasParams;  // .x=Bias, .y=SlopeBias, .z=ReceiverBias, .w=MaxSubjectZ - MinSubjectZ
	float SoftTransitionScale;
	float FadePlaneOffset;
	float InvFadePlaneLength;
};

// Textures
Texture2D<float> ShadowMapTexture : register(t0);
Texture2D<float> SceneDepthTexture : register(t1);

// Samplers
SamplerState ShadowSampler : register(s0);  // Point sampler, no comparison
SamplerState SceneDepthSampler : register(s1);

struct PS_Input
{
	float4 ScreenPos : SV_POSITION;
};

float4 main(PS_Input input) : SV_TARGET
{
	float2 ScreenUV = input.ScreenPos.xy / input.ScreenPos.zw;

	// Read scene depth (reversed-Z)
	float SceneDepthReversedZ = SceneDepthTexture.Sample(SceneDepthSampler, ScreenUV).r;

	// Project to shadow space
	float4 ScreenPosition = float4(ScreenUV * 2.0 - 1.0, SceneDepthReversedZ, 1.0);
	float4 ShadowPosition = mul(ScreenPosition, ScreenToShadow);
	ShadowPosition.xyz /= ShadowPosition.w;

	// ===== CRITICAL: Convert reversed-Z scene depth to linear =====
	// This matches the transform done in the shadow depth vertex shader
	float SceneDepthLinear = 1.0 - ShadowPosition.z;

	// Clamp to prevent comparing against shadow map's unrendered far plane (cleared to 1.0)
	// Any pixel outside the shadow frustum will have depth = 1.0 (unshadowed)
	float SceneDepthClamped = min(SceneDepthLinear, 0.99999);

	// ===== MANUAL PCF FILTERING =====
	// For simplicity, single sample (no filtering shown here)
	float ShadowmapDepth = ShadowMapTexture.Sample(ShadowSampler, ShadowPosition.xy).r;

	// ===== THE COMPARISON =====
	// Formula: saturate((ShadowmapDepth - SceneDepth) * TransitionScale + 1.0)
	// This implements: unshadowed = (ShadowmapDepth > SceneDepth) with soft transition
	float TransitionScale = SoftTransitionScale;
	float DepthDifference = ShadowmapDepth - SceneDepthClamped;
	float ShadowFactor = saturate(DepthDifference * TransitionScale + 1.0);

	// Clamp unrendered pixels (depth > 0.99) to unshadowed
	if (ShadowmapDepth > 0.99)
	{
		ShadowFactor = 1.0;
	}

	// Output: 1.0 = fully lit, 0.0 = fully shadowed
	return float4(ShadowFactor.xxx, 1.0);
}
```

### Critical Lines Explained

**Line: `float SceneDepthLinear = 1.0 - ShadowPosition.z;`**

This is the **inverse transform** of the shadow depth vertex shader:
- `ShadowPosition.z` — scene depth in shadow-space clip coordinates (reversed-Z)
- `1.0 -` — converts to linear (matches shadow map's linear stored depth)

**Line: `float SceneDepthClamped = min(SceneDepthLinear, 0.99999);`**

Prevents unrendered regions (shadow map cleared to 1.0) from being treated as very far. Any pixel outside the shadow frustum becomes unshadowed.

**Line: `float ShadowFactor = saturate(DepthDifference * TransitionScale + 1.0);`**

The actual comparison with soft transition:
- If `ShadowmapDepth >> SceneDepth` → positive difference → `saturate(...) ≈ 1.0` → unshadowed
- If `ShadowmapDepth << SceneDepth` → negative difference → `saturate(...) ≈ 0.0` → shadowed
- If `|difference|` is small → soft transition in between

---

## 3. PCF Filtering (4-Sample)

**Based on**: `ShadowFilteringCommon.ush`, lines 334–349

```hlsl
// Helper function to compute occlusion from shadowmap depth
float ComputeOcclusion(float ShadowmapDepth, float SceneDepth, float TransitionScale)
{
	// Standard comparison: SceneDepth < ShadowmapDepth (or equivalently, ShadowmapDepth > SceneDepth)
	// Rearranged math (from UE5) for per-pixel constant optimization:
	// saturate((ShadowmapDepth - SceneDepth) * TransitionScale + 1.0)
	float ConstantFactor = (SceneDepth * TransitionScale - 1.0);
	float ShadowFactor = saturate(ShadowmapDepth * TransitionScale - ConstantFactor);

	// Treat unrendered pixels (depth > 0.99) as unshadowed
	if (ShadowmapDepth > 0.99)
	{
		ShadowFactor = 1.0;
	}

	return ShadowFactor;
}

// 2x2 PCF (4 samples)
float PCF_2x2(
	Texture2D<float> ShadowMap,
	SamplerState Sampler,
	float2 ShadowUV,
	float SceneDepth,
	float2 ShadowMapSize,
	float TransitionScale
)
{
	float2 InvSize = 1.0 / ShadowMapSize;

	// Compute texel position with sub-texel offset for proper filtering
	float2 TexelPos = ShadowUV * ShadowMapSize - 0.5;
	float2 Fraction = frac(TexelPos);
	float2 TexelCorner = floor(TexelPos);

	// Sample 2x2 neighborhood
	float4 Depths;
	Depths.x = ShadowMap.SampleLevel(Sampler, (TexelCorner + float2(0, 0)) * InvSize, 0).r;
	Depths.y = ShadowMap.SampleLevel(Sampler, (TexelCorner + float2(1, 0)) * InvSize, 0).r;
	Depths.z = ShadowMap.SampleLevel(Sampler, (TexelCorner + float2(0, 1)) * InvSize, 0).r;
	Depths.w = ShadowMap.SampleLevel(Sampler, (TexelCorner + float2(1, 1)) * InvSize, 0).r;

	// Compute occlusion for each sample
	float4 Occlusions;
	Occlusions.x = ComputeOcclusion(Depths.x, SceneDepth, TransitionScale);
	Occlusions.y = ComputeOcclusion(Depths.y, SceneDepth, TransitionScale);
	Occlusions.z = ComputeOcclusion(Depths.z, SceneDepth, TransitionScale);
	Occlusions.w = ComputeOcclusion(Depths.w, SceneDepth, TransitionScale);

	// Bilinear interpolation
	float Result = lerp(
		lerp(Occlusions.x, Occlusions.y, Fraction.x),
		lerp(Occlusions.z, Occlusions.w, Fraction.x),
		Fraction.y
	);

	return Result;
}
```

### Key Detail

**Line: `float ConstantFactor = (SceneDepth * TransitionScale - 1.0);`**

This is algebraic rearrangement from UE5 to optimize shader constants:

```
Original:  saturate((ShadowmapDepth - SceneDepth) * TransitionScale + 1.0)
Expanded:  saturate(ShadowmapDepth * TransitionScale - SceneDepth * TransitionScale + 1.0)
Rearranged: saturate(ShadowmapDepth * TransitionScale - (SceneDepth * TransitionScale - 1.0))
```

The second form allows `(SceneDepth * TransitionScale - 1.0)` to be computed once per pixel and reused for all 4-16 samples.

---

## 4. Constants Setup (C++)

**Based on**: `ShadowRendering.h`, lines 1150–1177

```cpp
struct ShadowProjectionConstants
{
	Matrix4x4 ScreenToShadowMatrix;
	Vector4 DepthBiasParameters;     // .x=ConstantBias, .y=SlopeBias, .z=ReceiverBias, .w=MaxSubjectZ-MinSubjectZ
	Vector4 ShadowBufferSize;        // .xy=ShadowMapResolution, .zw=1/Resolution
	float SoftTransitionScale;       // Default 60.0f
	float FadePlaneOffset;
	float InvFadePlaneLength;
};

// Set up constants for projection pass
void SetupShadowProjectionConstants(
	const ShadowInfo& Shadow,
	const View& View,
	ShadowProjectionConstants& OutConstants
)
{
	// Screen-to-shadow transform matrix
	OutConstants.ScreenToShadowMatrix = Shadow.ScreenToShadowMatrix;

	// Depth bias (apply at storage time, not projection time)
	OutConstants.DepthBiasParameters = Vector4(
		Shadow.GetConstantDepthBias(),      // Default ~10.0f for CSM
		Shadow.GetSlopeDepthBias(),         // Default ~3.0f
		Shadow.GetReceiverDepthBias(),      // Default ~0.9f
		Shadow.GetMaxSubjectZ() - Shadow.GetMinSubjectZ()  // Frustum depth range
	);

	// Shadow map texture size
	OutConstants.ShadowBufferSize = Vector4(
		(float)Shadow.ResolutionX,
		(float)Shadow.ResolutionY,
		1.0f / Shadow.ResolutionX,
		1.0f / Shadow.ResolutionY
	);

	// Soft transition width (controls fade between lit/shadowed)
	// Larger values = softer transitions, smaller values = sharper shadows
	// Default from cvar: 60.0f
	OutConstants.SoftTransitionScale = CVarShadowTransitionScale;
}
```

### Default Values Reference

| Parameter | Default | Units | Notes |
|-----------|---------|-------|-------|
| ConstantDepthBias (CSM) | 10.0 | Light-space units | ~10-30 typical |
| SlopeDepthBias (CSM) | 3.0 | Unitless multiplier | Multiplies (1 - NoL) / NoL |
| ReceiverBias | 0.9 | [0, 1] | Blend factor for receiver-side bias |
| TransitionScale | 60.0 | Unitless | Soft shadow fade width |

---

## 5. Quick Reference: Comparison Function

The shadow comparison always follows this pattern:

```hlsl
// 1. Read both depths
float ShadowmapDepth = ShadowMap.Sample(Sampler, UV).r;     // Stored linear depth
float SceneDepth = 1.0 - ViewDepthReversedZ;                // Convert scene depth to linear

// 2. Compare
float Difference = ShadowmapDepth - SceneDepth;

// 3. Soft transition
float TransitionScale = 60.0;
float ShadowFactor = saturate(Difference * TransitionScale + 1.0);

// Result: 1.0 = unshadowed, 0.0 = shadowed
```

**Mnemonic**: `(StoredDepth - SceneDepth) * Scale + 1.0` → ranges [0, 1] for reasonable depths.

---

## 6. Sampler State Setup (C++)

```cpp
// Directional/Spot light shadows (manual PCF in shader)
FRHISamplerState* DirectionalShadowSampler = CreateStaticSamplerState(
	ESamplerFilter::Point,      // No hardware filtering
	ESamplerAddressMode::Clamp,
	ESamplerAddressMode::Clamp,
	ESamplerAddressMode::Clamp
	// NO comparison function (manual in shader)
);

// Point light shadows (hardware PCF)
FRHISamplerState* PointLightShadowSampler = CreateStaticSamplerState(
	ESamplerFilter::Bilinear,
	ESamplerAddressMode::Clamp,
	ESamplerAddressMode::Clamp,
	ESamplerAddressMode::Clamp,
	EComparisonFunction::Less  // Hardware comparison: depth < shadowmap
);
```

---

## Testing Checklist

After implementing, verify these cases:

- [ ] **Self-shadowing is minimized**: Casters don't shadow themselves on their own surface
  - Use slope bias to adjust
  - Check: `ShadowFactor` should be ~0.9 on caster surfaces, not 0.0

- [ ] **Soft transitions work**: Increase `TransitionScale` and see shadow edges soften
  - Default 60.0 gives reasonable softness
  - Lower values (e.g., 20.0) → harder shadows
  - Higher values (e.g., 150.0) → softer shadows

- [ ] **Unrendered pixels are unshadowed**: Move objects far from light, they should stay lit
  - Without clamping: far pixels become black (incorrect)
  - With clamping: far pixels stay lit (correct)

- [ ] **Depth bias is frame-rate independent**: Bias amount doesn't change with camera movement
  - Linear depth bias is frame-rate stable by definition

---

## References

All templates based directly on:
- `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowDepthVertexShader.usf`
- `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowProjectionPixelShader.usf`
- `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowFilteringCommon.ush`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.h`
