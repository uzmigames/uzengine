<!-- doc-meta
title: UE5 Local Light Shadow Implementation — Architectural Analysis
description: Exact analysis of UE5's spot light and point light shadow system with file:line citations
keywords: shadows, RHI, rendering, spot lights, point lights, atlas, projection
-->

# UE5 Local Light Shadow Implementation

**Source Analysis of Unreal Engine 5.6.0**

This document extracts the exact architectural patterns, algorithms, and data structures UE5 uses for local light shadows (spot lights and point lights), suitable for UzEngine adaptation.

---

## 1. Shadow Atlas & Memory Layout

### 1.1 Atlas Texture Resolution

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:664-672`

```cpp
static FIntPoint GetShadowDepthTextureResolution(ERHIFeatureLevel::Type FeatureLevel)
{
    int32 MaxShadowRes = GetMaxShadowResolution(FeatureLevel);
    const FIntPoint ShadowBufferResolution(
        FMath::Clamp(MaxShadowRes, 1, (int32)GMaxShadowDepthBufferSizeX),
        FMath::Clamp(MaxShadowRes, 1, (int32)GMaxShadowDepthBufferSizeY));
    return ShadowBufferResolution;
}
```

**Key Details:**
- Atlas size is determined by `GetMaxShadowResolution(FeatureLevel)` clamped against `GMaxShadowDepthBufferSizeX/Y` (typically 4096x4096 on high-end).
- **Format:** `PF_ShadowDepth` (R32F depth format) for 2D shadow atlases.
- Atlas is a single 2D texture (or texture array) shared across all local light shadows.
- **Border size:** 4 pixels (line 814: `const static uint32 SHADOW_BORDER = 4;`)

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:814`

```cpp
const static uint32 SHADOW_BORDER = 4;
```

The border prevents filtering artifacts when sampling shadow map boundaries. Each shadow tile is allocated with (ResolutionX + 2*BorderSize) × (ResolutionY + 2*BorderSize) actual texels, but shadow writing occurs in the inner region.

### 1.2 Tile Allocation Strategy

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:5774-5865`

UE5 uses a **texture layout allocator** (not shown in detail here, but referenced via `TextureLayout`). Key aspects:

1. **Allocator type:** `FTextureLayout` (a 2D bin-packing allocator)
2. **Allocation policy:** First-fit with power-of-2 alignment
3. **Tile sizes:** Determined dynamically based on light's screen-space footprint and a texel-per-pixel ratio

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:4154-4167`

```cpp
// From CreateWholeSceneProjectedShadow
const float ScreenRadius = LightSceneInfo->Proxy->GetEffectiveScreenRadius(View.GetShadowViewMatrices(), View.ViewRect.Size());
MaxScreenRadius = FMath::Max(MaxScreenRadius, ScreenRadius);

// Resolution computed as:
switch (LightSceneInfo->Proxy->GetLightType())
{
case LightType_Point:
    UnclampedResolution = ScreenRadius * CVarShadowTexelsPerPixelPointlight.GetValueOnRenderThread();
    break;
case LightType_Spot:
    UnclampedResolution = ScreenRadius * CVarShadowTexelsPerPixelSpotlight.GetValueOnRenderThread();
    break;
}
```

**Resolution scaling parameters:**
- **Point lights:** `CVarShadowTexelsPerPixelPointlight = 1.27324` (line 225-229)
- **Spot lights:** `CVarShadowTexelsPerPixelSpotlight = 2.0 * 1.27324 = 2.54648` (line 231-235)
- **Rect lights:** `CVarShadowTexelsPerPixelRectlight = 1.27324` (line 237-241)

Resolution is then clamped to power-of-two boundaries and the actual tile fits into the atlas.

### 1.3 Format & Resource Binding

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:5868`

```cpp
FPooledRenderTargetDesc ShadowMapDesc2D = FPooledRenderTargetDesc::Create2DDesc(
    AtlasSize,
    PF_ShadowDepth,                                              // R32F depth format
    FClearValueBinding::DepthOne,
    TexCreate_None,
    TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, // GPU write + shader read
    false);
