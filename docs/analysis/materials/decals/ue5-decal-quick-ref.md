<!-- doc-meta
type: quick-reference
scope: deferred-decal-implementation
derived-from: ue5-deferred-decal-architecture.md
-->

# UE5 Deferred Decal — Quick Implementation Reference

## The 4-Step Decal Rendering Pipeline

### Step 1: Vertex Shader (Unit Cube Projection)
```hlsl
// Input: InPosition in [-1,1]³ (OBB corner)
// Constant: FrustumComponentToClip (camera frustum × decal frustum)
OutPosition = mul(InPosition, FrustumComponentToClip);
// Result: SV_POSITION in clip space
```

**Key**: `FrustumComponentToClip` is a single 4×4 transform.

---

### Step 2: Pixel Shader — Reproject to Decal Space

```hlsl
// 1. Fetch scene depth at this pixel
float deviceZ = sceneDepthTexture.Load(uint3(SvPosition.xy, 0)).r;
SvPosition.z = deviceZ;
SvPosition.w = ConvertFromDeviceZ(deviceZ);  // Reversed-Z conversion

// 2. Transform screen position → decal local space [-1,1]³
float4 DecalVectorHom = mul(float4(SvPosition.xyz, 1), SvPositionToDecal);
float3 OSPosition = DecalVectorHom.xyz / DecalVectorHom.w;

// 3. Cull geometry outside the decal OBB
clip(OSPosition.xyz + 1.0);    // Discard if x,y,z < -1
clip(1.0 - OSPosition.xyz);    // Discard if x,y,z > 1

// 4. Compute texture UVs and fade
float3 DecalVector = OSPosition * 0.5 + 0.5;  // [-1,1] → [0,1]
float3 SwizzlePos = DecalVector.zyx;          // Reorder axes
float2 DecalUVs = SwizzlePos.xy;              // Final material UVs
float FadeDistance = SwizzlePos.z;            // [0,1] along projection axis
```

**Key matrices**:
- `SvPositionToDecal = SvPositionToDecalBase × InvViewProjection × WorldToDecal`
- `SvPositionToDecalBase` handles viewport scaling
- `InvViewProjection` converts NDC to world space
- `WorldToDecal` converts world to decal local space

---

### Step 3: Material Evaluation → DBuffer Encoding

```hlsl
// From decal material
float3 BaseColor = SampleBaseColor(DecalUVs);
float3 WorldNormal = SampleNormal(DecalUVs);
float Roughness = SampleRoughness(DecalUVs);
float Metallic = SampleMetallic(DecalUVs);
float Specular = SampleSpecular(DecalUVs);
float Opacity = SampleOpacity(DecalUVs) * DecalFading;

// Encode to DBuffer format
float4 DBufferA = float4(BaseColor, Opacity);
float4 DBufferB = float4(WorldNormal * 0.5 + 0.5, Opacity);
float4 DBufferC = float4(Metallic, Specular, Roughness, Opacity);

// Output to 3 DBuffer RTs
Output.RT0 = DBufferA;
Output.RT1 = DBufferB;
Output.RT2 = DBufferC;
```

**Format Details**:
| RT | Format | Data |
|----|--------|------|
| DBufferA | RGBA8 | (R: BaseColor.r, G: BaseColor.g, B: BaseColor.b, A: ColorOpacity) |
| DBufferB | RGBA8 | (R: Normal.x×0.5+0.5, G: Normal.y×0.5+0.5, B: Normal.z×0.5+0.5, A: NormalOpacity) |
| DBufferC | RGBA8 | (R: Metallic, G: Specular, B: Roughness, A: RoughnessOpacity) |

**Decoding** (in lighting pass):
```hlsl
float3 DecodedNormal = DBufferB.rgb * 2 - 1;  // Reverse the encoding
```

---

### Step 4: Lighting Pass — Apply DBuffer Modulation

```hlsl
// In the main lighting pass, read DBuffer
float4 DBufferA = DBufferATexture.Load(uint3(PixelPos, 0));
float4 DBufferB = DBufferBTexture.Load(uint3(PixelPos, 0));
float4 DBufferC = DBufferCTexture.Load(uint3(PixelPos, 0));

// Decode
float3 DecalNormal = DBufferB.rgb * 2 - 1;
float DecalMetallic = DBufferC.r;
float DecalSpecular = DBufferC.g;
float DecalRoughness = DBufferC.b;

// Pre-multiplied alpha blending
float ColorOpacity = DBufferA.a;
float NormalOpacity = DBufferB.a;
float RoughnessOpacity = DBufferC.a;

// Apply modulation
BaseColor = BaseColor * (1 - ColorOpacity) + DBufferA.rgb;
WorldNormal = normalize(WorldNormal * (1 - NormalOpacity) + DecalNormal);
Roughness = Roughness * (1 - RoughnessOpacity) + DecalRoughness;
Metallic = Metallic * (1 - RoughnessOpacity) + DecalMetallic;
Specular = Specular * (1 - RoughnessOpacity) + DecalSpecular;
```

