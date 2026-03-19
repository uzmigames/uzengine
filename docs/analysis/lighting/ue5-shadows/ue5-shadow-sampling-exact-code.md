<!-- doc-meta
type: analysis
category: rendering
title: UE5 5.6 Shadow Sampling — Exact Code Analysis
description: Complete reverse-engineering of UE5 shadow depth comparison, depth transformation, and sampler state configuration.
tags: [shadow-mapping, pcf, depth-comparison, depth-transform, rhi, dx12]
status: complete
last-updated: 2026-03-16
-->

# UE5 5.6 Shadow Sampling — Exact Code

This document contains **exact code snippets from UE5 5.6 source**, with file paths and line numbers. No synthesis or interpretation—only what UE5 actually does.

## 1. Shadow Depth Output (Vertex Shader)

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowDepthVertexShader.usf`

### Where: Depth Computation in SetShadowDepthOutputs()

Lines 51–108. The key function that computes shadow depth for output:

```hlsl
void SetShadowDepthOutputs(
	float4x4 WorldToClipMatrix,
	float4x4 WorldToShadowMatrix,
	float4 WorldPosition,
	float3 WorldVertexNormal,
	out float4 OutPosition,
	out float ShadowDepth
#if PERSPECTIVE_CORRECT_DEPTH
	, out float OutDepthBias
#endif
)
{
	OutPosition = mul(WorldPosition, WorldToClipMatrix);

	// Clamp the vertex to the near plane if it is in front of the near plane
	if (PassStruct.bClampToNearPlane > 0 && OutPosition.z > OutPosition.w)
	{
		OutPosition.z = 0.999999f;
		OutPosition.w = 1.0f;
	}

	// ... slope bias calculation ...

	#if PERSPECTIVE_CORRECT_DEPTH
		ShadowDepth = OutPosition.z;
		OutDepthBias = DepthBias;
	#elif ONEPASS_POINTLIGHT_SHADOW
		ShadowDepth = 0;
	#else
		// Output linear, normalized depth
		const float InvMaxSubjectDepth = PassStruct.ShadowParams.w;
		#if PLATFORM_NEEDS_PRECISE_SHADOW_DEPTH
			precise
		#endif
		float AdjustedDepth = ( 1 - OutPosition.z * InvMaxSubjectDepth ) + DepthBias;
		ShadowDepth = AdjustedDepth;
		OutPosition.z = AdjustedDepth;
	#endif
}
```

### Critical Detail: Line 104

```cpp
float AdjustedDepth = ( 1 - OutPosition.z * InvMaxSubjectDepth ) + DepthBias;
```

**This is the shadow depth transform**. Breaking it down:

- `OutPosition.z` — standard DX12 reversed-Z clip-space depth (0 = far plane, 1 = near plane)
- `InvMaxSubjectDepth` — shadow-space parameter: `PassStruct.ShadowParams.w` = `1.0 / (MaxSubjectZ - MinSubjectZ)`
- `1 - OutPosition.z * InvMaxSubjectDepth` — **converts from reversed-Z to linear normalized depth in [0, 1]**
- `+ DepthBias` — depth bias applied **before** storing to texture

**Result**: Shadow maps store **linear normalized depth** (0 = near, 1 = far), NOT reversed-Z.

---

## 2. Shadow Depth Readback (Pixel Shader)

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowProjectionPixelShader.usf`

### Where: Main Shadow Projection Shader

Lines 108–414. The key shadow comparison code:

#### Line 147: Reverse-Z Conversion for Scene Depth

```hlsl
float ShadowZ = 1 - ShadowPosition.z;
ShadowPosition.xyz /= ShadowPosition.w;
```

**Critical**: `1 - ShadowPosition.z` converts the **main view's reversed-Z depth** to linear depth (0 = near, 1 = far), to match the shadow map's linear depth.

This happens **before** any comparison.

#### Lines 169–173: Depth Clamping

```hlsl
// Clamp pixel depth in light space for shadowing opaque, because areas of the shadow depth buffer that weren't rendered to will have been cleared to 1
// We want to force the shadow comparison to result in 'unshadowed' in that case, regardless of whether the pixel being shaded is in front or behind that plane
float LightSpacePixelDepthForOpaque = min(ShadowZ, 0.99999f);
// Must not clamp for SSS shadowing, the subsurface gradient must continue past the far plane
float LightSpacePixelDepthForSSS = ShadowZ;
```

