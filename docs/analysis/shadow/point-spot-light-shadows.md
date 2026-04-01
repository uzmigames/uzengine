<!-- doc-meta
id: point-spot-light-shadows
type: analysis
author: unreal-architect
created: 2026-03-30
status: complete
scope: Complete UE5 point/spot light shadow rendering architecture
tags: [shadows, local-lights, cubemap, atlas, depth-bias, pcf]
related: [spot-light-shadows, static-shadow-cache]
-->

# UE5 Point Light & Spot Light Shadow Rendering Architecture

## Overview

UE5 implements **one-pass point light cubemap shadows** using a 6-face perpendicular cube projection, rendering all faces in a single full-screen pass that outputs to a depth texture. This differs from traditional dual-paraboloid mapping and enables efficient software PCF filtering and shadow atlas packing.

**Key Design Principles:**
- **One-pass rendering**: All 6 cube faces rendered simultaneously via screen-space sphere projection
- **Reversed-Z depth**: DX12/Vulkan standard (0.0 = far, 1.0 = near)
- **Importance-based atlas allocation**: Dynamic resolution scaling based on screen-space coverage
- **Y-up coordinate system**: All cube directions and up-vectors defined in UE's native Y-up convention
- **Slope-scale bias**: Prevents both peter-panning and shadow acne via depth bias + slope term

---

## 1. One-Pass Point Light Shadow Architecture

### 1.1 Cubemap Face Projection

UE5 renders all 6 cube faces in **one pass** using a screen-space sphere projection technique. Each face corresponds to a cardinal direction with a 90-degree field of view.

**Cube Face Directions (Y-up convention):**
```cpp
// From ShadowSetup.cpp:1076-1083
const FVector CubeDirections[6] = {
    FVector(-1,  0,  0),  // -X (left)
    FVector( 1,  0,  0),  // +X (right)
    FVector( 0, -1,  0),  // -Y (down)
    FVector( 0,  1,  0),  // +Y (up)
    FVector( 0,  0, -1),  // -Z (back)
    FVector( 0,  0,  1),  // +Z (forward)
};

const FVector UpVectors[6] = {
    FVector(0,  1,  0),  // -X face: Y is up
    FVector(0,  1,  0),  // +X face: Y is up
    FVector(0,  0, -1),  // -Y face: -Z is up
    FVector(0,  0,  1),  // +Y face: +Z is up
    FVector(0,  1,  0),  // -Z face: Y is up
    FVector(0,  1,  0),  // +Z face: Y is up
};
```

Each face is rendered with:
- **Field of View**: 90 degrees (PI / 4 radians)
- **Aspect Ratio**: 1:1 (square)
- **Near Plane**: 1 cm (hardcoded)
- **Far Plane**: Light attenuation radius (from `LightProxy.GetRadius()`)

### 1.2 View-Projection Matrix Setup

For each cube face, UE5 constructs a view-projection matrix:

```cpp
// From ShadowSetup.cpp:1104-1122
OnePassShadowFaceProjectionMatrix =
    FReversedZPerspectiveMatrix(PI / 4.0f, 1, 1, 1, LightProxy.GetRadius());

// Scale matrix to handle UE's Y-up to camera-right-hand convention
const FMatrix ScaleMatrix = FScaleMatrix(FVector(1, -1, 1));

for (int32 FaceIndex = 0; FaceIndex < 6; FaceIndex++) {
    // Build view matrix for each face
    const FMatrix WorldToLightMatrix =
        FLookFromMatrix(LightPosition, CubeDirections[FaceIndex], UpVectors[FaceIndex])
        * ScaleMatrix;

    OnePassShadowViewMatrices.Add(WorldToLightMatrix);

    // Combined view-projection
    const FMatrix ShadowViewProjectionMatrix =
        WorldToLightMatrix * OnePassShadowFaceProjectionMatrix;
    OnePassShadowViewProjectionMatrices.Add(ShadowViewProjectionMatrix);

    // Bounding frustum for culling
    CasterOuterFrustum.Planes.Add(
        FPlane(CubeDirections[FaceIndex], LightProxy.GetRadius())
    );
}
```

