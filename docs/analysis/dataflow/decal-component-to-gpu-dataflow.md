<!-- doc-meta
type: data-flow
scope: decal-rendering-pipeline
references: ue5-deferred-decal-architecture.md
-->

# Data Flow: Decal Component → GPU Rendering

## Overview

This document traces every field and transform as decal data flows from the game thread component through the render thread to GPU execution.

---

## Stage 1: Game Thread — UDecalComponent

### Source
```cpp
// W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Components\DecalComponent.h
class UDecalComponent : public UDecalComponentBase
{
    // Transform (inherited from USceneComponent)
    FVector RelativeLocation;
    FRotator RelativeRotation;
    FVector RelativeScale3D;
    
    // Decal-specific
    UMaterialInterface* DecalMaterial;
    FVector DecalSize = FVector(256, 256, 256);  // OBB extents in cm
    float FadeDuration = 0.0f;
    float FadeInDuration = 0.0f;
    float FadeStartDelay = 0.0f;
    float FadeInStartDelay = 0.0f;
    FLinearColor DecalColor = FLinearColor::White;
    int32 SortOrder = 0;
    bool bProjectOnBackfaces = false;
    bool bProjectOnTranslucentMaterials = false;
};
```

**Key fields**:
- `RelativeLocation, RelativeRotation, RelativeScale3D` → Combined into `ComponentTrans`
- `DecalSize` → Multiplied into `ComponentTrans` as scale
- `DecalMaterial` → Determines shader evaluation
- `DecalColor` → Tint multiplied in pixel shader

---

## Stage 2: Render Thread Snapshot — FDeferredDecalProxy

### Creation
```cpp
// File: W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\SceneProxies\DeferredDecalProxy.h
class FDeferredDecalProxy
{
public:
    const USceneComponent* Component;
    UMaterialInterface* DecalMaterial;
    
    // ← CRITICAL TRANSFORM (includes DecalSize)
    FTransform ComponentTrans;
    
    // Bounds for culling
    FBoxSphereBounds Bounds;
    
    // Lifecycle
    int32 SortOrder;
    float InvFadeDuration;      // 1.0 / FadeDuration
    float InvFadeInDuration;    // 1.0 / FadeInDuration
    float FadeStartDelayNormalized;
    float FadeInStartDelayNormalized;
    
    // Appearance
    float FadeScreenSize;
    FLinearColor DecalColor;
};
```

### Derived Values (computed on render thread)
```cpp
// File: DecalRenderingShared.cpp:199-216
const FMatrix DecalToWorldMatrix = VisibleDecal.ComponentTrans.ToMatrixWithScale();
const FMatrix WorldToDecalMatrix = VisibleDecal.ComponentTrans.ToInverseMatrixWithScale();

const FDFVector3 AbsoluteOrigin(DecalToWorldMatrix.GetOrigin());
const FVector3f PositionHigh = AbsoluteOrigin.High;  // LWC high bits

const FMatrix44f RelativeDecalToWorldMatrix = 
    FDFMatrix::MakeToRelativeWorldMatrix(PositionHigh, DecalToWorldMatrix).M;

const FVector3f OrientationVector = 
    (FVector3f)VisibleDecal.ComponentTrans.GetUnitAxis(EAxis::X);  // Decal X-axis
```

**Data dependency**:
```
UDecalComponent.RelativeTransform
  + UDecalComponent.DecalSize (as scale)
    ↓
FDeferredDecalProxy.ComponentTrans
  ↓ [Render Thread]
DecalToWorldMatrix (4×4)
WorldToDecalMatrix (4×4)
DecalPositionHigh (LWC)
RelativeDecalToWorldMatrix (4×4)
OrientationVector (FVector3f)
```

---

## Stage 3: Visibility Culling — FVisibleDecal