**Detail**: Scene depth is clamped to 0.99999 to avoid comparing against the shadow map's far plane (cleared to 1.0).

#### Lines 242–261: PCF Setup and Call

```hlsl
FPCFSamplerSettings Settings;

Settings.ShadowDepthTexture = ShadowDepthTexture;
Settings.ShadowDepthTextureSampler = ShadowDepthTextureSampler;
Settings.ShadowBufferSize = ShadowBufferSize;
#if SHADING_PATH_DEFERRED && !FORWARD_SHADING && !SUBPIXEL_SHADOW
Settings.TransitionScale = SoftTransitionScale.z * lerp(ProjectionDepthBiasParameters.z, 1.0, NoL);
#else
Settings.TransitionScale = SoftTransitionScale.z;
#endif
Settings.SceneDepth = LightSpacePixelDepthForOpaque;
Settings.bSubsurface = false;
Settings.bTreatMaxDepthUnshadowed = false;
Settings.DensityMulConstant = 0;
Settings.ProjectionDepthBiasParameters = 0;
#if SHADING_PATH_MOBILE
	Shadow = MobileShadowPCF(ShadowPosition.xy, Settings);
#else
	Shadow = ManualPCF(ShadowPosition.xy, Settings);
#endif
```

**Pass to PCF**: `Settings.SceneDepth = LightSpacePixelDepthForOpaque` — the **clamped scene depth**.

---

## 3. Manual PCF Filtering

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowFilteringCommon.ush`

### Where: CalculateOcclusion() — The Comparison Function

Lines 151–180 (4-sample version), Lines 182–214 (3-sample version):

```hlsl
float4 CalculateOcclusion(float4 ShadowmapDepth, FPCFSamplerSettings Settings)
{
	if (Settings.bSubsurface)
	{
		float4 OccluderDistance = max(Settings.SceneDepth - ShadowmapDepth, 0);
		float4 Occlusion = GetSubSurfaceTransmission(OccluderDistance, Settings.DensityMulConstant);
		// Never shadow from depths that were never written to (max depth value)
		return select(ShadowmapDepth > .99f, 1.0f, Occlusion);
	}
	else
	{
		// The standard comparison is SceneDepth < ShadowmapDepth
		// Using a soft transition based on depth difference
		// Offsets shadows a bit but reduces self shadowing artifacts considerably
		float TransitionScale = Settings.TransitionScale;

		// Unoptimized Math: saturate((ShadowmapDepth - Settings.SceneDepth) * TransitionScale + 1);
		// Rearranged the math so that per pixel constants can be optimized from per sample constants.
		float ConstantFactor = (Settings.SceneDepth * TransitionScale - 1);
		float4 ShadowFactor = saturate(ShadowmapDepth * TransitionScale - ConstantFactor);

		FLATTEN
		if (Settings.bTreatMaxDepthUnshadowed)
		{
			ShadowFactor = saturate(ShadowFactor + (ShadowmapDepth > .99f));
		}

		return ShadowFactor;
	}
}
```

**The Comparison (Lines 162–170)**:

```hlsl
// The standard comparison is SceneDepth < ShadowmapDepth
float ConstantFactor = (Settings.SceneDepth * TransitionScale - 1);
float4 ShadowFactor = saturate(ShadowmapDepth * TransitionScale - ConstantFactor);
```

Expanding the math:

```
ShadowFactor = saturate(ShadowmapDepth * TransitionScale - (SceneDepth * TransitionScale - 1))
             = saturate((ShadowmapDepth - SceneDepth) * TransitionScale + 1)
```

**So the comparison is: `ShadowmapDepth > SceneDepth` (unshadowed when true).**

Or equivalently: **`SceneDepth < ShadowmapDepth`** → unshadowed (1.0).

This is a **soft transition**:
- If `SceneDepth << ShadowmapDepth` → fully unshadowed (1.0)
- If `SceneDepth ≈ ShadowmapDepth` → soft transition
- If `SceneDepth >> ShadowmapDepth` → fully shadowed (0.0)

**Important**: TransitionScale is `SoftTransitionScale.z` (typically ~60.0), which controls the fade width.

---

## 4. Sampler State Creation

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.h`