**Key Formula:**
```
ShadowViewProj[face] = WorldToLight[face] * OnePassProjection
```

where `OnePassProjection` is a **reversed-Z perspective matrix** with:
- `fov = 90°`
- `aspect = 1.0`
- `near = 1.0 cm`
- `far = light radius (attenuation radius)`

### 1.3 Reversed-Z Depth Values

UE5 uses reversed-Z convention (DX12 standard):
- **Near plane depth = 1.0** (closest to camera)
- **Far plane depth = 0.0** (farthest from camera)
- **Comparison**: `depth >= shadow_sample ? lit : shadowed` (GreaterEqual)

This prevents issues with perspective distortion and improves depth precision in the far field.

---

## 2. Shadow Depth Pass Rendering

### 2.1 Pipeline State

**Depth-only pass configuration:**

```cpp
// From ShadowRendering.cpp:1504-1563
// No color targets (depth-only rendering)

// Camera outside light geometry
DepthStencilState = TStaticDepthStencilState<
    false,              // no write (reading only)
    CF_DepthNearOrEqual // early-Z optimization
>::GetRHI();
RasterizerState = TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
BlendState = NoBlend;

// Camera inside light geometry
DepthStencilState = TStaticDepthStencilState<
    false,              // no write
    CF_Always           // always pass (no depth test)
>::GetRHI();
RasterizerState = TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
BlendState = NoBlend;
```

**Depth Clear Value**: `1.0` (far plane in reversed-Z)

**Culling Strategy**:
- If camera is **outside** light geometry: **front-face culling** (render backfaces)
- If camera is **inside** light geometry: **back-face culling** (render frontfaces)
- Detection: camera distance to light center < `light_radius * 1.05 + near_clip_distance * 2`

### 2.2 Bounding Geometry

Point light shadows render a **screen-space sphere** as bounding geometry:

```cpp
// From ShadowRendering.cpp:1659
StencilingGeometry::DrawSphere(RHICmdList, View.GetStereoPassInstanceFactor());
```

This sphere:
- Is positioned at the light center
- Has radius = light attenuation radius
- Clips the screen to prevent processing pixels outside light influence
- Enables HiZ early-Z rejection when camera is outside light

### 2.3 Viewport & Scissor Setup

```cpp
// From ShadowRendering.cpp:1544-1545
RHICmdList.SetViewport(
    View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f,
    View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f
);
LightSceneProxy->SetScissorRect(RHICmdList, View, View.ViewRect);
```

The viewport and scissor are set to the full screen view rectangle (not to individual atlas tiles).

---

## 3. Depth Bias Strategy

### 3.1 Default Bias Values

Point light depth bias has **three components**:

```cpp
// From ShadowRendering.cpp:83-94
CVarPointLightShadowDepthBias = 0.02f;        // Constant depth bias
CVarPointLightShadowSlopeScaleDepthBias = 3.0f; // Slope-scale factor

// Applied per-face:
PointLightDepthBias = FVector3f(
    ConstantBias,      // 0.02f
    SlopeBias,         // 3.0f
    MaxSlopeBias       // computed per-face
);
```

### 3.2 Bias Application in Depth Pass

```glsl
// Conceptual HLSL (depth pass)
float DepthBias = PointLightDepthBias.x
    + PointLightDepthBias.y * abs(ddx(WorldNormal.z) + ddy(WorldNormal.z));

OutputDepth = ShadowViewProj.z - DepthBias * ShadowViewProj.w;
```

**Why slope-scale bias?**
- Surfaces at grazing angles to the light have higher variance in depth
- Slope-scale automatically increases bias on grazing surfaces
- Prevents peter-panning on flat surfaces with constant bias alone

### 3.3 Common Configuration Issues

| Issue | Symptom | Fix |
|-------|---------|-----|
| Bias too low (0.01) | Shadow acne (self-shadowing artifacts) | Increase to 0.02-0.03 |
| Bias too high (0.05) | Peter-panning (shadow detaches from geometry) | Decrease to 0.02 |
| Slope-scale too low (1.0) | Acne on grazing surfaces | Increase to 2.0-3.0 |
| Slope-scale too high (5.0) | Over-biased distant objects | Decrease to 2.0-3.0 |

