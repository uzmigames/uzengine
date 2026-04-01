<!-- doc-meta
id: point-light-shadow-data-flow
type: analysis
author: unreal-architect
created: 2026-03-30
status: complete
scope: Complete data flow of point light shadows from component to shader
tags: [shadows, data-flow, cbuffer, shader, component-to-gpu]
related: [point-spot-light-shadows, local-light-atlas-allocation]
-->

# Point Light Shadow Data Flow: Component → Renderer → Shader

## Overview

This document traces the **complete data pipeline** for point light shadows from the ECS component through the renderer to GPU shader execution. It provides exact file paths, function names, and variable names for UzEngine replication.

---

## 1. Component Layer

### 1.1 PointLightComponent Properties

**File**: `Engine/Classes/Components/PointLightComponent.h`

```cpp
class UPointLightComponent : public ULocalLightComponent {
public:
    // ─── Light Parameters ───
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light",
        meta=(ClampMin="0.0", UIMax="10000.0"))
    float Intensity = 1000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light",
        meta=(ClampMin="0.0", UIMax="10000.0"))
    float AttenuationRadius = 1000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light",
        meta=(ClampMin="0.0", UIMax="500.0"))
    float SourceRadius = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light",
        meta=(ClampMin="0.0"))
    float SoftSourceRadius = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light",
        meta=(ClampMin="1.0", UIMax="32.0"))
    float LightFalloffExponent = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light",
        meta=(DisplayName="Use Inverse Squared Falloff"))
    uint32 bUseInverseSquaredFalloff : 1 = 1;

    // ─── Shadow Parameters ───
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shadow",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float ShadowBias = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shadow",
        meta=(ClampMin="0.0"))
    float ShadowSlopeScaleDepthBias = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shadow",
        meta=(ClampMin="0.0"))
    float ShadowResolutionScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shadow")
    uint32 bCastShadows : 1 = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shadow")
    uint32 bCastDynamicShadows : 1 = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shadow")
    uint32 bCastStaticShadows : 1 = 1;

    // ─── Rendering ───
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering")
    FLinearColor LightColor = FLinearColor(1, 1, 1, 1);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float IndirectLightingIntensity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
        meta=(ClampMin="0.0", ClampMax="10.0"))
    float VolumetricScatteringIntensity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
        meta=(ClampMin="1.0", UIMax="32.0"))
    float IESIntensityScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering")
    class UTexture* IESTexture = nullptr;

    // Setters (trigger proxy updates)
    ENGINE_API void SetIntensity(float NewIntensity);
    ENGINE_API void SetSourceRadius(float NewRadius);
    ENGINE_API void SetAttenuationRadius(float NewRadius);
};
```

---

## 2. Scene Proxy Layer

### 2.1 Component → Proxy (Game Thread)

**File**: `Engine/Private/PointLightSceneProxy.h`

```cpp
class FPointLightSceneProxy : public FLocalLightSceneProxy {
public:
    // Cached component properties
    float FalloffExponent;
    float SourceRadius;
    float SoftSourceRadius;
    float SourceLength;
    uint32 bInverseSquared : 1;

    // Constructor (called when component is added to scene)
    FPointLightSceneProxy(const UPointLightComponent* Component)
        : FLocalLightSceneProxy(Component)
        , FalloffExponent(Component->LightFalloffExponent)
        , SourceRadius(Component->SourceRadius)
        , SoftSourceRadius(Component->SoftSourceRadius)
        , SourceLength(Component->SourceLength)
        , bInverseSquared(Component->bUseInverseSquaredFalloff)
    {
    }
};
```

**Parent class** `FLocalLightSceneProxy`:

```cpp
class FLocalLightSceneProxy : public FLightSceneProxy {
public:
    float Radius;           // AttenuationRadius from component
    float InvRadius;        // 1.0 / Radius
    float MaxDrawDistance;
    float FadeRange;
    // ... other fields

    // Key methods
    virtual float GetRadius() const override { return Radius; }
    virtual bool IsInverseSquared() const override { return bInverseSquared; }
    virtual void GetLightShaderParameters(FLightRenderParameters& OutParams) const;
};
```

**Parent class** `FLightSceneProxy`:

```cpp
class FLightSceneProxy {
public:
    FVector Position;       // World position
    FVector Direction;      // Dominant direction (for area lights)
    FLinearColor Color;     // RGB + brightness
    float Intensity;        // Total luminous flux
    // ... 50+ more fields
};
```

---

## 3. Lighting Data & Shader Constants

### 3.1 FLightRenderParameters (Per-Light Constants)

**File**: `Engine/Public/LightData.ush` (HLSL include)

```glsl
// Constant buffer for a single light (shared for CPU & GPU)
struct FLightRenderParameters {
    float3 Position;        // World position
    float InvRadius;        // 1.0 / light radius
    float3 Color;           // RGB color
    float FalloffExponent;  // For non-inverse-squared lights
    float3 Direction;       // Forward direction (for spot/rect lights)
    float SpecularScale;    // Multiplier for specular highlights
    float3 Tangent;         // Tangent (for rect lights)
    float SoftSourceRadius; // Soft shadow size
    float3 Bitangent;       // Bitangent (for rect lights)
    float SourceRadius;     // Hard shadow size
    float2 DistanceFadeMAD;  // Distance fade parameters
    float RectLightBarnDoorAngle;
    float RectLightBarnDoorLength;
    float ShadowMapChannelMask;  // Static shadow channel
    float ShadowAmount;     // Multiplier for shadow intensity
    float ContactShadowLength;  // Ray-cast shadow distance
    float ContactShadowLengthInWS;  // Is contact shadow in world-space?
    // ... 40+ more fields
};
```

### 3.2 Data Flow: Component → FLightRenderParameters

```
Component.Intensity ──┐
Component.Color ──────┼──> GetLightShaderParameters()
Component.Position ───┼──> FLightRenderParameters
Component.Radius ─────┘
```

**Conversion function**:

```cpp
// PointLightSceneProxy.cpp
void FPointLightSceneProxy::GetLightShaderParameters(
    FLightRenderParameters& OutParams) const
{
    OutParams.Position = GetOrigin();
    OutParams.Color = GetColor().GetClipped().ToFColor(true).ReinterpretAsLinear();
    OutParams.InvRadius = 1.0f / Radius;
    OutParams.Intensity = Intensity;
    OutParams.SourceRadius = SourceRadius;
    OutParams.SoftSourceRadius = SoftSourceRadius;
    OutParams.FalloffExponent = FalloffExponent;
    // ... more fields
}
```

---

## 4. Shadow Setup & Projection Data

### 4.1 FProjectedShadowInfo Creation

**File**: `Engine/Private/ShadowSetup.cpp` (line 4120-4230)

```cpp
// Per-frame call for each light that casts shadows
void FSceneRenderer::CreateWholeSceneProjectedShadow(
    FLightSceneInfo* LightSceneInfo) const
{
    // 1. Compute desired resolution from screen coverage
    const float ScreenRadius = LightSceneInfo->Proxy->GetEffectiveScreenRadius(...);
    float UnclampedResolution = ScreenRadius * 1.5f;  // CVarShadowTexelsPerPixelPointlight
    float ClampedResolution = FMath::Min(UnclampedResolution, MaxShadowResolution);

    // 2. Round to power-of-two
    int32 SizeX = RoundToPowerOfTwo(ClampedResolution);
    int32 SizeY = SizeX;

    // 3. Create FWholeSceneProjectedShadowInitializer
    FWholeSceneProjectedShadowInitializer Initializer;
    Initializer.bOnePassPointLightShadow = true;  // ← POINT LIGHT FLAG
    Initializer.bRayTracedDistanceField = false;
    Initializer.bTranslucentShadow = false;
    Initializer.CascadeSettings.ShadowSplitIndex = 0;
    Initializer.PreShadowTranslation = FVector::ZeroVector;

    // 4. Create FProjectedShadowInfo
    FProjectedShadowInfo* ShadowInfo = new FProjectedShadowInfo();
    ShadowInfo->SetupWholeSceneProjection(
        LightSceneInfo,
        nullptr,  // No dependent view for whole-scene
        Initializer,
        FrameNumber
    );
}
```

