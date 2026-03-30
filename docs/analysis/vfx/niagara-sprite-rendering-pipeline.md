<!-- doc-meta
title: Niagara Sprite Rendering Pipeline — Complete Analysis
category: vfx
created: 2026-03-29
-->

# Niagara Sprite Rendering Pipeline — Complete Analysis

Exhaustive analysis of how UE5 Niagara renders particle sprites with materials,
traced from vertex factory through material integration to final pixel output.
Includes comparison with UzEngine's current implementation to identify bugs.

---

## 1. Sprite Vertex Factory — How Particles Become Quads

### File: `Engine/Plugins/FX/Niagara/Shaders/Private/NiagaraSpriteVertexFactory.ush`

### 1.1 Input Structure (line 42-48)

```hlsl
struct FVertexFactoryInput
{
    float2 TexCoord    : ATTRIBUTE0;  // Pre-baked quad UV from vertex buffer
    uint VertexId      : SV_VertexID;
    uint InstanceId    : SV_InstanceID;
};
```

**KEY DIFFERENCE FROM UZENGINE**: Niagara uses **instanced rendering** with a
pre-baked quad vertex buffer + index buffer, NOT pure SV_VertexID expansion.

- **Vertex buffer**: `GParticleTexCoordVertexBuffer` — 4 vertices per sprite:
  `(0,0), (0,1), (1,1), (1,0)` (file: `Engine/Source/Runtime/Engine/Private/Particles/ParticleResources.cpp`, line 31-34)
- **Index buffer**: `GParticleIndexBuffer` — 6 indices per sprite:
  `{0, 2, 3, 0, 1, 2}` (file: same, line 95-101)
- **Draw call**: `DrawIndexedInstanced(6, numParticles, 0, 0, 0)` —
  6 indices per instance, N instances (one per alive particle)

The `TexCoord` input comes from the vertex buffer (ATTRIBUTE0). The `InstanceId`
selects which particle to read data from.

### 1.2 Per-Particle Data Access (lines 751-884)

Particle attributes are read from SRV buffers (`NiagaraParticleDataFloat`,
`NiagaraParticleDataHalf`) using offset-based accessors:

```hlsl
// Accessor pattern — read particle attribute at data offset for given instance
float3 GetNiagaraParticleSimPosition(uint InstanceID) {
    return SafeGetVec3(NiagaraSpriteVF.PositionDataOffset, InstanceID,
                       NiagaraSpriteVF.DefaultPos.xyz);
}
float4 GetNiagaraParticleColor(uint InstanceID) {
    return SafeGetVec4(NiagaraSpriteVF.ColorDataOffset, InstanceID,
                       NiagaraSpriteVF.DefaultColor);
}
float GetNiagaraParticleSubimage(uint InstanceID) {
    return SafeGetFloat(NiagaraSpriteVF.SubimageDataOffset, InstanceID,
                        NiagaraSpriteVF.DefaultSubImage);
}
```

Each attribute has a **data offset** (index into the particle data buffer) and
a **default value** (used when the offset is INDEX_NONE, meaning the attribute
is not present in the simulation).

**Defaults** (from `NiagaraRendererSprites.cpp`, line 562-585):
- DefaultPos: emitter world position
- DefaultSize: `(50.0, 50.0)` — world-space units, NOT half-size
- DefaultUVScale: `(1.0, 1.0)`
- DefaultColor: `(1.0, 1.0, 1.0, 1.0)` — fully opaque white
- DefaultRotation: `0.0` — degrees (converted to radians in shader)
- DefaultSubImage: `0.0` — first frame

### 1.3 Rotation Conversion (line 803-804)

```hlsl
const float Rotation = (SafeGetFloat(..., DefaultRotation) / 180.0f) * PI;
return Rotation * NiagaraSpriteVF.RotationScale + NiagaraSpriteVF.RotationBias;
```

Rotation is stored in **degrees** in the particle data, converted to radians
in the vertex shader. `RotationScale` defaults to 1.0, `RotationBias` to 0.0.
When velocity-aligned, `RotationScale` = 0.0 (rotation is ignored).