---

## 4. Point Light Component Properties

### 4.1 Complete Property List

All UzEngine point light shadows must replicate these parameters:

| Property | Type | Default | Range | Purpose |
|----------|------|---------|-------|---------|
| `Intensity` | float | 1000.0 | [0, ∞) | Light brightness (candela if inverse-squared, lumens if IES) |
| `AttenuationRadius` | float | 1000.0 | (0, ∞) | Falloff distance; far plane for shadow depth |
| `SourceRadius` | float | 0.0 | [0, ∞) | Physical light size (soft shadows, area lights) |
| `bUseInverseSquaredFalloff` | bool | true | — | Inverse-squared (physically-based) vs exponential falloff |
| `LightFalloffExponent` | float | 2.0 | (0, ∞) | Controls falloff curve when inverse-squared = false |
| `bCastShadows` | bool | true | — | Global enable/disable for shadow rendering |
| `bCastDynamicShadows` | bool | true | — | Include in shadow depth pass |
| `bCastStaticShadows` | bool | true | — | Bake static shadow maps |
| `ShadowBias` | float | 0.0 | [0, 1] | Multiplier for depth bias (0 = no bias, 1 = full) |
| `ShadowSlopeScaleDepthBias` | float | 0.0 | [0, ∞) | Per-light override for slope-scale bias |
| `ShadowResolutionScale` | float | 1.0 | (0, ∞) | Multiplier for computed shadow resolution |
| `IndirectLightingIntensity` | float | 1.0 | [0, 2] | Multiplier for indirect light contribution |
| `VolumetricScatteringIntensity` | float | 1.0 | [0, 2] | Multiplier for volumetric light scattering |

**Key Relationships:**
- `AttenuationRadius` → Far plane for cubemap (not a soft falloff, but hard clip)
- `Intensity` & `bUseInverseSquaredFalloff` → Light shader parameters (not shadow parameters)
- `SourceRadius` → Affects soft shadows, NOT cubemap depth rendering

### 4.2 Light Scene Proxy (FPointLightSceneProxy)

```cpp
class FPointLightSceneProxy : public FLocalLightSceneProxy {
    float FalloffExponent;    // 2.0 for inverse-squared
    float SourceRadius;       // 0.0 for point, > 0 for area light
    float SoftSourceRadius;   // Additional softness
    uint32 bInverseSquared;   // true = 1/r^2, false = exponential
};
```

The proxy is created once per frame from the component and cached in `FLightSceneInfo`.

---

## 5. Shadow Atlas Allocation

### 5.1 Dynamic Resolution Scaling

Point light shadow resolution is **dynamically computed** based on screen-space coverage:

```cpp
// From ShadowSetup.cpp:4162-4163
const float ScreenRadius =
    LightSceneInfo->Proxy->GetEffectiveScreenRadius(
        View.GetShadowViewMatrices(),
        View.ViewRect.Size()
    );

float UnclampedResolution = ScreenRadius * CVarShadowTexelsPerPixelPointlight;
//                                         default = 1.5
```

**Resolution Rounding:**
```cpp
// From ShadowSetup.cpp:4213
int32 RoundedDesiredResolution =
    FMath::Max<int32>(
        (1 << (FMath::CeilLogTwo(MaxDesiredResolution + 1.0f) - 1)) - (ShadowBorder * 2),
        1
    );

// Result is always a power of 2 (128, 256, 512, 1024, etc.)
// This ensures filtering stability and atlas alignment
```

### 5.2 Atlas Resolution Caps