### Filtering
```cpp
// File: DecalRenderingShared.cpp:29-47
FVisibleDecal::FVisibleDecal(const FDeferredDecalProxy& InDecalProxy, ...)
    : MaterialProxy(InDecalProxy.DecalMaterial->GetRenderProxy())
    , Component((uintptr_t)InDecalProxy.Component)
    , SortOrder(InDecalProxy.SortOrder)
    , ConservativeRadius(InConservativeRadius)  // For sphere culling
    , FadeAlpha(InFadeAlpha)                    // Screen-size fade
    , InvFadeDuration(InDecalProxy.InvFadeDuration)
    , InvFadeInDuration(InDecalProxy.InvFadeInDuration)
    , FadeStartDelayNormalized(InDecalProxy.FadeStartDelayNormalized)
    , FadeInStartDelayNormalized(InDecalProxy.FadeInStartDelayNormalized)
    , DecalColor(InDecalProxy.DecalColor)
    , ComponentTrans(InDecalProxy.ComponentTrans)
    , BoxBounds(InDecalProxy.GetBounds().GetBox())
{
    FMaterial const& MaterialResource = 
        MaterialProxy->GetIncompleteMaterialWithFallback(FeatureLevel);
    BlendDesc = DecalRendering::ComputeDecalBlendDesc(ShaderPlatform, MaterialResource);
}
```

**Key output**: `BlendDesc` determines PSO (blend mode, render target count, etc.)

---

## Stage 4: Matrix Computation — Shader Parameters

### SvPositionToDecal Matrix

**Computation** (DecalRenderingShared.cpp:217-241):

```cpp
// 1. Screen-to-NDC transform (handles viewport scaling)
float Mx = 2.0f * InvViewSize.X;                    // 2.0 / ViewportWidth
float My = -2.0f * InvViewSize.Y;                   // -2.0 / ViewportHeight
float Ax = -1.0f - 2.0f * View.ViewRect.Min.X * InvViewSize.X;
float Ay = 1.0f + 2.0f * View.ViewRect.Min.Y * InvViewSize.Y;

const FMatrix SvPositionToDecalBase(
    FPlane(Mx, 0, 0, 0),
    FPlane(0, My, 0, 0),
    FPlane(0, 0, 1, 0),
    FPlane(Ax, Ay, 0, 1)
);

// 2. Screen → World (NDC to world space)
FMatrix InvViewProjection = View.ViewMatrices.GetInvViewProjectionMatrix();

// 3. World → Decal (computed from FDeferredDecalProxy)
FMatrix WorldToDecalMatrix = VisibleDecal.ComponentTrans.ToInverseMatrixWithScale();

// 4. Compose: Screen → NDC → World → Decal
FMatrix44f SvPositionToDecal = FMatrix44f(
    SvPositionToDecalBase * InvViewProjection * WorldToDecalMatrix
);
```

**Size**: 4×4 = 16 floats = 256 bytes (but often packed into 4 float4 constants)

### DecalToWorld Matrix

```cpp
// For transforming normals from decal space to world space
FMatrix44f DecalToWorldMatrix = FMatrix44f(RelativeDecalToWorldMatrix);

// For scale-corrected normals (non-uniform scale)
FVector3f DecalToWorldInvScale = 
    static_cast<FVector3f>(DecalToWorldMatrix.GetScaleVector().Reciprocal());
```

**Usage**: Normal transform needs inverse-transpose, handled via `DecalToWorldInvScale`

### Additional Parameters

```cpp
// DecalPositionHigh: Large World Coordinates
float3 DecalPositionHigh = PositionHigh;

// DecalOrientation: Decal's local X-axis (for material expressions)
float3 DecalOrientation = (FVector3f)VisibleDecal.ComponentTrans.GetUnitAxis(EAxis::X);

// DecalParams: Fade control
float DecalParams.x = FadeAlphaValue;      // Screen-size fade [0,1]
float DecalParams.y = LifetimeAlpha;       // Time-based fade [0,1]

// DecalColorParam: Tint color
float4 DecalColorParam = VisibleDecal.DecalColor;
```

---

## Stage 5: Vertex Shader Dispatch

### Input Assembly
```hlsl
// Implicit (6 quad faces, 24 vertices per cube)
// OR explicit VB/IB with vertices at ±(1,1,1)

in float4 InPosition : ATTRIBUTE0;  // Decal space [-1,1]³
```