### 1.4 Billboard Quad Expansion (lines 959-996)

```hlsl
// Tangents computed from facing/alignment mode (line 963)
GetTangents(Input, ParticleTranslatedWorldPosition, ParticleRotation,
            CustomFacing, CustomAlignment, ParticleDirection, Right, Up);

// UV + SubImage computation (line 971)
ComputeBillboardUVs(Input, ParticleSize, SubImageIndex, UVScale,
                    UVForPosition, UVForTexturing, UVForTexturingUnflipped);

// World position = center + offset * tangent (line 993-996)
const float2x3 Tangents = float2x3(Intermediates.TangentRight, Intermediates.TangentUp);
const float3 VertexOffset = CameraOffset
    + mul(Size * PixelSizeRatio * (UVForPosition - PivotOffset), Tangents);
Intermediates.VertexWorldPosition = LWCAdd(ParticlePosition, VertexOffset);
```

**CRITICAL DETAIL**: The billboard expansion multiplies by `(UVForPosition - PivotOffset)`.
- `UVForPosition` comes from the vertex buffer UV: `(0,0), (0,1), (1,1), (1,0)`
- `PivotOffset` defaults to `(0.5, 0.5)` — the center of the quad
- So the expansion range is `(-0.5, -0.5)` to `(0.5, 0.5)`, multiplied by `Size`
- This means `Size` is the FULL size of the sprite, not half-size

**UzEngine uses** `halfSize = finalSize * 0.5` with offsets in `[-1, +1]`, which
is mathematically equivalent: `[-1,+1] * halfSize = [-0.5,+0.5] * fullSize`.

---

## 2. SubUV / Flipbook — Frame Selection and UV Remapping

### File: `NiagaraSpriteVertexFactory.ush`, lines 1048-1064

This is the **EXACT** UV remapping formula:

```hlsl
// SubUV computation
float SubImageLerp = frac(SubImageIndex);

// Bias to avoid precision issues
float SubImageA = SubImageIndex - SubImageLerp + 0.5f;
float SubImageB = SubImageA + 1;

// Column = frame mod numCols
float SubImageAH = floor(fmod(SubImageA, NiagaraSpriteVF.SubImageSize.x));
float SubImageBH = floor(fmod(SubImageB, NiagaraSpriteVF.SubImageSize.x));

// Row = frame / numCols (integer division via multiply by inverse)
float SubImageAV = floor(SubImageA * NiagaraSpriteVF.SubImageSize.z);
float SubImageBV = floor(SubImageB * NiagaraSpriteVF.SubImageSize.z);

// Final UV = (cellOrigin + baseUV) * cellSize
Intermediates.TexCoord.xy = (float2(SubImageAH, SubImageAV) + UVForTexturing)
                           * NiagaraSpriteVF.SubImageSize.zw;
Intermediates.TexCoord.zw = (float2(SubImageBH, SubImageBV) + UVForTexturing)
                           * NiagaraSpriteVF.SubImageSize.zw;

// Lerp factor: only used when SubImageBlendMode == 1
Intermediates.SubImageLerp = (NiagaraSpriteVF.SubImageBlendMode == 1)
                            ? SubImageLerp : 0.0f;
```

### SubImageSize Uniform (line 551, NiagaraRendererSprites.cpp)

```cpp
PerViewUniformParameters.SubImageSize = FVector4f(
    SubImageSize.X,          // .x = numCols (e.g., 8)
    SubImageSize.Y,          // .y = numRows (e.g., 8)
    1.0f / SubImageSize.X,   // .z = 1/numCols (inverse for UV scale)
    1.0f / SubImageSize.Y    // .w = 1/numRows (inverse for UV scale)
);
```

### SubImageIndex Source

The `SubImageIndex` is a **float** per-particle attribute:
- Set by CPU Niagara modules (e.g., "SubImage Index" module)
- Typically computed as: `SubImageIndex = normalizedAge * totalFrames`
- The **integer part** selects the current frame
- The **fractional part** is the blend factor between current and next frame

### Comparison with UzEngine