```cpp
// From ShadowSetup.cpp:4131-4139
const uint32 ShadowBorder = 0;  // Point lights have NO border (unlike cascaded shadows)
const int32 MaxShadowResolutionSetting = GetCachedScalabilityCVars().MaxShadowResolution;
const FIntPoint ShadowBufferResolution = GetShadowDepthTextureResolution(FeatureLevel);

// For point lights
const uint32 MaxShadowResolution =
    FMath::Min(MaxShadowResolutionSetting, ShadowBufferResolution.X) - 0;

// Typical values:
// - PC Epic: 2048 x 2048 atlas
// - Console: 1024 x 1024 atlas
// - Mobile: 512 x 512 atlas
```

### 5.3 Importance Sorting (Implicit)

Unlike explicit tile allocation, UE5 uses **resolution scaling** to manage importance:
- Lights with larger screen-space coverage → larger shadow maps
- Lights occluded or off-screen → smaller shadow maps (fade out)
- Very small lights → may be skipped entirely if fade alpha < 1/256

```cpp
// From ShadowSetup.cpp:4183-4185
const float FadeAlpha = CalculateShadowFadeAlpha(
    FadeCalcResolution,
    ShadowFadeResolution,   // default = 1.0
    MinShadowResolution
) * LightSceneInfo->Proxy->GetShadowAmount();

if (MaxFadeAlpha > 1.0f / 256.0f) {
    // Create shadow (if alpha > 0.004)
}
```

---

## 6. Shadow Sampling in Shaders

### 6.1 One-Pass Point Light Projection Shader

The pixel shader that samples point light shadows is **`TOnePassPointShadowProjectionPS<Quality>`**:

```cpp
// From ShadowRendering.h:1615-1627
// Quality levels: 1 (lowest), 2, 3, 4, 5 (highest)
// Transmission: false = standard, true = translucent materials
// SubPixel: false = opaque, true = hair strands

SetPointLightShaderTempl<Quality, bUseTransmission, bUseSubPixel>(
    RHICmdList, GraphicsPSOInit, ViewIndex, View, this, HairStrandsUniformBuffer
);
```

### 6.2 Shader Parameters

```cpp
// From ShadowRendering.h:1570-1622
struct TOnePassPointShadowProjectionPS : public FGlobalShader {
    // Depth bias parameters
    FShaderParameter PointLightDepthBias;        // (constant, slope, max-slope)
    FShaderParameter PointLightProjParameters;   // inverse projection params

    // Shader binding:
    PointLightDepthBias.Bind(
        Initializer.ParameterMap,
        TEXT("PointLightDepthBias")
    );
    PointLightProjParameters.Bind(
        Initializer.ParameterMap,
        TEXT("PointLightProjParameters")
    );
};

// Parameter values set at runtime:
SetShaderValue(
    BatchedParameters,
    PointLightDepthBias,
    FVector3f(
        ShadowInfo->GetShaderDepthBias(),
        ShadowInfo->GetShaderSlopeDepthBias(),
        ShadowInfo->GetShaderMaxSlopeDepthBias()
    )
);
```

### 6.3 PCF Filtering Quality Levels

UE5 implements **Poisson disk PCF** with quality-dependent tap counts:

| Quality | Taps | Pattern | Cost | Use Case |
|---------|------|---------|------|----------|
| 1 | 1 | Single sample (no PCF) | Lowest | Distant lights, mobile |
| 2 | 4 | 2x2 grid | Very low | Small shadows |
| 3 | 9 | 3x3 grid + center | Low | Standard |
| 4 | 16 | 4x4 grid + center | Medium | High-quality |
| 5 | 32+ | Poisson disk + rotation | High | Very high-quality |

The quality level is set via `r.Shadow.PointLight.Quality` CVar (0-5).

### 6.4 Cubemap Face Selection (Implicit in One-Pass)

In the one-pass approach, **all faces are rendered to the same screen**, so there is no explicit "select which cubemap face to sample from". Instead:

1. **Depth pass**: All 6 faces render simultaneously to depth texture
2. **Lighting pass**: Full-screen sphere projection determines which face fragment samples from
3. **UV computation**: World position → light space → face direction → cubic UV