### 4.2 FProjectedShadowInfo: One-Pass Setup

**File**: `Engine/Private/ShadowSetup.cpp` (line 1070-1127)

```cpp
// Inside SetupWholeSceneProjection(...) for one-pass point lights
if (Initializer.bOnePassPointLightShadow) {
    // ─── Step 1: Create perspective projection for all 6 faces ───
    const float FOV = PI / 4.0f;                    // 90 degrees
    const float AspectRatio = 1.0f;                 // Square
    const float NearPlane = 1.0f;                   // 1 cm (hardcoded)
    const float FarPlane = LightProxy.GetRadius(); // Attenuation radius

    OnePassShadowFaceProjectionMatrix =
        FReversedZPerspectiveMatrix(FOV, AspectRatio, NearPlane, FarPlane);

    // ─── Step 2: Create view matrices for 6 cube faces ───
    const FVector CubeDirections[6] = {
        FVector(-1, 0, 0),  // -X
        FVector( 1, 0, 0),  // +X
        FVector( 0,-1, 0),  // -Y
        FVector( 0, 1, 0),  // +Y
        FVector( 0, 0,-1),  // -Z
        FVector( 0, 0, 1),  // +Z
    };

    const FVector UpVectors[6] = {
        FVector(0, 1, 0),   // -X face: Y up
        FVector(0, 1, 0),   // +X face: Y up
        FVector(0, 0,-1),   // -Y face: -Z up
        FVector(0, 0, 1),   // +Y face: +Z up
        FVector(0, 1, 0),   // -Z face: Y up
        FVector(0, 1, 0),   // +Z face: Y up
    };

    const FVector LightPosition = Initializer.WorldToLight.GetOrigin();
    const FMatrix ScaleMatrix = FScaleMatrix(FVector(1, -1, 1));

    for (int32 FaceIndex = 0; FaceIndex < 6; FaceIndex++) {
        // View matrix for this face
        FMatrix WorldToLight =
            FLookFromMatrix(LightPosition, CubeDirections[FaceIndex], UpVectors[FaceIndex])
            * ScaleMatrix;

        OnePassShadowViewMatrices.Add(WorldToLight);

        // Combined view-projection
        FMatrix ShadowViewProjection = WorldToLight * OnePassShadowFaceProjectionMatrix;
        OnePassShadowViewProjectionMatrices.Add(ShadowViewProjection);

        // Culling frustum
        CasterOuterFrustum.Planes.Add(FPlane(CubeDirections[FaceIndex], FarPlane));
    }
    CasterOuterFrustum.Init();

    // ─── Step 3: Update depth bias ───
    UpdateShaderDepthBias();
}
```

### 4.3 Depth Bias Computation

**File**: `Engine/Private/ShadowRendering.cpp` (lines 83-93)