### Constant Buffers (Vertex Shader)

```cpp
// FDeferredDecalVS::FParameters
{
    FViewShaderParameters View;                  // Camera matrices
    FMatrix44f FrustumComponentToClip;           // Decal frustum → clip space
    FPrimitiveUniformShaderParameters Primitive; // Identity (no per-instance variation)
}
```

### Output
```hlsl
out float4 OutPosition : SV_POSITION;  // Clip space position
```

**Computation**:
```hlsl
OutPosition = mul(InPosition, FrustumComponentToClip);
```

**Key insight**: Depth will be replaced by pixel shader from scene depth buffer.

---

## Stage 6: Pixel Shader — Critical Data Path

### Input from Rasterizer
```hlsl
in float4 SvPosition : SV_POSITION;  // Screen pixel position (x,y,z=deviceZ,w=1)
in float2 ScreenUV;                  // Normalized viewport coords [0,1]
```

### Constant Buffers (Pixel Shader)

```cpp
// FDeferredDecalPS::SetParameters (DecalRenderingShared.cpp:199)
{
    // Decal matrices
    float4x4 SvPositionToDecal;              // [256 bytes]
    float4x4 DecalToWorld;                   // [256 bytes]
    float3   DecalToWorldInvScale;           // [12 bytes]
    float3   DecalOrientation;               // [12 bytes]
    
    // Decal properties
    float2   DecalParams;                    // (fadeAlpha, lifetimeAlpha)
    float4   DecalColorParam;                // RGBA color
    float4   DecalPositionHigh;              // LWC position high bits
    
    // Scene textures
    Texture2D<float> SceneDepth;
    Texture2D<float4> SceneNormal;           // (optional, for reprojection)
    
    // Material parameters (from FMaterialShader)
    // ... material-dependent constants ...
}
```

### Execution Steps

```hlsl
// 1. Lookup scene depth
float2 ScreenUV = SvPositionToBufferUV(SvPosition);
float deviceZ = LookupDeviceZ(ScreenUV);  // Sample SceneDepth
SvPosition.z = deviceZ;
SvPosition.w = ConvertFromDeviceZ(deviceZ);  // Reversed-Z conversion

// 2. Project to decal space
float4 DecalVectorHom = mul(float4(SvPosition.xyz, 1), SvPositionToDecal);
float3 OSPosition = DecalVectorHom.xyz / DecalVectorHom.w;

// 3. Early exit if outside bounds
clip(OSPosition.xyz + 1.0);    // Discard if any < -1
clip(1.0 - OSPosition.xyz);    // Discard if any > 1

// 4. Compute decal UVs
float3 DecalVector = OSPosition * 0.5 + 0.5;  // [-1,1] → [0,1]
float3 SwizzlePos = DecalVector.zyx;          // Reorder axes
float2 DecalUVs = SwizzlePos.xy;

// 5. Setup material parameters
FMaterialPixelParameters MaterialParameters;
MaterialParameters.TexCoords[0] = DecalUVs;
MaterialParameters.TwoSidedSign = 1;
MaterialParameters.CameraVector = normalize(CameraOrigin - WorldPosition);
// ... etc ...

// 6. Evaluate material
FPixelMaterialInputs PixelMaterialInputs;
CalcPixelMaterialInputs(MaterialParameters, PixelMaterialInputs);

float3 BaseColor = GetMaterialBaseColor(PixelMaterialInputs);
float3 WorldNormal = MaterialParameters.WorldNormal;
float Roughness = GetMaterialRoughness(PixelMaterialInputs);
float Metallic = GetMaterialMetallic(PixelMaterialInputs);
float Specular = GetMaterialSpecular(PixelMaterialInputs);
float Opacity = GetMaterialOpacity(PixelMaterialInputs);

// 7. Apply decal fade
const float DecalFading = 
    saturate(4 - 4 * abs(SwizzlePos.z * 2 - 1)) * DecalParams.x;
Opacity *= DecalFading;

// 8. Encode to DBuffer
float4 DBufferA = float4(BaseColor, Opacity);
float4 DBufferB = float4(WorldNormal * 0.5 + 128.0/255.0, Opacity);
float4 DBufferC = float4(Metallic, Specular, Roughness, Opacity);
```