### Directional Shadow Sampler (Line 1161)

```cpp
FRHISamplerState* DepthSamplerState = TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();

SetTextureParameter(BatchedParameters, ShadowDepthTexture, ShadowDepthTextureSampler, DepthSamplerState, ShadowDepthTextureValue);
```

**Template parameters**:
- `SF_Point` — point filtering (no hardware comparison, manual sampling)
- `AM_Clamp, AM_Clamp, AM_Clamp` — clamp UV coordinates

**Important**: **NO comparison function** (`SCF_Less` or similar). UE5 does **manual comparison in the shader** (see CalculateOcclusion above).

### Point Light Shadow Sampler (Line 285 in ShadowRendering.cpp)

```cpp
OutParameters.ShadowDepthCubeTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 0, 0, SCF_Less>::GetRHI();
```

**Template parameters**:
- `SF_Bilinear` — bilinear filtering
- `AM_Clamp, AM_Clamp, AM_Clamp` — clamp all three cube coordinates
- `SCF_Less` — **hardware comparison function**: `depth < shadowmap_depth` → comparison result is 1.0 (lit), 0.0 (shadowed)

**Key difference**: Point light shadows use **hardware PCF** with comparison, while directional shadows use **manual PCF**.

---

## 5. Complete Shadow Sampling Flow

### Summary of UE5's Approach

1. **Depth Rendering** (Shadow Pass):
   - Vertex shader: `AdjustedDepth = (1 - clip_z * InvMaxSubjectDepth) + DepthBias`
   - Result: **Linear normalized [0, 1] depth** stored in shadow texture

2. **Depth Readback** (Shadow Projection):
   - Scene depth from reversed-Z view: `ShadowZ = 1 - ViewSpaceReversedZ`
   - Result: **Linear normalized [0, 1] depth** matching shadow map

3. **Comparison** (Manual PCF):
   - Formula: `saturate((ShadowmapDepth - SceneDepth) * TransitionScale + 1)`
   - Interpretation: **unshadowed if `ShadowmapDepth > SceneDepth`**
   - Soft transition controlled by TransitionScale (~60.0 by default)

4. **Sampler**:
   - Directional/spot: `SF_Point` with **no hardware comparison** (manual in shader)
   - Point lights: `SF_Bilinear` with **`SCF_Less` hardware comparison**

---

## 6. Key Parameters and Their Sources

### ProjectionDepthBiasParameters (Line 58 in ShadowProjectionPixelShader.usf)

```hlsl
// .x:DepthBias, .y:SlopeDepthBias, .z:ReceiverBias, .w: MaxSubjectZ - MinSubjectZ
float4 ProjectionDepthBiasParameters;
```

Set at line 1170 in ShadowRendering.h:

```cpp
SetShaderValue(BatchedParameters, ProjectionDepthBias,
	FVector4f(
		ShadowInfo->GetShaderDepthBias(),
		ShadowInfo->GetShaderSlopeDepthBias(),
		ShadowInfo->GetShaderReceiverDepthBias(),
		ShadowInfo->MaxSubjectZ - ShadowInfo->MinSubjectZ
	)
);
```

### ShadowBufferSize (Line 20 in ShadowFilteringCommon.ush)

```hlsl
//XY - Pixel size of shadowmap
//ZW - Inverse pixel size of shadowmap
float4 ShadowBufferSize;
```

Used for texture coordinate scaling in PCF (line 248 in ShadowFilteringCommon.ush):

```hlsl
float2 TexelPos = ShadowPosition * Settings.ShadowBufferSize.xy - 0.5f;
```

### SoftTransitionScale (Line 250 in ShadowProjectionPixelShader.usf)

```hlsl
Settings.TransitionScale = SoftTransitionScale.z;
```

Controls the fade region width. Default from cvar `r.Shadow.TransitionScale` = 60.0 (line 68-72 in ShadowRendering.cpp).

---

## 7. Reversed-Z Handling

### The Critical Insight: Two Different Depth Spaces

| Space | Semantics | UE5 Handling |
|-------|-----------|-------------|
| **View Clip-Space (Reversed-Z)** | 0 = far, 1 = near | Main view depth buffer |
| **Shadow Texture** | 0 = near, 1 = far | Converted by `1 - clip_z * InvMaxSubjectDepth` |