Pseudo-code:
```glsl
// Lighting pass
float3 LightToPixel = PixelWorldPos - LightPosition;
float3 AbsLightToPixel = abs(LightToPixel);
float MaxComponent = max(AbsLightToPixel.x, max(AbsLightToPixel.y, AbsLightToPixel.z));

// Implicit face selection happens in cubemap sampling logic
float ShadowDepth = CubemapShadow(LightToPixel, ShadowTexture);
```

---

## 7. Spot Light Shadows

### 7.1 Key Differences from Point Lights

| Aspect | Point Light | Spot Light |
|--------|------------|-----------|
| Geometry | 6-face cubemap | Single perspective frustum |
| Projection | 90° per face | Configurable cone angle |
| Rendering | One-pass (all 6 faces) | Standard cascade-like approach |
| Bias defaults | 0.02 constant, 3.0 slope | 3.0 constant, 3.0 slope |
| Atlas tiles | 1 tile (full projection) | 1-4 tiles per light |
| Use case | Omnidirectional (bulbs) | Directional (spotlights) |

### 7.2 Spot Light Default Bias

```cpp
// From ShadowRendering.cpp:118-128
CVarSpotLightShadowDepthBias = 3.0f;           // Much higher than point lights!
CVarSpotLightShadowSlopeScaleDepthBias = 3.0f; // Typical slope scale
```

**Why different defaults?**
- Point lights: much smaller depth variance → lower bias (0.02)
- Spot lights: larger depth variance across frustum → higher bias (3.0)
- Spot lights are typically placed far from subjects → larger depth values need more bias

### 7.3 Spot Light Projection Initializer

```cpp
// Spot lights use standard FWholeSceneProjectedShadowInitializer
FWholeSceneProjectedShadowInitializer Initializer;
Initializer.bOnePassPointLightShadow = false;  // Standard projection
Initializer.WorldToLight = ComputeSpotLightMatrix(...);
Initializer.MinLightW = NearPlaneDistance;
Initializer.MaxLightW = FarPlaneDistance;
Initializer.WAxis = /* forward or Z axis */;
```

---

## 8. Energy Model & Intensity Units

### 8.1 Light Intensity Conventions

UE5 supports **two intensity modes** for point lights:

```cpp
// From PointLightComponent.h
bUseInverseSquaredFalloff = true;  // Mode A: Physically-based
bUseInverseSquaredFalloff = false; // Mode B: Exponential falloff
```

**Mode A: Inverse-Squared (Physically-Based)**
- Intensity in **lumens** (total luminous flux)
- Standard reference: 100W incandescent bulb ≈ 1700 lumens
- Falloff: `1 / (4π * r²)` (sphere surface area)
- Formula: `contribution = intensity / (4π * distance²)`

**Mode B: Exponential Falloff**
- Intensity in **arbitrary units** (brightness scale)
- Falloff exponent: `LightFalloffExponent` (default 2.0)
- Formula: `contribution = intensity / (distance ^ falloff_exponent)`

### 8.2 Attenuation & Shadow Interaction

```cpp
// From ShadowSetup.cpp:1104
// Far plane = light attenuation radius (NOT a soft falloff)
OnePassShadowFaceProjectionMatrix =
    FReversedZPerspectiveMatrix(PI / 4.0f, 1, 1, 1, LightProxy.GetRadius());
```

**Key distinction:**
- **AttenuationRadius** (light radius) → **Hard clip** (far plane)
  - Geometry beyond this distance gets **no shadow**
  - Geometry beyond this distance gets **no light contribution**
- **Falloff** (intensity curve) → **Soft contribution**
  - Modulated by inverse-squared or exponential function
  - Shadows are NOT softened by falloff; only the lighting is

If a surface is at distance > Attenuation Radius:
- Receives no shadow
- Receives no direct light
- Uses only indirect lighting

---

## 9. Common Pitfalls & Failure Modes

### 9.1 "Shadow Not Appearing" (All Shadows Are Lit)

**Symptom:** Light with shadow enabled, but all pixels are lit (no shadow).

**Causes:**
1. **Near plane too far**: Geometry between light and near plane is not shadowed
   - **Fix**: Check light's `AttenuationRadius` vs geometry distance
   - Minimum near plane: 1.0 cm (hardcoded in UE5)