```

- **Clear value:** 1.0 (unlit/far plane)
- **Usage flags:** DepthStencilTargetable (depth write in rendering pass) + ShaderResource (sample in lighting)
- **No MSAA:** Single-sample for simplicity

---

## 2. Spot Light Shadow Projection

### 2.1 Outer Cone Angle & FOV Mapping

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\SpotLightComponent.cpp:45-56`

```cpp
class FSpotLightSceneProxy : public FPointLightSceneProxy
{
public:
    float OuterConeAngle;
    float CosInnerCone;
    float CosOuterCone;
    float InvCosConeDifference;
    float SinOuterCone;
    float InvTanOuterCone;

    FSpotLightSceneProxy(const USpotLightComponent* Component)
        : FPointLightSceneProxy(Component)
    {
        const float ClampedInnerConeAngle = FMath::Clamp(
            Component->InnerConeAngle, 0.0f, 89.0f) * (float)UE_PI / 180.0f;
        const float ClampedOuterConeAngle = FMath::Clamp(
            Component->OuterConeAngle * (float)UE_PI / 180.0f,
            ClampedInnerConeAngle + 0.001f,
            89.0f * (float)UE_PI / 180.0f + 0.001f);

        OuterConeAngle = ClampedOuterConeAngle;
        CosOuterCone = FMath::Cos(ClampedOuterConeAngle);
        SinOuterCone = FMath::Sin(ClampedOuterConeAngle);
        CosInnerCone = FMath::Cos(ClampedInnerConeAngle);
        InvCosConeDifference = 1.0f / (CosInnerCone - CosOuterCone);
        InvTanOuterCone = 1.0f / FMath::Tan(ClampedOuterConeAngle);
    }
};
```

**Key insight:** The `InvTanOuterCone` (1/tan(outerConeAngle)) is used directly as a scale factor in the projection matrix.

### 2.2 Shadow Initializer Setup

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\SpotLightComponent.cpp:119-138`

```cpp
virtual bool GetWholeSceneProjectedShadowInitializer(
    const FSceneViewFamily& ViewFamily,
    TArray<FWholeSceneProjectedShadowInitializer, TInlineAllocator<6> >& OutInitializers) const override
{
    FWholeSceneProjectedShadowInitializer& OutInitializer = OutInitializers.AddDefaulted_GetRef();

    // Light position is moved into light-space origin
    OutInitializer.PreShadowTranslation = -GetLightToWorld().GetOrigin();

    // Remove translation from world-to-light matrix
    OutInitializer.WorldToLight = GetWorldToLight().RemoveTranslation();

    // Projection scale = 1/tan(outerConeAngle)
    // This creates a perspective projection with FOV = 2*outerConeAngle
    OutInitializer.Scales = FVector2D(InvTanOuterCone, InvTanOuterCone);

    const FSphere AbsoluteBoundingSphere = FSpotLightSceneProxy::GetBoundingSphere();
    OutInitializer.SubjectBounds = FBoxSphereBounds(
        AbsoluteBoundingSphere.Center - GetOrigin(),
        FVector(AbsoluteBoundingSphere.W, AbsoluteBoundingSphere.W, AbsoluteBoundingSphere.W),
        AbsoluteBoundingSphere.W);

    OutInitializer.WAxis = FVector4(0, 0, 1, 0);  // Z is the view-forward axis
    OutInitializer.MinLightW = 0.1f;              // Near plane = 0.1 cm
    OutInitializer.MaxDistanceToCastInLightW = Radius;  // Far plane = light radius
    OutInitializer.bRayTracedDistanceField = /* ... */;
    return true;
}
```

**Projection matrix construction:**

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:1060-1062`