**Why**: Shadow maps use standard DX12 conventions (linear), but the main view uses reversed-Z for better precision. UE5 converts both to the same space before comparison.

### The Conversion (Line 147 in ShadowProjectionPixelShader.usf)

```hlsl
float ShadowZ = 1 - ShadowPosition.z;
```

This converts the **reversed-Z projected scene depth** to match the **linear shadow map depth**.

---

## 8. Depth Bias Application

### Vertex Shader (Lines 83–90 in ShadowDepthVertexShader.usf)

```hlsl
const float MaxSlopeDepthBias = PassStruct.ShadowParams.z;
const float Slope = clamp(abs(NoL) > 0 ? sqrt(saturate(1 - NoL*NoL)) / NoL : MaxSlopeDepthBias, 0, MaxSlopeDepthBias);

const float SlopeDepthBias = PassStruct.ShadowParams.y;
const float SlopeBias = SlopeDepthBias * Slope;

const float ConstantDepthBias = PassStruct.ShadowParams.x;
const float DepthBias = SlopeBias + ConstantDepthBias;
```

Then added to depth output (line 104):

```hlsl
float AdjustedDepth = ( 1 - OutPosition.z * InvMaxSubjectDepth ) + DepthBias;
```

**Order**: Compute `(1 - clip_z * InvMax)` first (linear conversion), **then** add bias.

### Pixel Shader (No additional bias)

The depth stored in the shadow texture already includes bias. The comparison in CalculateOcclusion is bias-free; bias is absorbed into the stored depth.

---

## 9. Self-Shadowing Artifact Prevention

### Receiver Bias (Line 248 in ShadowProjectionPixelShader.usf)

```hlsl
Settings.TransitionScale = SoftTransitionScale.z * lerp(ProjectionDepthBiasParameters.z, 1.0, NoL);
```

For deferred shading on receivers:
- **High NoL** (front-facing): use standard TransitionScale
- **Low NoL** (grazing): use `TransitionScale * ReceiverBias` (attenuate comparison tightness)

Prevents self-shadowing on thin geometry facing away from light.

### Max Depth Clamping (Line 171 in ShadowProjectionPixelShader.usf)

```hlsl
float LightSpacePixelDepthForOpaque = min(ShadowZ, 0.99999f);
```

Shadow maps are cleared to 1.0 (far plane). Clamping to 0.99999 ensures any unrendered pixel (depth = 1.0) is treated as unshadowed, regardless of comparison direction.

---

## 10. Gathering the Exact Constants

### Default Shadow CVars (ShadowRendering.cpp)

```cpp
// Line 30-34: CSM (Cascaded Shadow Maps)
CVarCSMShadowDepthBias = 10.0f;
CVarCSMShadowSlopeScaleDepthBias = 3.0f;

// Line 68-72: Transition/Fade Region
CVarShadowTransitionScale = 60.0f;

// Line 74-78: Receiver Bias
CVarCSMShadowReceiverBias = 0.9f;

// Line 83-93: Point Light Shadows
CVarPointLightShadowDepthBias = 0.02f;
CVarPointLightShadowSlopeScaleDepthBias = 3.0f;
```

---

## Implementation Checklist for UzEngine

- [ ] **Shadow depth vertex shader**: Implement `AdjustedDepth = (1 - clip_z * InvMaxSubjectDepth) + DepthBias`
- [ ] **Shadow projection pixel shader**: Implement reversed-Z → linear conversion `ShadowZ = 1 - ViewDepth`
- [ ] **PCF comparison**: Implement `saturate((ShadowmapDepth - SceneDepth) * TransitionScale + 1)`
- [ ] **Sampler state**: Use `SF_Point` + `AM_Clamp` (manual PCF, no hardware comparison)
- [ ] **Depth clamping**: Clamp scene depth to 0.99999 before PCF
- [ ] **TransitionScale**: Default 60.0, exposed as cvar
- [ ] **Receiver bias**: Apply only in deferred shading path based on NoL
- [ ] **Slope bias**: Apply at depth rendering time (vertex shader)

---

## References

- `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowDepthVertexShader.usf` — Depth output
- `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowProjectionPixelShader.usf` — Depth projection
- `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowFilteringCommon.ush` — PCF comparison
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp` — C++ setup
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.h` — Sampler creation