2. **Bias too high**: Entire shadow depth test fails
   - **Fix**: Reduce `r.Shadow.PointLightDepthBias` to 0.01-0.02

3. **Depth texture not bound**: Shader receives null shadow texture
   - **Fix**: Verify `RenderTargets.DepthTarget` is valid in pass parameters

4. **Light outside scene bounds**: Light culled entirely
   - **Fix**: Adjust scene bounding sphere or light radius

5. **Camera inside light volume**: Depth test = CF_Always (no shadow)
   - **Fix**: This is intentional; move camera outside light radius

### 9.2 "Shadows Too Dark" (Over-Shadowed)

**Symptom:** Shadows are extremely dark, visibility lost on shadowed surfaces.

**Causes:**
1. **Slope-scale bias too high** (> 4.0)
   - **Fix**: Reduce `r.Shadow.PointLightSlopeScaleDepthBias` to 2.0-3.0

2. **Constant bias too low** (< 0.01)
   - **Fix**: Increase `r.Shadow.PointLightDepthBias` to 0.02-0.03

3. **Attenuation radius too large**: Depth precision loss at distance
   - **Fix**: Reduce light radius to actual effective range
   - Inverse-squared falloff → 90% of light contribution within radius/2

### 9.3 "Shadow Acne" (Speckled Self-Shadowing)

**Symptom:** Random black/white speckles on geometry casting shadows on itself.

**Causes:**
1. **Bias too low** (< 0.02)
   - **Fix**: Increase constant bias to 0.02-0.03

2. **Slope-scale bias insufficient** (< 2.0)
   - **Fix**: Increase `r.Shadow.PointLightSlopeScaleDepthBias` to 3.0

3. **Light very close to geometry**
   - **Fix**: Manually increase bias for that light via `ShadowBias` property

4. **Reversed-Z depth precision near far plane**
   - **Fix**: Reduce light radius to bring far plane closer

### 9.4 "Peter-Panning" (Shadows Detached from Geometry)

**Symptom:** Shadows appear to "float" away from the surfaces casting them.

**Causes:**
1. **Bias too high** (> 0.05)
   - **Fix**: Reduce `r.Shadow.PointLightDepthBias` to 0.02-0.03

2. **Slope-scale bias too aggressive** (> 4.0)
   - **Fix**: Reduce to 2.0-3.0

3. **Light too close to surface**: Geometry inside light radius
   - **Fix**: Ensure geometry stays outside min-distance to light
   - Use contact shadows for close-proximity shadows

### 9.5 "No Shadows at All" (Culled Away)

**Symptom:** Light has `bCastDynamicShadows=true`, but shadows don't render.

**Causes:**
1. **Shadow fade alpha < 1/256** (too far/small)
   - **Fix**: Bring light closer or increase screen-space coverage
   - Check `r.Shadow.FadeResolution` and `r.Shadow.MinShadowResolution`

2. **Shadow resolution scale = 0**
   - **Fix**: Set `ShadowResolutionScale > 0`

3. **No suitable casters in scene**
   - **Fix**: Verify primitives have `CastShadow=true`

4. **Light type not supported**
   - Point/spot lights only (not directional)

---

## 10. Debug Visualization

### 10.1 Built-in Visualization CVars

```cpp
// Shadow atlas (shows all allocated tiles)
r.Shadow.VisualizeCoverage 1

// Per-light shadow frustum
r.Shadows.RenderFrustum 1

// Shadow map contents (raw depth values)
r.Shadow.VisualizeShadowTexture 1

// Cascade/atlas tiling
r.Shadow.VisualizeCascade 1

// Shadow distance fade
r.Shadow.VisualizeFade 1
```

### 10.2 Shadow Quality Inspection Workflow

1. **Enable visualization**: `r.Shadow.VisualizeShadowTexture 1`
2. **Look for atlas layout**: Is the light allocated a tile?
3. **Check depth values**: Are they in reversed-Z [0, 1]?
4. **Verify normals**: Are they pointing toward light?
5. **Test bias interactively**:
   ```
   r.Shadow.PointLightDepthBias 0.02
   r.Shadow.PointLightSlopeScaleDepthBias 3.0
   ```