```cpp
// Static CVars (set once, read per-frame)
static float CVarPointLightShadowDepthBias = 0.02f;
static float CVarPointLightShadowSlopeScaleDepthBias = 3.0f;

// Inside FProjectedShadowInfo::UpdateShaderDepthBias()
void FProjectedShadowInfo::UpdateShaderDepthBias() {
    // Compute bias based on light type
    if (bOnePassPointLightShadow) {
        // Per-face depth bias (same for all 6 faces)
        ShaderDepthBias = CVarPointLightShadowDepthBias;  // 0.02
        ShaderSlopeDepthBias = CVarPointLightShadowSlopeScaleDepthBias;  // 3.0

        // Per-light override
        if (LightSceneInfo->Proxy->GetShadowBias() > 0.0f) {
            ShaderDepthBias *= LightSceneInfo->Proxy->GetShadowBias();
        }
        if (LightSceneInfo->Proxy->GetShadowSlopeScaleDepthBias() > 0.0f) {
            ShaderSlopeDepthBias = LightSceneInfo->Proxy->GetShadowSlopeScaleDepthBias();
        }
    }
}
```

---

## 5. Shadow Rendering (Depth Pass)

### 5.1 Depth Pass PSO Configuration

**File**: `Engine/Private/ShadowRendering.cpp` (lines 1504-1563)

```cpp
void FProjectedShadowInfo::RenderOnePassPointLightProjection(...) {
    // ─── Determine culling based on camera position ───
    const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();
    const bool bCameraInsideLight =
        ((View.ViewMatrices.GetViewOrigin() - LightBounds.Center).SizeSquared() <
         FMath::Square(LightBounds.W * 1.05f + View.NearClippingDistance * 2.0f));

    // ─── Setup PSO ───
    FGraphicsPipelineStateInitializer GraphicsPSOInit;
    RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

    GraphicsPSOInit.PrimitiveType = PT_TriangleList;
    GraphicsPSOInit.BlendState = NoBlendState;  // No color write

    if (bCameraInsideLight) {
        // Inside: No depth test, render backfaces
        GraphicsPSOInit.DepthStencilState =
            TStaticDepthStencilState<false, CF_Always>::GetRHI();
        GraphicsPSOInit.RasterizerState =
            TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
    } else {
        // Outside: DepthNearOrEqual (early-Z), render frontfaces
        GraphicsPSOInit.DepthStencilState =
            TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
        GraphicsPSOInit.RasterizerState =
            TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
    }

    // ─── Setup viewport (full screen) ───
    RHICmdList.SetViewport(
        View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f,
        View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f
    );

    // ─── Bounding geometry (screen-space sphere) ───
    StencilingGeometry::DrawSphere(RHICmdList, View.GetStereoPassInstanceFactor());
}
```

### 5.2 Depth Pass Shader Binding

**File**: `Engine/Private/ShadowRendering.cpp` (lines 1489-1502)

```cpp
template<uint32 Quality, bool bUseTransmission, bool bUseSubPixel>
static void SetPointLightShaderTempl(
    FRHICommandList& RHICmdList,
    FGraphicsPipelineStateInitializer& GraphicsPSOInit,
    int32 ViewIndex,
    const FViewInfo& View,
    const FProjectedShadowInfo* ShadowInfo) {

    // Select shader based on quality level
    TShaderMapRef<FShadowVolumeBoundProjectionVS> VertexShader(View.ShaderMap);
    TShaderMapRef<TOnePassPointShadowProjectionPS<Quality, bUseTransmission, bUseSubPixel>>
        PixelShader(View.ShaderMap);

    GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI =
        GetVertexDeclarationFVector4();
    GraphicsPSOInit.BoundShaderState.VertexShaderRHI =
        VertexShader.GetVertexShader();
    GraphicsPSOInit.BoundShaderState.PixelShaderRHI =
        PixelShader.GetPixelShader();

    SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

    // ─── Bind shader parameters ───
    SetShaderParametersLegacyVS(RHICmdList, VertexShader, View, ShadowInfo,
        EShadowProjectionVertexShaderFlags::None);
    SetShaderParametersLegacyPS(RHICmdList, PixelShader, ViewIndex, View, ShadowInfo);
}
```

---

## 6. Shader Layer

### 6.1 Vertex Shader (FShadowVolumeBoundProjectionVS)

**File**: `Engine/Shaders/Private/ShadowVolumeBoundProjectionVS.usf`