---

## C++ Shader Parameter Setup

### Matrix Computation

```cpp
// From FVisibleDecal
const FMatrix DecalToWorldMatrix = VisibleDecal.ComponentTrans.ToMatrixWithScale();
const FMatrix WorldToDecalMatrix = VisibleDecal.ComponentTrans.ToInverseMatrixWithScale();

// Screen-to-NDC transform
FVector2D InvViewSize = FVector2D(1.0f / View.ViewRect.Width(), 
                                  1.0f / View.ViewRect.Height());
float Mx = 2.0f * InvViewSize.X;
float My = -2.0f * InvViewSize.Y;
float Ax = -1.0f - 2.0f * View.ViewRect.Min.X * InvViewSize.X;
float Ay = 1.0f + 2.0f * View.ViewRect.Min.Y * InvViewSize.Y;

const FMatrix SvPositionToDecalBase(
    FPlane(Mx, 0, 0, 0),
    FPlane(0, My, 0, 0),
    FPlane(0, 0, 1, 0),
    FPlane(Ax, Ay, 0, 1)
);

// Final matrix
FMatrix44f SvPositionToDecal = FMatrix44f(
    SvPositionToDecalBase * View.ViewMatrices.GetInvViewProjectionMatrix() * WorldToDecalMatrix
);

SetShaderValue(BatchedParameters, SvPositionToDecalParam, SvPositionToDecal);
```

### PSO Configuration

**Depth/Stencil**:
```cpp
DepthTest = GREATER_EQUAL;      // Reversed-Z
DepthWrite = false;              // Don't modify depth
StencilWrite = true;             // (Optional optimization)
```

**Rasterizer**:
```cpp
FrontCounterClockwise = false;   // Standard
CullMode = BACK;                 // Cull back-faces (render from inside)
```

**Blend**:
```cpp
SrcBlend = SrcAlpha;
DstBlend = InvSrcAlpha;
BlendOp = Add;
// Per-RT (DBuffer standard)
```

---

## Fade Formula

```hlsl
// Fade from center to edges of decal along projection axis
// Distance = SwizzlePos.z, which is [0,1] (0 = far edge, 1 = center, 0 = near edge)
float EdgeFadeDistance = abs(SwizzlePos.z * 2 - 1);  // [0,1] → [1,0,1]
float DecalFading = saturate(4 - 4 * EdgeFadeDistance);  // Smooth falloff at edges
```

---

## Common Mistakes

| Mistake | Effect | Fix |
|---------|--------|-----|
| Writing to depth buffer | Decals occlude geometry behind them | `DepthWrite=false` |
| Front-face culling | Decal invisible (rendering back-faces from outside) | Use back-face cull |
| Not clipping outside OBB | Decal UVs wrap beyond bounds | `clip()` after projection |
| Rendering after lighting | Can't modulate GBuffer values | Render before lighting pass |
| Wrong matrix order | Decal appears in wrong position | Use `Base × InvViewProj × WorldToDecal` |
| Not pre-multiplying alpha | Blend colors incorrectly | `(1-opacity)×old + opacity×new` |
| Hardcoding normal as blue | Can't change decal orientation | Sample normal from material |

---

## Parameter List (Shader Constants)

```hlsl
// Per-decal
float4x4 SvPositionToDecal;      // Screen → decal space
float4x4 DecalToWorld;            // Decal → world (for normal transformation)
float3   DecalToWorldInvScale;    // For scale-corrected normals
float3   DecalOrientation;        // Decal's X-axis (for material expression)
float2   DecalParams;             // (fade screen-size, lifetime alpha)
float4   DecalColorParam;         // Decal color tint
float4   DecalPositionHigh;       // LWC high bits (for large worlds)
```

---

## Timeline: When Decals Render

```
Frame:
  [1] Base Pass (GBuffer fill)
  [2] ← Decals Render Here (DBuffer stage)
  [3] Lighting Pass (applies DBuffer modulation)
  [4] Transparency/Post-process
```

Alternative (if not using DBuffer):
```
Frame:
  [1] Base Pass (GBuffer fill)
  [2] Lighting Pass
  [3] ← Decals Render Here (GBuffer stage, modifies result)
  [4] Transparency/Post-process
```

**Best practice**: Use DBuffer (Stage 2) for unlimited decal count without performance cost.

---

## Testing Checklist

- [ ] Decal appears at correct world position
- [ ] Decal rotates with component transform
- [ ] Decal scales with component size (OBB)
- [ ] Decal UVs don't wrap outside bounds
- [ ] Decal normal blends smoothly with geometry normal
- [ ] Decal fade-out works at edges
- [ ] Multiple decals stack correctly (no z-fighting)
- [ ] Decal doesn't occlude geometry (depth-write disabled)
- [ ] Decal visible from all camera angles (no culling artifacts)
- [ ] Performance: 100+ decals at 60 FPS