UzEngine's flipbook code (`vfx_flipbook.hlsli`) uses the same mathematical approach:
```hlsl
uint col = frameIndex % cols;
uint row = frameIndex / cols;
return float2((float)col * invCols, (float)row * invRows);
```
And then: `frameUV + baseUV * frameSize` (SampleFlipbookUV, line 170).

**This is equivalent.** The UzEngine flipbook UV formula is correct.

---

## 3. Material Integration — How the Material Samples the Sprite

### 3.1 ParticleColor Node

**File**: `HLSLMaterialTranslator.cpp`, line 6416-6419

```cpp
int32 FHLSLMaterialTranslator::ParticleColor() {
    bUsesParticleColor |= (ShaderFrequency != SF_Vertex);
    return AddExternalCodeChunk(TEXT("ParticleColor"));
}
```

This generates the HLSL code `Parameters.Particle.Color` which reads from the
VS-to-PS interpolant. When `NEEDS_PARTICLE_COLOR` is defined (line 2835):
```hlsl
Result.Particle.Color = Interpolants.Color;  // line 347
```

The interpolated color comes from the vertex factory:
```hlsl
Interpolants.Color = Intermediates.Color;  // line 1174
```
Where `Intermediates.Color` is read per-particle: `GetNiagaraParticleColor(ParticleID)`.

**In UzEngine**: `input.color` in PSInput serves the same role — the particle
color is already interpolated per-vertex.

### 3.2 ParticleSubUV Node (Texture Sampling)

**File**: `HLSLMaterialTranslator.cpp`, lines 6354-6391

```cpp
int32 FHLSLMaterialTranslator::ParticleSubUV(
    int32 TextureIndex, EMaterialSamplerType SamplerType,
    int32 MipValue0Index, int32 MipValue1Index,
    ETextureMipValueMode MipValueMode, bool bBlend)
{
    int32 ParticleSubUV;
    const FString TexCoordCode = TEXT("Parameters.Particle.SubUVCoords[%u].xy");
    const int32 TexCoord1 = AddCodeChunk(MCT_Float2, *TexCoordCode, 0);

    if (bBlend) {
        // Two-frame linear interpolation:
        // A = texture.Sample(sampler, Parameters.Particle.SubUVCoords[0].xy)
        // B = texture.Sample(sampler, Parameters.Particle.SubUVCoords[1].xy)
        // result = lerp(A, B, Parameters.Particle.SubUVLerp)
        const int32 TexCoord2 = AddCodeChunk(MCT_Float2, *TexCoordCode, 1);
        const int32 SubImageLerp = AddCodeChunk(MCT_Float,
            TEXT("Parameters.Particle.SubUVLerp"));
        const int32 TexSampleA = TextureSample(TextureIndex, TexCoord1, ...);
        const int32 TexSampleB = TextureSample(TextureIndex, TexCoord2, ...);
        ParticleSubUV = Lerp(TexSampleA, TexSampleB, SubImageLerp);
    } else {
        // Single-frame, no blend:
        // result = texture.Sample(sampler, Parameters.Particle.SubUVCoords[0].xy)
        ParticleSubUV = TextureSample(TextureIndex, TexCoord1, ...);
    }
    bUsesParticleSubUVs = true;
    return ParticleSubUV;
}
```

### 3.3 SubUVCoords Flow Through the Pipeline

```
VS: GetNiagaraParticleSubimage(ParticleID) returns float SubImageIndex
    |
    v
VS: SubUV math computes:
    TexCoord.xy = (cellA + baseUV) * inverseDims  <- frame A UV
    TexCoord.zw = (cellB + baseUV) * inverseDims  <- frame B UV
    SubImageLerp = frac(SubImageIndex)
    |
    v
VS Interpolants:
    ParticleSubUVs.xy = TexCoord.xy  (frame A UV)
    ParticleSubUVs.zw = TexCoord.zw  (frame B UV)
    TangentToWorld0AndInterp_Sizer.w = SubImageLerp
    |
    v
PS: GetMaterialPixelParameters():
    Result.Particle.SubUVCoords[0] = Interpolants.ParticleSubUVs.xy
    Result.Particle.SubUVCoords[1] = Interpolants.ParticleSubUVs.zw
    Result.Particle.SubUVLerp = SubImageLerp
    |
    v
Material: ParticleSubUV expression samples texture at SubUVCoords[0] and [1]
          lerps by SubUVLerp (if bBlend is true)
```