```hlsl
// Input
cbuffer ShadowDepthPass : register(b0) {
    float4x4 WorldToClip;       // Combined view-projection matrix
    float3 LightPosition;        // Light center (translated world coords)
    float Padding;
};

struct FShadowVSInput {
    float4 Position : POSITION;  // Sphere vertex position
};

struct FShadowVSOutput {
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
};

FShadowVSOutput main(FShadowVSInput Input) {
    FShadowVSOutput Output;

    // Transform sphere to world space
    float3 WorldPos = Input.Position.xyz + LightPosition;

    // Project to clip space
    Output.Position = mul(float4(WorldPos, 1), WorldToClip);
    Output.WorldPosition = WorldPos;

    return Output;
}
```

### 6.2 Pixel Shader (TOnePassPointShadowProjectionPS)

**File**: `Engine/Shaders/Private/OnePassPointShadowProjection.usf`

```hlsl
// Constant buffer parameters (bound in SetShaderParametersLegacyPS)
cbuffer ShadowDepthPass : register(b0) {
    float3 PointLightDepthBias;        // (constant, slope, max-slope)
    float3 PointLightProjParameters;   // (inverse params)
    float4x4 OnePassShadowViewProjectionMatrices[6];  // Per-face VP matrices
    float4x4 OnePassShadowViewMatrices[6];            // Per-face V matrices
    float4x4 OnePassShadowFaceProjectionMatrix;       // Shared projection
    float3 LightPosition;
    float LightRadius;
};

struct FShadowPSInput {
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
};

// Output depth (or color if translucent)
float main(FShadowPSInput Input) : SV_Target {
    // Compute light-to-pixel direction
    float3 LightToPixel = Input.WorldPosition - LightPosition;

    // Select which cube face this pixel belongs to
    float3 AbsLightToPixel = abs(LightToPixel);
    float MaxComponent = max(AbsLightToPixel.x, max(AbsLightToPixel.y, AbsLightToPixel.z));

    int FaceIndex = 0;
    if (AbsLightToPixel.x == MaxComponent) {
        FaceIndex = (LightToPixel.x > 0) ? 1 : 0;  // +X or -X
    } else if (AbsLightToPixel.y == MaxComponent) {
        FaceIndex = (LightToPixel.y > 0) ? 3 : 2;  // +Y or -Y
    } else {
        FaceIndex = (LightToPixel.z > 0) ? 5 : 4;  // +Z or -Z
    }

    // Transform to light space for this face
    float4 ShadowPos = mul(float4(Input.WorldPosition, 1.0),
                            OnePassShadowViewProjectionMatrices[FaceIndex]);

    // Convert to depth value (reversed-Z convention)
    float Depth = ShadowPos.z / ShadowPos.w;

    // Apply depth bias
    float3 Normal = normalize(LightToPixel);
    float BiasAmount = PointLightDepthBias.x +
                       PointLightDepthBias.y * abs(ddx(Normal.z)) +
                       PointLightDepthBias.y * abs(ddy(Normal.z));

    Depth -= BiasAmount * ShadowPos.w;

    // Clamp to valid depth range [0, 1] for reversed-Z
    Depth = saturate(Depth);

    return Depth;
}
```

---

## 7. Shadow Sampling (Lighting Pass)

### 7.1 Deferred Lighting Pixel Shader Integration

**File**: `Engine/Shaders/Private/DeferredLightingCommon.ush`