---

## Stage 7: Render Targets — DBuffer

### Target Layout (Desktop)

| Index | RT Name | Format | Resolve | Contents |
|-------|---------|--------|---------|----------|
| 0 | DBufferA | RGBA8 (UNORM) | MSAA → SS | (R:BaseColor.r, G:BaseColor.g, B:BaseColor.b, A:ColorOpacity) |
| 1 | DBufferB | RGBA8 (UNORM) | MSAA → SS | (R:Normal.x×0.5+0.5, G:Normal.y×0.5+0.5, B:Normal.z×0.5+0.5, A:NormalOpacity) |
| 2 | DBufferC | RGBA8 (UNORM) | MSAA → SS | (R:Metallic, G:Specular, B:Roughness, A:RoughnessOpacity) |
| 3* | DBufferMask | R8 (UNORM) | - | RT write mask (platform-dependent) |

### Load/Store Actions

```cpp
// Load DBuffer at start
for (RT in DBufferA, DBufferB, DBufferC)
    RT.LoadAction = ERenderTargetLoadAction::ELoad;

// Blend mode: pre-multiplied alpha
RT.BlendState = {
    SrcBlend: SrcAlpha,
    DstBlend: InvSrcAlpha,
    BlendOp: Add,
};

// Depth
Depth.LoadAction = ELoad;
Depth.StoreAction = ENoAction;  // Don't write depth
```

---

## Stage 8: Lighting Pass — DBuffer Application

### Reading DBuffer

```hlsl
float2 BufferUV = PixelPos / RTSize;

float4 DBufferA = DBufferATexture.Sample(BufferUV);
float4 DBufferB = DBufferBTexture.Sample(BufferUV);
float4 DBufferC = DBufferCTexture.Sample(BufferUV);

// Decode
float3 DecalBaseColor = DBufferA.rgb;
float DecalColorOpacity = DBufferA.a;

float3 DecalWorldNormal = DBufferB.rgb * 2.0 - (256.0/255.0);  // Reverse encoding
float DecalNormalOpacity = DBufferB.a;

float DecalMetallic = DBufferC.r;
float DecalSpecular = DBufferC.g;
float DecalRoughness = DBufferC.b;
float DecalRoughnessOpacity = DBufferC.a;
```

### Applying to GBuffer

```hlsl
// Blend with geometry data (pre-multiplied alpha)
#if MATERIALDECALRESPONSEMASK & 0x1  // Respond to color
    BaseColor = BaseColor * (1.0 - DecalColorOpacity) + DecalBaseColor;
#endif

#if MATERIALDECALRESPONSEMASK & 0x2  // Respond to normal
    WorldNormal = normalize(
        WorldNormal * (1.0 - DecalNormalOpacity) + DecalWorldNormal
    );
#endif

#if MATERIALDECALRESPONSEMASK & 0x4  // Respond to roughness/metallic
    Roughness = Roughness * (1.0 - DecalRoughnessOpacity) + DecalRoughness;
    Metallic = Metallic * (1.0 - DecalRoughnessOpacity) + DecalMetallic;
    Specular = Specular * (1.0 - DecalRoughnessOpacity) + DecalSpecular;
#endif
```

---

## Complete Data Flow Diagram