**CRITICAL**: The UV remapping from base quad UV to atlas sub-rectangle happens
in the **vertex shader**, NOT the pixel shader. The PS receives already-remapped
UVs that point directly into the atlas texture.

**UzEngine** does the UV remapping in the **pixel shader** (vfx_flipbook.hlsli),
which is functionally equivalent but slightly less efficient (per-pixel instead
of per-vertex). This is not a correctness issue.

---

## 4. Blend States — EXACT D3D States

### File: `Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp`, lines 355-407

#### BLEND_Translucent (Standard Alpha Blend)

```
Color: BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha
Alpha: BO_Add, BF_Zero, BF_InverseSourceAlpha
WriteMask: CW_RGBA
```
D3D11/12 equivalent:
```
SrcBlend      = D3D12_BLEND_SRC_ALPHA
DestBlend     = D3D12_BLEND_INV_SRC_ALPHA
BlendOp       = D3D12_BLEND_OP_ADD
SrcBlendAlpha = D3D12_BLEND_ZERO
DestBlendAlpha= D3D12_BLEND_INV_SRC_ALPHA
BlendOpAlpha  = D3D12_BLEND_OP_ADD
```
**Output**: `float4(EmissiveColor, Opacity)` — straight alpha, NOT premultiplied.

#### BLEND_Additive

```
Color: BO_Add, BF_One, BF_One
Alpha: BO_Add, BF_Zero, BF_InverseSourceAlpha
WriteMask: CW_RGBA
```
D3D11/12 equivalent:
```
SrcBlend      = D3D12_BLEND_ONE
DestBlend     = D3D12_BLEND_ONE
BlendOp       = D3D12_BLEND_OP_ADD
SrcBlendAlpha = D3D12_BLEND_ZERO
DestBlendAlpha= D3D12_BLEND_INV_SRC_ALPHA
BlendOpAlpha  = D3D12_BLEND_OP_ADD
```
**Output**: `float4(EmissiveColor, Opacity)` — RGB is added directly (Src=One),
alpha channel preserves background compositing info.

#### BLEND_AlphaComposite (Premultiplied Alpha)

```
Color: BO_Add, BF_One, BF_InverseSourceAlpha
Alpha: BO_Add, BF_Zero, BF_InverseSourceAlpha
WriteMask: CW_RGBA
```
D3D11/12 equivalent:
```
SrcBlend      = D3D12_BLEND_ONE
DestBlend     = D3D12_BLEND_INV_SRC_ALPHA
BlendOp       = D3D12_BLEND_OP_ADD
SrcBlendAlpha = D3D12_BLEND_ZERO
DestBlendAlpha= D3D12_BLEND_INV_SRC_ALPHA
BlendOpAlpha  = D3D12_BLEND_OP_ADD
```
**Output**: `float4(RGB * alpha, alpha)` — RGB must be premultiplied before output.

#### BLEND_Modulate

```
Color: BO_Add, BF_DestColor, BF_Zero
Alpha: (same)
WriteMask: CW_RGB (no alpha write)
```

### Comparison with UzEngine

UzEngine's blend modes (from `particle_billboard_ps.hlsl` comments, lines 17-20):
- AlphaBlend: SrcAlpha / InvSrcAlpha — **matches UE5 Translucent for color channel**
- Additive: One / One — **matches UE5 Additive for color channel**
- PremulAlpha: One / InvSrcAlpha — **matches UE5 AlphaComposite for color channel**

**Alpha channel blending**: UE5 uses `BF_Zero / BF_InverseSourceAlpha` for the
alpha channel in all three modes. UzEngine should verify its PSO alpha channel
blend settings match.

---

## 5. Pixel Shader Output — Final Color Formula

### For Unlit Translucent Materials (most particle materials)