```hlsl
// Forward declaration
float SamplePointLightShadow(
    float3 PixelWorldPos,
    float3 LightPosition,
    float LightRadius,
    Texture2D ShadowTexture,
    SamplerState ShadowSampler,
    int QualityLevel);

// Inside GetShadowTermsBase()
void GetShadowTermsBase(
    float SceneDepth,
    half4 PrecomputedShadowFactors,
    FDeferredLightData LightData,
    half4 LightAttenuation,
    inout FShadowTerms OutShadow) {

    if (LightData.bRadialLight) {  // Point/spot lights
        // Sample point light shadow
        float ShadowValue = SamplePointLightShadow(
            PixelWorldPos,
            LightData.LightPosition,
            LightData.Radius,
            LightData.ShadowTexture,
            LightData.ShadowSampler,
            LightData.ShadowQuality);

        // Combine with static shadows
        OutShadow.SurfaceShadow = ShadowValue * StaticShadowing;
        OutShadow.TransmissionShadow = ShadowValue * StaticShadowing;
    }
}
```

### 7.2 Point Light Shadow Sampling (Quality Levels)

```hlsl
// Quality 1: Single tap (fastest)
float SamplePointLightShadow_Q1(float3 LightToPixel, Texture2D ShadowTex, ...) {
    return ShadowTex.Sample(ShadowSampler, CubemapUV).r > Depth ? 1.0 : 0.0;
}

// Quality 3: 9-tap PCF (standard)
float SamplePointLightShadow_Q3(float3 LightToPixel, Texture2D ShadowTex, ...) {
    float Shadow = 0.0;
    [unroll]
    for (int i = 0; i < 9; i++) {
        float2 UV = CubemapUV + PoissonDisk[i] * TexelSize;
        float ShadowDepth = ShadowTex.Sample(ShadowSampler, UV).r;
        Shadow += (ShadowDepth > Depth) ? 1.0 : 0.0;
    }
    return Shadow / 9.0;
}
```

---

## 8. Data Flow Table

| Stage | File | Variable | Type | Value |
|---|---|---|---|---|
| **1. Component** | PointLightComponent.h | `Intensity` | float | 1000.0 |
| | PointLightComponent.h | `AttenuationRadius` | float | 1000.0 |
| | PointLightComponent.h | `SourceRadius` | float | 0.0 |
| | PointLightComponent.h | `bUseInverseSquaredFalloff` | bool | true |
| **2. Proxy** | PointLightSceneProxy.h | `FalloffExponent` | float | 2.0 |
| | LocalLightSceneProxy.h | `Radius` | float | AttenuationRadius |
| | LocalLightSceneProxy.h | `InvRadius` | float | 1.0 / Radius |
| **3. Light Params** | LightData.ush | `FLightRenderParameters::Position` | float3 | Component.Position |
| | LightData.ush | `FLightRenderParameters::InvRadius` | float | 1.0 / Radius |
| **4. Shadow Setup** | ShadowSetup.cpp | `OnePassShadowFaceProjectionMatrix` | float4x4 | FReversedZ(90°, 1.0, 1.0, Radius) |
| | ShadowSetup.cpp | `OnePassShadowViewMatrices[6]` | float4x4[] | FLookFrom(CubeDirections, UpVectors) |
| | ShadowSetup.cpp | `ShaderDepthBias` | float | 0.02 * ShadowBias |
| | ShadowSetup.cpp | `ShaderSlopeDepthBias` | float | 3.0 |
| **5. Render** | ShadowRendering.cpp | `RenderTargets.DepthTarget` | Texture2D | ResolutionX × ResolutionY |
| | ShadowRendering.cpp | `DepthClear` | float | 1.0 (reversed-Z far) |
| **6. Shader Bind** | ShadowRendering.cpp | `PointLightDepthBias` | float3 | (constant, slope, max) |
| | ShadowRendering.cpp | `PointLightProjParameters` | float3 | Inverse projection |
| **7. Depth Pass VS** | ShadowVolumeBoundProjectionVS.usf | `WorldToClip[FaceIndex]` | float4x4 | VP matrices |
| **8. Depth Pass PS** | OnePassPointShadowProjection.usf | `Depth` | float | z/w in [0, 1] |
| **9. Lighting Pass** | DeferredLightingCommon.ush | `ShadowValue` | float | Sampled depth comparison |