```
UDecalComponent
├─ RelativeLocation, RelativeRotation, RelativeScale3D
├─ DecalSize (scale multiplier)
├─ DecalMaterial
├─ DecalColor
├─ FadeDuration, FadeInDuration, ...
└─ SortOrder

    ↓ [Render thread snapshot]

FDeferredDecalProxy
├─ ComponentTrans = (Location, Rotation, Scale×DecalSize)
├─ DecalMaterial
├─ DecalColor
├─ Bounds (sphere/box for culling)
└─ Fade/Lifetime params

    ↓ [Visibility cull]

FVisibleDecal
├─ ComponentTrans (for matrix computation)
├─ MaterialProxy
├─ BlendDesc (PSO)
└─ FadeAlpha (screen-size)

    ↓ [Matrix setup]

FDeferredDecalPS::SetParameters
├─ SvPositionToDecal = SvPosToNDC × InvViewProj × WorldToDecal [256 bytes]
├─ DecalToWorld [256 bytes]
├─ DecalToWorldInvScale [12 bytes]
├─ DecalOrientation [12 bytes]
├─ DecalParams [8 bytes]
├─ DecalColorParam [16 bytes]
└─ (+ all material parameters)

    ↓ [GPU: Vertex Shader]

Vertex Output (per-fragment interpolant)
├─ SvPosition (clip space)
└─ (rasterized to screen space)

    ↓ [GPU: Pixel Shader]

Decal Evaluation
├─ OSPosition = SvPosition × SvPositionToDecal [perspective divide]
├─ clip() if OSPosition outside [-1,1]³
├─ DecalUVs = OSPosition.zyx × 0.5 + 0.5
├─ Material evaluation (BaseColor, Normal, Roughness, Metallic, Opacity)
└─ Fade multiplication

    ↓ [Encoding]

DBuffer Output
├─ DBufferA = (BaseColor, ColorOpacity)
├─ DBufferB = (Normal×0.5+0.5, NormalOpacity)
└─ DBufferC = (Metallic, Specular, Roughness, RoughnessOpacity)

    ↓ [Framebuffer blend]

DBuffer RTs (written with alpha blending)

    ↓ [Later: Lighting Pass]

Decal Modulation
├─ Read DBuffer at PixelPos
├─ Decode (reverse normal encoding, etc.)
├─ Blend with GBuffer:
│  ├─ BaseColor ← BaseColor×(1-opacity) + DecalBaseColor
│  ├─ WorldNormal ← normalize(Normal×(1-opacity) + DecalNormal)
│  └─ Roughness/Metallic ← blend
└─ Continue with standard lighting

    ↓ [Output]

Final lit image (with decal modulation applied)
```

---

## Key Invariants

| Invariant | Why | How to Check |
|-----------|-----|--------------|
| OSPosition in [-1,1]³ after clip | Decals only apply inside OBB | `assert(all(abs(OSPosition) <= 1.0))` |
| Opacity ∈ [0,1] after fade | Prevents over-darkening | `assert(Opacity >= 0 && Opacity <= 1)` |
| SvPositionToDecal is invertible | Must transform back from decal space | Check det(SvPositionToDecal) ≠ 0 |
| DecalToWorldInvScale > 0 | Scale reciprocal must be positive | `assert(all(DecalToWorldInvScale > 0))` |
| DBuffer RTs written at blend time | Pre-multiplied alpha requires write | PSO has `BlendOp=Add, SrcBlend=SrcAlpha` |

---

## Memory Layout Summary

| Component | Size | Type | Binding |
|-----------|------|------|---------|
| SvPositionToDecal | 256 B | float4x4 | CB (root const or CBV) |
| DecalToWorld | 256 B | float4x4 | CB |
| DecalToWorldInvScale | 12 B | float3 | CB |
| DecalOrientation | 12 B | float3 | CB |
| DecalParams | 8 B | float2 | CB |
| DecalColorParam | 16 B | float4 | CB |
| SceneDepth | 2 KB+ | Texture2D | SRV |
| Material params | varies | varies | CB + SRV |
| **Total PS CB** | ~600 B | - | Typical |

---

## Timing & Synchronization

```
Frame N:
  T0: GBuffer pass (fill depth + normals)
     └─ SceneDepth produced
  
  T1: DBuffer decal pass ← needs SceneDepth
     └─ Reads SceneDepth
     └─ Writes DBufferA/B/C
     └─ (depth-write disabled, no depth dependency)
  
  T2: Lighting pass ← needs DBufferA/B/C
     └─ Reads DBufferA/B/C
     └─ Applies modulation to GBuffer
     └─ Outputs lit color

Key: Decals have no depth dependency (except reading existing depth).
     Multiple decal passes can run sequentially without synchronization.
```