The typical UE5 particle material is **Unlit + Translucent**. The material graph:
```
ParticleSubUV (texture sample) -> EmissiveColor
ParticleColor.A * texture.A    -> Opacity
```

Or more commonly:
```
ParticleSubUV * ParticleColor -> EmissiveColor
(derived opacity)             -> Opacity
```

### Final Output Computation

For non-Substrate (legacy) Unlit Translucent:

```hlsl
// The material evaluates user graph, producing:
EmissiveColor = GetMaterialEmissive(PixelMaterialInputs);   // clamped >= 0
Opacity = GetMaterialOpacity(PixelMaterialInputs);           // saturated [0,1]

// Clip near-zero opacity
clip(Opacity - 1.0 / 255.0);

// Output:
Out.MRT[0] = float4(EmissiveColor, Opacity);
// Fog applied on top if applicable
```

For a typical particle material:
```
EmissiveColor = TextureSample(SubUVCoords) * ParticleColor.rgb
Opacity       = TextureSample.a * ParticleColor.a
```

### UzEngine Equivalent

```hlsl
// particle_billboard_ps.hlsl, line 370
float4 finalColor = spriteColor * input.color * colorLifeScale;
```

This combines:
- `spriteColor` = texture sample (with flipbook UV) = **EmissiveColor source**
- `input.color` = particle color from VS = **ParticleColor**
- `colorLifeScale` = lerp(colorLifeStart, colorLifeEnd, normalizedAge)

Then alpha erosion, soft depth fade, and blend-mode-specific alpha handling.

**This formula is correct** and matches UE5's typical particle material behavior.

---

## 6. SubImageSize Uniform Layout

### UE5 (NiagaraSpriteVertexFactory.h, line 26)

```cpp
SHADER_PARAMETER_EX(FVector4f, SubImageSize, EShaderPrecisionModifier::Half)
```

Value set in C++ (NiagaraRendererSprites.cpp, line 551):
```cpp
SubImageSize = FVector4f(
    numCols,         // .x
    numRows,         // .y
    1.0f / numCols,  // .z = inverse cols (for UV scale)
    1.0f / numRows   // .w = inverse rows (for UV scale)
);
```

### UzEngine (VFXBillboardCB)

```hlsl
uint flipbookRows;           // separate uint
uint flipbookCols;           // separate uint
uint flipbookTotalFrames;
float flipbookFPS;
```

**Difference**: UE5 packs both the dimensions AND their inverses into a single
float4. UzEngine uses separate uints. The inverse computation happens in the
flipbook helper function (`1.0 / (float)cols`), which is correct but requires
the GPU to compute the division per-vertex/pixel instead of once on CPU.

---

## 7. What Could Be Wrong in UzEngine

Based on the analysis, here are the most likely causes for sprite sheet textures
not displaying correctly:

### 7.1 Texture Format / sRGB

UE5 particle textures are typically imported as:
- **Format**: RGBA 8-bit per channel (BC1/BC3 compressed on GPU)
- **sRGB**: YES for color textures (the texture is stored in sRGB, sampled in linear)
- **Alpha**: Stored in texture alpha channel, sampled directly

If UzEngine loads the sprite sheet as **linear** instead of **sRGB**, the colors
will appear washed out / too bright. Conversely, if loaded as sRGB but sampled
with a non-sRGB-aware view, colors may appear too dark.

### 7.2 Sampler State

UzEngine's PS declares: `SamplerState linearSampler : register(s0)` — described
as "Anisotropic wrap" (line 179). For sprite sheet / flipbook textures:
- **Address mode**: Should be **Clamp** (not Wrap), otherwise UV coordinates near
  frame edges will bleed into adjacent frames
- **Filter**: Bilinear or trilinear is fine

**UE5** typically uses **Clamp** addressing for particle textures to prevent
atlas bleeding.

### 7.3 UV Coordinate System

UzEngine's quad UVs (particle_billboard_vs.hlsl, line 58-65):
```hlsl
static const float2 kCornerUVs[6] = {
    float2(0.0f, 1.0f),  // 0: BL
    float2(1.0f, 1.0f),  // 1: BR
    float2(0.0f, 0.0f),  // 2: TL
    ...
};
```
- BL = (0,1), TL = (0,0) — UV V is flipped: V=0 at TOP, V=1 at BOTTOM.