```cpp
ViewToClipInner = InnerScaleMatrix * FShadowProjectionMatrix(MinSubjectZ, MaxSubjectZ, Initializer.WAxis);
ViewToClipOuter = OuterScaleMatrix * FShadowProjectionMatrix(MinSubjectZ, MaxSubjectZ, Initializer.WAxis);
TranslatedWorldToClipInnerMatrix = FMatrix44f(TranslatedWorldToView * ViewToClipInner);
```

The `FShadowProjectionMatrix` is an orthographic projection in Z (clamped between MinSubjectZ and MaxSubjectZ), with the X-Y scales determined by `InvTanOuterCone`.

**Effective FOV:** If `InvTanOuterCone = 1/tan(θ)` and we scale projection matrix by this, the resulting **field-of-view is 2*θ** (the full cone angle).

### 2.3 View Matrix Construction

The view matrix is constructed from the spot light's local rotation:

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components/SpotLightComponent.cpp:123`

```cpp
OutInitializer.WorldToLight = GetWorldToLight().RemoveTranslation();
```

`GetWorldToLight()` is the inverse of the light's `FLightToWorld` transform (rotation + translation). For a spot light pointing down -Z with position at origin (after PreShadowTranslation), the view matrix aligns the light's forward direction with the -Z axis of the shadow view.

---

## 3. Point Light Shadow (Dual Paraboloid or One-Pass Cubemap)

### 3.1 One-Pass Cubemap Configuration

Point lights can use a **one-pass rendering** approach that renders all 6 cubemap faces in a single geometry pass (with instancing or layer duplication).

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:1080-1127`

```cpp
if (Initializer.bOnePassPointLightShadow)
{
    // 6 cube face directions (in light-space -X, +X, -Y, +Y, -Z, +Z)
    const static FVector CubeDirections[6] =
    {
        FVector(-1, 0, 0),
        FVector(1, 0, 0),
        FVector(0, -1, 0),
        FVector(0, 1, 0),
        FVector(0, 0, -1),
        FVector(0, 0, 1)
    };

    const static FVector UpVectors[6] =
    {
        FVector(0, 1, 0),   // -X face: up = +Y
        FVector(0, 1, 0),   // +X face: up = +Y
        FVector(0, 0, -1),  // -Y face: up = -Z
        FVector(0, 0, 1),   // +Y face: up = +Z
        FVector(0, 1, 0),   // -Z face: up = +Y
        FVector(0, 1, 0)    // +Z face: up = +Y
    };

    // Perspective matrix: 90-degree FOV (π/4 radians = half-angle)
    OnePassShadowFaceProjectionMatrix = FReversedZPerspectiveMatrix(
        PI / 4.0f,           // half-FOV = 45°, full FOV = 90°
        1,                   // aspect ratio = 1 (square face)
        1,                   // near plane = 1 cm
        1,                   // ignored (reversed-Z near/far are reversed)
        LightProxy.GetRadius() // far plane = light radius
    );

    const FVector LightPosition = Initializer.WorldToLight.GetOrigin();

    OnePassShadowViewMatrices.Empty(6);
    OnePassShadowViewProjectionMatrices.Empty(6);
    const FMatrix ScaleMatrix = FScaleMatrix(FVector(1, -1, 1));  // Flip Y (D3D convention)

    for (int32 FaceIndex = 0; FaceIndex < 6; FaceIndex++)
    {
        // Create view matrix looking from light down each direction
        const FMatrix WorldToLightMatrix = FLookFromMatrix(
            LightPosition,
            CubeDirections[FaceIndex],
            UpVectors[FaceIndex]) * ScaleMatrix;

        OnePassShadowViewMatrices.Add(WorldToLightMatrix);

        const FMatrix ShadowViewProjectionMatrix =
            WorldToLightMatrix * OnePassShadowFaceProjectionMatrix;
        OnePassShadowViewProjectionMatrices.Add(ShadowViewProjectionMatrix);

        // Culling frustum plane (from light along face direction)
        CasterOuterFrustum.Planes.Add(
            FPlane(CubeDirections[FaceIndex], LightProxy.GetRadius())
        );
    }
    CasterOuterFrustum.Init();
}
```