---

## 9. Constant Buffer Layouts

### 9.1 Shadow Depth Pass CB (Conceptual)

```cpp
// Packed into GPU constant buffer
struct ShadowDepthPassCB {
    float4x4 ViewProjection;              // 64 bytes (16 float4s)
    float3   PointLightDepthBias;         // 12 bytes
    float    _pad1;                       // 4 bytes
    float3   PointLightProjParameters;    // 12 bytes
    float    _pad2;                       // 4 bytes
    float4x4 OnePassViewProj[6];          // 384 bytes (6 * 64)
    float4x4 OnePassView[6];              // 384 bytes
    float4x4 OnePassProjection;           // 64 bytes
    float3   LightPosition;               // 12 bytes
    float    LightRadius;                 // 4 bytes
    // ────────────────────────────────
    // Total: ~952 bytes (should be rounded to 16-byte boundaries)
};
```

### 9.2 Lighting Pass CB (Deferred Lighting)

```hlsl
cbuffer LightDataCB : register(b1) {
    float3   Position;                   // Light world position
    float    InvRadius;                  // 1.0 / radius
    float3   Color;                      // RGB
    float    Intensity;
    float3   Direction;                  // Dominant direction
    float    SpecularScale;
    // ... more fields (40+ total)
};
```

---

## 10. Key Integration Points for UzEngine

### 10.1 Component → ECS Mapping

```cpp
// UzEngine equivalent
struct PointLightComponent {
    float intensity = 1000.0f;
    float attenuation_radius = 1000.0f;
    float source_radius = 0.0f;
    float soft_source_radius = 0.0f;
    float light_falloff_exponent = 2.0f;
    bool use_inverse_squared_falloff = true;

    float shadow_bias = 0.0f;
    float shadow_slope_scale_depth_bias = 0.0f;
    float shadow_resolution_scale = 1.0f;
    bool cast_shadows = true;
    bool cast_dynamic_shadows = true;
    bool cast_static_shadows = true;

    LinearColor light_color{1, 1, 1, 1};
    float indirect_lighting_intensity = 1.0f;
    float volumetric_scattering_intensity = 1.0f;
};
```

### 10.2 Renderer Integration Points

1. **Per-frame shadow setup** (in render pass init):
   - Compute screen radius
   - Determine resolution (power-of-2)
   - Create FProjectedShadowInfo with cube matrices

2. **Depth pass rendering**:
   - Set full-screen viewport
   - Bind cube view-projection matrices (6)
   - Render sphere bounding geometry
   - Output to depth texture

3. **Lighting pass sampling**:
   - Trace LightToPixel direction
   - Select cube face
   - Sample depth texture
   - Apply PCF filtering based on quality

### 10.3 Shader Files Needed

```
shaders/
├── include/
│   ├── shadow_common.hlsli          (shared: depth comparison, bias)
│   ├── point_light_shadow.hlsli     (cubemap sampling)
│   └── depth_bias.hlsli             (bias application)
├── depth_pass/
│   ├── point_light_depth_vs.hlsl    (sphere projection)
│   └── point_light_depth_ps.hlsl    (depth output)
└── lighting_pass/
    └── point_light_sampling.hlsl    (PCF + shadow value)
```

---

## 11. Summary

**Data Flow Path:**
```
Component Properties
    ↓
Scene Proxy (cached)
    ↓
Light Render Parameters
    ↓
Shadow Setup (per-frame)
    ├─ Projection matrices (6 cube faces)
    ├─ Depth bias
    └─ Resolution computation
    ↓
Depth Pass Rendering
    ├─ Sphere geometry binding
    ├─ Viewport setup (full screen)
    └─ Depth texture output
    ↓
Lighting Pass Sampling
    ├─ Direction computation
    ├─ PCF filtering
    └─ Shadow value output
    ↓
Final Shaded Pixel
```