UE5's quad UVs (`ParticleResources.cpp`, line 31-34):
```cpp
Vertices[SpriteIndex*4 + 0] = FVector2f(0.0f, 0.0f);  // vertex 0
Vertices[SpriteIndex*4 + 1] = FVector2f(0.0f, 1.0f);  // vertex 1
Vertices[SpriteIndex*4 + 2] = FVector2f(1.0f, 1.0f);  // vertex 2
Vertices[SpriteIndex*4 + 3] = FVector2f(1.0f, 0.0f);  // vertex 3
```
With index buffer `{0, 2, 3, 0, 1, 2}`, the two triangles are:
- T0: v0(0,0), v2(1,1), v3(1,0)
- T1: v0(0,0), v1(0,1), v2(1,1)

**Both use the same UV convention**: (0,0) at top-left, (1,1) at bottom-right.
UzEngine's mapping is equivalent.

### 7.4 Texture Not Actually Being Sampled

If `spriteTextureIdx` is `0xFFFFFFFF` (invalid), UzEngine falls through to the
procedural soft circle (lines 298-304). Check that:
1. The texture SRV descriptor index is correctly passed to the shader
2. The descriptor heap index is valid (not -1 / 0xFFFFFFFF)
3. The texture is actually uploaded to GPU memory before the draw call

### 7.5 Flipbook Frame Index Computation

UE5 uses `age * fps` wrapped by `totalFrames`. UzEngine uses the same formula
(`vfx_flipbook.hlsli`, line 137-140):
```hlsl
float frameFloat = max(age, 0.0) * fps;
uint frame0 = (uint)frameFloat % totalFrames;
```

**This is correct.**

### 7.6 Channel Mask Interference

If `spriteChannelMask != 0`, UzEngine extracts a single channel as alpha with
white RGB (lines 255-264). If the mask is incorrectly set to nonzero when using
a full RGBA texture, the color information will be lost.

---

## 8. Complete Data Flow Summary

### UE5 Niagara Sprite Data Flow

```
CPU: NiagaraRendererSprites.cpp
  |-- Fills FNiagaraSpriteUniformParameters:
  |   SubImageSize = (cols, rows, 1/cols, 1/rows)
  |   SubImageBlendMode = bSubImageBlend ? 1 : 0
  |   DefaultColor = (1,1,1,1)
  |   DefaultSubImage = 0.0
  |   ColorDataOffset = offset into particle buffer
  |   SubimageDataOffset = offset into particle buffer
  |
  |-- Sets up instanced draw:
  |   IndexBuffer = GParticleIndexBuffer (6 indices: {0,2,3,0,1,2})
  |   VertexBuffer = GParticleTexCoordVertexBuffer (4 verts: quad UVs)
  |   NumPrimitives = 6/3 = 2 triangles per instance
  |   NumInstances = aliveParticleCount
  |
  v
VS: NiagaraSpriteVertexFactory.ush
  |-- Reads per-particle: position, size, color, rotation, SubImageIndex
  |-- Computes billboard tangents (Right, Up) based on facing mode
  |-- Computes SubUV:
  |     TexCoord.xy = (cellA + baseUV) * (1/cols, 1/rows)  // frame A
  |     TexCoord.zw = (cellB + baseUV) * (1/cols, 1/rows)  // frame B
  |     SubImageLerp = frac(SubImageIndex)
  |-- Outputs to interpolants:
  |     ParticleSubUVs = (TexCoord.xy, TexCoord.zw)
  |     Color = particleColor
  |     SubImageLerp (packed in TangentToWorld0.w)
  |
  v
PS: Material graph + BasePassPixelShader.usf
  |-- ParticleSubUV node:
  |     sampleA = texture.Sample(sampler, SubUVCoords[0])
  |     sampleB = texture.Sample(sampler, SubUVCoords[1])
  |     result = lerp(sampleA, sampleB, SubUVLerp)  // if bBlend
  |-- ParticleColor node:
  |     result = Parameters.Particle.Color (from interpolant)
  |-- Material combines:
  |     EmissiveColor = textureResult.rgb * ParticleColor.rgb
  |     Opacity = textureResult.a * ParticleColor.a
  |
  v
Blend State: (for Translucent)
  Color: SrcAlpha + InvSrcAlpha
  Alpha: Zero + InvSrcAlpha
  Output: float4(EmissiveColor, Opacity)
```