**Key parameters:**
- **Projection:** Perspective with **90° FOV** (half-angle = π/4 = 45°)
- **Aspect ratio:** 1.0 (square)
- **Near plane:** 1 cm
- **Far plane:** Light's attenuation radius
- **Reversed-Z:** Using `FReversedZPerspectiveMatrix` (depth 0 = far, 1 = near for better precision)
- **Y-flip:** Scale matrix flips Y for D3D/Vulkan coordinate convention

### 3.2 Cubemap Resolution for Point Lights

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:707-754`

Point light shadow maps use **separate cubemap surfaces** (not a single texture array). The resolution is quantized to discrete levels:

```cpp
/** Returns an index in the range [0, NumCubeShadowDepthSurfaces) given an input resolution. */
static int32 GetCubeShadowDepthZIndex(ERHIFeatureLevel::Type FeatureLevel, int32 ShadowResolution)
{
    FIntPoint ObjectShadowBufferResolution = GetShadowDepthTextureResolution(FeatureLevel);

    // Use a lower resolution because cubemaps use a lot of memory
    ObjectShadowBufferResolution.X /= 2;
    ObjectShadowBufferResolution.Y /= 2;

    const int32 SurfaceSizes[NumCubeShadowDepthSurfaces] =  // NumCubeShadowDepthSurfaces = 5
    {
        ObjectShadowBufferResolution.X,           // e.g., 2048 (4096/2)
        ObjectShadowBufferResolution.X / 2,       // e.g., 1024
        ObjectShadowBufferResolution.X / 4,       // e.g., 512
        ObjectShadowBufferResolution.X / 8,       // e.g., 256
        CVarMinShadowResolution.GetValueOnRenderThread()  // e.g., 32
    };

    for (int32 SearchIndex = 0; SearchIndex < NumCubeShadowDepthSurfaces; SearchIndex++)
    {
        if (ShadowResolution >= SurfaceSizes[SearchIndex])
        {
            return SearchIndex;
        }
    }

    check(0);
    return 0;
}

/** Returns the appropriate resolution for a given cube shadow index. */
static int32 GetCubeShadowDepthZResolution(ERHIFeatureLevel::Type FeatureLevel, int32 ShadowIndex)
{
    // ... returns SurfaceSizes[ShadowIndex]
}
```

**Resolution buckets:**
- **Level 0:** 50% of max atlas size (e.g., 2048 if atlas is 4096)
- **Level 1-3:** Halved at each step
- **Level 4:** CVarMinShadowResolution (default 32 per line 256)

Point lights use these pooled cubemap surfaces with a **texture array view** into a single 3D texture or separate cubemap resources.

---

## 4. Shadow Sampling in Deferred Lighting

### 4.1 Local Light Shadow Data Structure

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.h:525-527`

```cpp
float GetShaderDepthBias() const { return ShaderDepthBias; }
float GetShaderSlopeDepthBias() const { return ShaderSlopeDepthBias; }
float GetShaderMaxSlopeDepthBias() const { return ShaderMaxSlopeDepthBias; }
```

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp:1170`

Shader parameter binding for directional & spot lights:

```cpp
SetShaderValue(BatchedParameters, ProjectionDepthBias,
    FVector4f(
        ShadowInfo->GetShaderDepthBias(),
        ShadowInfo->GetShaderSlopeDepthBias(),
        ShadowInfo->GetShaderReceiverDepthBias(),
        ShadowInfo->MaxSubjectZ - ShadowInfo->MinSubjectZ));  // Z depth range
```

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp:1621`

Shader parameter binding for point light cubemaps:

```cpp
SetShaderValue(BatchedParameters, PointLightDepthBias,
    FVector3f(
        ShadowInfo->GetShaderDepthBias(),
        ShadowInfo->GetShaderSlopeDepthBias(),
        ShadowInfo->GetShaderMaxSlopeDepthBias()));
```

### 4.2 Shadow Projection (Spot Light Example)