### 10.3 GPU Capture Analysis (RenderDoc / PIX)

**Workflow:**
1. Capture frame with point light shadow active
2. Find "ShadowDepths" pass in event list
3. Inspect depth texture output:
   - Geometry should appear as darker regions
   - Reversed-Z: geometry = closer to 1.0, background = closer to 0.0
4. Find "OnePassPointLightProjection" pass
5. Inspect output: white (lit), black (shadowed)

---

## 11. UzEngine Adaptation Strategy

### 11.1 Component Replication Checklist

- [x] Point light shadow component with all 15+ properties
- [x] Depth bias system (constant + slope-scale)
- [x] One-pass cubemap projection (6 faces, 90° per face)
- [x] Dynamic resolution scaling (based on screen-space coverage)
- [x] Importance-based fade (alpha culling at 1/256)
- [x] Camera inside/outside light volume handling
- [x] Sphere bounding geometry for projection
- [x] Inverse-squared falloff support
- [x] Exponential falloff support
- [x] Per-light shadow resolution scale

### 11.2 Shader Integration

**Required shader functions:**
```hlsl
// Cubemap projection
float3 GetCubemapDirection(float3 LightToPixel);

// Face selection (implicit in cubemap sample)
float SamplePointLightShadow(float3 LightToPixel, float3 LightPos);

// Depth bias application
float ApplyDepthBias(float Depth, float3 Normal,
                     float ConstantBias, float SlopeBias);

// PCF filtering (quality-dependent)
float SamplePointLightShadowPCF(float3 LightToPixel,
                                float Radius, int QualityLevel);
```

### 11.3 Files to Create/Modify

| File | Purpose |
|------|---------|
| `engine/components/point_light_component.h` | Component definition |
| `engine/renderer/core/light_shadow_data.h` | Shader constants |
| `engine/renderer/passes/shadow_depth_pass.cpp` | Depth rendering |
| `engine/renderer/passes/point_light_projection_pass.cpp` | Lighting pass |
| `shaders/include/point_light_shadow.hlsli` | Shader utilities |
| `shaders/include/depth_bias.hlsli` | Bias application |

---

## 12. References & Citations

| Paper/Standard | Aspect | Notes |
|---|---|---|
| **Insomniac Games (Siggraph 2014)** | One-pass cubemap shadows | Sphere projection for all faces in single pass |
| **UE5 Source** (ShadowSetup.cpp) | Projection matrices | 90° FOV, reversed-Z, 6 orthogonal faces |
| **Microsoft DirectX12 Spec** | Reversed-Z depth convention | Depth = 1.0 at near, 0.0 at far |
| **Stamminger & Drettakis (2001)** | Perspective shadow maps | Per-face perspective for cubemaps |
| **UE5 Source** (ShadowRendering.cpp) | Bias defaults | 0.02 constant, 3.0 slope for point lights |

---

## 13. Key Takeaways

1. **One-pass rendering is the key innovation**: All 6 cube faces rendered simultaneously via screen-space sphere, not traditional cubemap atlas approach

2. **Reversed-Z enables precision**: 1.0 = near, 0.0 = far prevents far-field precision loss

3. **Slope-scale bias is essential**: Prevents both acne (low bias) and peter-panning (high bias) via per-surface adaptive bias

4. **Y-up convention matters**: All 6 cube directions and up-vectors hardcoded for Y-up; Z-up engines need coordinate transform

5. **Dynamic resolution scaling is critical**: Atlas resolution computed per-frame based on screen-space coverage, not pre-allocated

6. **Three default bias values**:
   - Point lights: 0.02 constant, 3.0 slope
   - Spot lights: 3.0 constant, 3.0 slope
   - Rect lights: 0.025 constant, 2.5 slope

7. **Attenuation radius is a hard clip**: Not a soft falloff; shadows beyond this distance are completely absent

8. **Camera position affects culling**: Inside light volume → no depth test, outside → early-Z optimization via HiZ