### UzEngine Sprite Data Flow

```
CPU: VFX renderer (C++)
  |-- Fills VFXBillboardCB:
  |   flipbookRows, flipbookCols, flipbookTotalFrames, flipbookFPS
  |   flipbookBlendMode (0=None, 1=Linear, 2=Random)
  |   globalColorTint, emissiveIntensity, softFadeDistance
  |   blendModeHint, spriteChannelMask
  |
  |-- Sets up draw:
  |   DrawIndirect(aliveCount * 6, 1, 0, 0)
  |   No vertex/index buffer — pure SV_VertexID expansion
  |
  v
VS: particle_billboard_vs.hlsl
  |-- Reads per-particle from StructuredBuffers: pos, size, color, rot, vel, age, life
  |-- Computes billboard: cameraRight/Up from invView matrix
  |-- Outputs: positionCS, texcoord (base quad UV), color, age, lifetime, particleIdx
  |-- NOTE: SubUV remapping does NOT happen here — deferred to PS
  |
  v
PS: particle_billboard_ps.hlsl
  |-- Flipbook logic in PS:
  |     ComputeFlipbookFrame(age, lifetime, rows, cols, fps, totalFrames)
  |     uv = SampleFlipbookUV(texcoord, fb.uv0, fb.frameSize)
  |     spriteColor = texture.Sample(sampler, uv)
  |-- If flipbookBlendMode == Linear:
  |     uv1 = SampleFlipbookUV(texcoord, fb.uv1, fb.frameSize)
  |     nextColor = texture.Sample(sampler, uv1)
  |     spriteColor = lerp(spriteColor, nextColor, fb.blendFactor)
  |-- Combine:
  |     finalColor = spriteColor * input.color * colorLifeScale
  |-- Blend mode handling, soft fade, discard, emissive
  |
  v
Blend State: depends on blendModeHint
  0: SrcAlpha / InvSrcAlpha (AlphaBlend)
  1: One / One (Additive)
  2: One / InvSrcAlpha (PremulAlpha)
```

### Key Architectural Differences

| Aspect | UE5 Niagara | UzEngine |
|--------|-------------|----------|
| Quad generation | Instanced draw with VB+IB | SV_VertexID, no VB/IB |
| SubUV UV remap | Vertex shader | Pixel shader |
| Particle data | SRV with offset-based access | StructuredBuffer per attribute |
| SubImage index | Float per-particle attribute | Computed from age*fps in PS |
| Material system | Full material graph | Hardcoded shader |
| SubUV blend | Controlled by SubImageBlendMode uniform | flipbookBlendMode in CB |

---

## 9. Debugging Checklist for UzEngine Sprite Display Issues

1. **Verify texture is loaded**: Check that `spriteTextureIdx != 0xFFFFFFFF`
2. **Verify sRGB**: Sprite textures should use sRGB format for SRV creation
3. **Verify sampler**: Must be CLAMP addressing, not WRAP (prevents atlas bleed)
4. **Verify flipbook params**: flipbookRows, flipbookCols, flipbookTotalFrames > 0
5. **Verify channel mask**: spriteChannelMask should be 0 for RGBA textures
6. **Verify particle color**: DefaultColor should be (1,1,1,1), not (0,0,0,0)
7. **Verify blend state**: Check both color AND alpha channel blend factors
8. **Verify alpha**: If texture has no alpha, particles will be invisible with
   SrcAlpha blending. Either use Additive (One/One) or ensure alpha channel exists.
9. **Verify depth test**: Particles should have DepthWrite=OFF, DepthTest=ON (Less)
10. **Verify age/lifetime**: If age >= lifetime, VS culls the particle (outputs zero)