**File:** `W:\UE_5.6_Source\Engine\Shaders\Private/DeferredLightingCommon.ush`

The lighting shader performs:

1. **Project world position into light's shadow view:**
   ```hlsl
   float4 ShadowClipPos = mul(float4(WorldPos, 1), LightViewProjMatrix);
   float2 ShadowAtlasUV = ShadowClipPos.xy / ShadowClipPos.w * 0.5 + 0.5;  // NDC to [0,1]
   ```

2. **Map atlas UV to tile within atlas:**
   ```hlsl
   // Apply shadow tile offset (X, Y, ScaleX, ScaleY)
   float2 TileUV = ShadowTileOffset.xy + ShadowAtlasUV * ShadowTileOffset.zw;
   ```

3. **Sample depth and compare:**
   ```hlsl
   float ShadowDepth = ShadowDepthTexture.Sample(ShadowSampler, TileUV).r;
   float ReceiverDepth = ShadowClipPos.z / ShadowClipPos.w;  // normalized depth

   float Shadow = ReceiverDepth < ShadowDepth + DepthBias ? 1.0 : 0.0;
   ```

---

## 5. Shadow Bias Configuration

### 5.1 Bias Values by Light Type

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp:83-152`

#### Point Lights:
```cpp
static TAutoConsoleVariable<float> CVarPointLightShadowDepthBias(
    TEXT("r.Shadow.PointLightDepthBias"),
    0.02f,
    TEXT("Depth bias that is applied in the depth pass for shadows from point lights. (0.03 avoids peter paning but has some shadow acne)"),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarPointLightShadowSlopeScaleDepthBias(
    TEXT("r.Shadow.PointLightSlopeScaleDepthBias"),
    3.0f,
    TEXT("Slope scale depth bias that is applied in the depth pass for shadows from point lights"),
    ECVF_RenderThreadSafe);
```

**Default values:**
- **Constant bias:** 0.02
- **Slope-scale bias:** 3.0

#### Spot Lights:
```cpp
static TAutoConsoleVariable<float> CVarSpotLightShadowDepthBias(
    TEXT("r.Shadow.SpotLightDepthBias"),
    3.0f,
    TEXT("Depth bias that is applied in the depth pass for whole-scene projected shadows from spot lights"),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarSpotLightShadowSlopeScaleDepthBias(
    TEXT("r.Shadow.SpotLightSlopeDepthBias"),
    3.0f,
    TEXT("Slope scale depth bias that is applied in the depth pass for whole-scene projected shadows from spot lights"),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarSpotLightShadowReceiverBias(
    TEXT("r.Shadow.SpotLightReceiverBias"),
    0.5f,
    TEXT("Receiver bias used by spotlights. Value between 0 and 1."),
    ECVF_RenderThreadSafe);
```

**Default values:**
- **Constant bias:** 3.0
- **Slope-scale bias:** 3.0
- **Receiver bias:** 0.5

#### Rect Lights:
```cpp
static TAutoConsoleVariable<float> CVarRectLightShadowDepthBias(
    TEXT("r.Shadow.RectLightDepthBias"),
    0.025f,
    TEXT("Depth bias that is applied in the depth pass for shadows from rect lights. (0.03 avoids peter paning but has some shadow acne)"),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRectLightShadowSlopeScaleDepthBias(
    TEXT("r.Shadow.RectLightSlopeScaleDepthBias"),
    2.5f,
    TEXT("Slope scale depth bias that is applied in the depth pass for shadows from rect lights"),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRectLightShadowReceiverBias(
    TEXT("r.Shadow.RectLightShadowReceiverBias"),
    0.3f,
    TEXT("Receiver bias used by rect light. Value between 0 and 1."),
    ECVF_RenderThreadSafe);
```

**Default values:**
- **Constant bias:** 0.025
- **Slope-scale bias:** 2.5
- **Receiver bias:** 0.3

### 5.2 Bias Computation Algorithm

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp:1811-1911`

```cpp
void FProjectedShadowInfo::UpdateShaderDepthBias()
{
    float DepthBias = 0;
    float SlopeScaleDepthBias = 1;

    if (IsWholeScenePointLightShadow())  // Point or Rect light
    {
        const bool bIsRectLight = LightSceneInfo->Proxy->GetLightType() == LightType_Rect;
        float DeptBiasConstant = 0;
        float SlopeDepthBiasConstant = 0;
        if (bIsRectLight)
        {
            DeptBiasConstant = CVarRectLightShadowDepthBias.GetValueOnRenderThread();
            SlopeDepthBiasConstant = CVarRectLightShadowSlopeScaleDepthBias.GetValueOnRenderThread();
        }
        else
        {
            DeptBiasConstant = CVarPointLightShadowDepthBias.GetValueOnRenderThread();
            SlopeDepthBiasConstant = CVarPointLightShadowSlopeScaleDepthBias.GetValueOnRenderThread();
        }

        // Compute bias relative to shadow map resolution
        DepthBias = DeptBiasConstant * 512.0f / FMath::Max(ResolutionX, ResolutionY);

        // Account for user-controlled shadow bias parameter (0 = default, 1 = 2x bias, etc.)
        // * 2.0f to be compatible with the system we had before ShadowBias
        DepthBias *= 2.0f * LightSceneInfo->Proxy->GetUserShadowBias();

        SlopeScaleDepthBias = SlopeDepthBiasConstant;
        SlopeScaleDepthBias *= LightSceneInfo->Proxy->GetUserShadowSlopeBias();
    }
    else if (/* spot light whole-scene or per-object */)
    {
        const float LightTypeDepthBias = bPerObjectOpaqueShadow ?
            CVarPerObjectSpotLightShadowDepthBias.GetValueOnRenderThread() :
            CVarSpotLightShadowDepthBias.GetValueOnRenderThread();

        DepthBias = LightTypeDepthBias * 512.0f / ((MaxSubjectZ - MinSubjectZ) * FMath::Max(ResolutionX, ResolutionY));
        DepthBias *= 2.0f * LightSceneInfo->Proxy->GetUserShadowBias();

        SlopeScaleDepthBias = bPerObjectOpaqueShadow ?
            CVarPerObjectSpotLightShadowSlopeScaleDepthBias.GetValueOnRenderThread() :
            CVarSpotLightShadowSlopeScaleDepthBias.GetValueOnRenderThread();
        SlopeScaleDepthBias *= LightSceneInfo->Proxy->GetUserShadowSlopeBias();
    }

    // Clamp to prevent excessive near-plane clipping
    DepthBias = FMath::Min(DepthBias, .1f);

    ShaderDepthBias = FMath::Max(DepthBias, 0.0f);
    ShaderSlopeDepthBias = FMath::Max(DepthBias * SlopeScaleDepthBias, 0.0f);
    ShaderMaxSlopeDepthBias = CVarShadowMaxSlopeScaleDepthBias.GetValueOnRenderThread();
}
```

### 5.3 Receiver-Side Bias

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp:1967-1984`

```cpp
float FProjectedShadowInfo::GetShaderReceiverDepthBias() const
{
    float ShadowReceiverBias = 1;
    {
        switch (GetLightSceneInfo().Proxy->GetLightType())
        {
        case LightType_Directional    : ShadowReceiverBias = CVarCSMShadowReceiverBias.GetValueOnRenderThread(); break;
        case LightType_Rect           : ShadowReceiverBias = CVarRectLightShadowReceiverBias.GetValueOnRenderThread(); break;
        case LightType_Spot           : ShadowReceiverBias = CVarSpotLightShadowReceiverBias.GetValueOnRenderThread(); break;
        case LightType_Point          : ShadowReceiverBias = GetShaderSlopeDepthBias(); break;
        }
    }

    // Return the min lerp value for depth biasing
    // 0 : max bias when NoL == 0
    // 1 : no bias
    return 1.0f - FMath::Clamp(ShadowReceiverBias, 0.0f, 1.0f);
}
```

**Interpretation:**
- Receiver bias is used as a lerp value in the lighting shader
- **Spot light receiver bias = 0.5:** Applies average bias (50% of configured amount)
- **Point light receiver bias:** Uses the slope depth bias directly

---

## 6. Shadow Filtering & PCF

### 6.1 PCF Pattern

UE5 uses **Poisson disk PCF sampling** for local light shadows.

**File:** `W:\UE_5.6_Source\Engine\Shaders\Private/ShadowFilteringCommon.ush`

The shader defines a **16-tap Poisson disk** pattern (UE5 standard):

```glsl
// Poisson disk sample offsets (in [-1, 1] normalized space, then scaled by kernel size)
const float2 PoissonSamples[16] =
{
    float2(-0.94201624, -0.39906216),
    float2(0.94558609, -0.76890725),
    // ... 14 more samples covering a circular pattern
};
```

**File:** `W:\UE_5.6_Source\Engine\Shaders\Private/DeferredLightingCommon.ush`

For local lights, the kernel size is typically:

```glsl
float KernelSize = ComputeTransitionSize(); // ShadowTransitionScale related
float ShadowSample = 0.0;

for (int i = 0; i < NUM_PCF_SAMPLES; i++)
{
    float2 SampleUV = TileUV + (PoissonSamples[i] * KernelSize);
    float SampleDepth = ShadowDepthTexture.Sample(ShadowSampler, SampleUV).r;
    ShadowSample += (ReceiverDepth < SampleDepth + DepthBias) ? 1.0 : 0.0;
}
ShadowSample /= NUM_PCF_SAMPLES;
```

### 6.2 Soft Shadow Transition

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp:1913-1955`

```cpp
float FProjectedShadowInfo::ComputeTransitionSize() const
{
    float TransitionSize = 1.0f;

    if (IsWholeScenePointLightShadow())
    {
        TransitionSize = bDirectionalLight ?
            (1.0f / CVarShadowTransitionScale.GetValueOnRenderThread()) :
            (1.0f / CVarSpotLightShadowTransitionScale.GetValueOnRenderThread());

        TransitionSize *= 2.0f * LightSceneInfo->Proxy->GetUserShadowBias();
    }
    // ...

    // Make sure that shadow soft transition size is greater than zero so 1/TransitionSize shader parameter won't be INF.
    const float MinTransitionSize = 0.00001f;
    return FMath::Max(TransitionSize, MinTransitionSize);
}
```

**Spot light transition scale:** `CVarSpotLightShadowTransitionScale = 60.0f` (line 142-146)

---

## 7. Data Structures Passed to GPU

### 7.1 Per-Shadow Constant Buffer

The `FProjectedShadowInfo` object passes these parameters to the GPU for shadow sampling:

| Parameter | Type | Source | Purpose |
|-----------|------|--------|---------|
| `PreShadowTranslation` | FVector | Line 309 | World-space translation to apply before light-space transform |
| `TranslatedWorldToView` | FMatrix | Line 314 | Light's view matrix (position already applied via PreShadowTranslation) |
| `ViewToClipInner` / `ViewToClipOuter` | FMatrix | Line 317-320 | Light's projection matrix (inner = no border, outer = with border) |
| `MaxSubjectZ` / `MinSubjectZ` | float | Line 337-338 | Z depth range in light-space [Min, Max] |
| `X`, `Y` | uint32 | Line 356-357 | Atlas tile position (in texels, including border) |
| `ResolutionX` / `ResolutionY` | uint32 | Line 363-364 | Tile resolution (excluding border) |
| `BorderSize` | uint32 | Line 367 | Border texel count (4 pixels) |
| `ShaderDepthBias` | float | Line 878 | Constant depth bias |
| `ShaderSlopeDepthBias` | float | Line 879 | Slope-scaled depth bias |
| `ShaderMaxSlopeDepthBias` | float | Line 880 | Max slope scale cap |

### 7.2 Cubemap-Specific Data (Point Lights)

For one-pass point light shadows:

| Parameter | Type | Count | Source |
|-----------|------|-------|--------|
| `OnePassShadowViewMatrices` | FMatrix | 6 | Line 448 |
| `OnePassShadowViewProjectionMatrices` | FMatrix | 6 | Line 445 |
| `OnePassShadowFaceProjectionMatrix` | FMatrix | 1 | Line 451 (90° FOV perspective) |

---

## 8. Key Algorithm Summary

### Spot Light Shadow Projection

1. **Light parameters:** Position, rotation, AttenuationRadius, OuterConeAngle
2. **Initializer construction:**
   - `PreShadowTranslation = -LightPosition`
   - `WorldToLight = LightRotationMatrix^-1` (without translation)
   - `Scales = (1/tan(OuterConeAngle), 1/tan(OuterConeAngle))`
   - `MinLightW = 0.1` cm (near plane)
   - `MaxDistanceToCastInLightW = AttenuationRadius` (far plane)

3. **Projection matrix:** Orthographic in Z, scaled by `1/tan(OuterConeAngle)` in X-Y
   - Results in a perspective frustum with FOV = 2×OuterConeAngle in light-space

4. **Depth bias:**
   - `DepthBias = CVarSpotLightShadowDepthBias * 512 / (MaxSubjectZ - MinSubjectZ) * MaxResolution * 2 * UserBias`
   - Default: `3.0 * 512 / ZRange / MaxRes * 2 * 1.0 = 3072 / ZRange / MaxRes`

5. **Sampling:**
   - Project world position: `ShadowClipPos = WorldPos * WorldToClip`
   - NDC to atlas UV: `TileUV = TileOffset + (ShadowClipPos.xy / ShadowClipPos.w * 0.5 + 0.5) * TileScale`
   - Compare: `Shadowed = ReceiverDepth < ShadowDepth + DepthBias`

### Point Light Cubemap Projection

1. **6 view matrices:** One for each cube face, using `FLookFromMatrix` with 90° field of view
2. **Single projection matrix:** `FReversedZPerspectiveMatrix(π/4, 1, 1, 1, Radius)`
3. **Face direction mapping:** Each face covers ±X, ±Y, or ±Z in light-space
4. **Sampling:** Normalize (WorldPos - LightPos) to get cube face direction, then sample

---

## 9. Adaptation Notes for UzEngine

### What to Preserve
- **Bias scaling by resolution:** `DepthBias * 512 / MaxResolution` accounts for perspective distortion
- **Receiver-side bias:** Lerp based on surface normal orientation (prevents self-shadowing artifacts)
- **Poisson disk PCF:** 16-tap pattern with transition-scale kernel
- **Slope-scaled bias:** Automatically prevents shadow acne on surfaces perpendicular to light direction

### What to Adapt
- **Texture packing:** UzEngine can use a simpler linear allocator instead of 2D bin-packing
- **Resolution quantization:** Instead of 5 discrete cubemap levels, UzEngine can use dynamic resolution
- **Bias defaults:** Tune CVars based on UzEngine's specific depth precision and attenuation curves
- **Format:** Consider R16F (half-precision) for reduced memory if precision is acceptable

---

## References

- **ShadowSetup.cpp:** Lines 960-1133 (SetupWholeSceneProjection), 4109-4419 (CreateWholeSceneProjectedShadow)
- **ShadowRendering.cpp:** Lines 1811-1911 (UpdateShaderDepthBias), 273-282 (GetOnePassPointShadowProjectionParameters)
- **SpotLightComponent.cpp:** Lines 22-183 (FSpotLightSceneProxy class)
- **ShadowRendering.h:** Lines 281-473 (FProjectedShadowInfo structure)

---

**Document Generated:** 2026-03-18
**Engine Version:** Unreal Engine 5.6.0
**Analysis Scope:** Local light shadow atlas, projection setup, bias computation, GPU binding
