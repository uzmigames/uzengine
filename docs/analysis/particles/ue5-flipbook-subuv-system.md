<!-- doc-meta
title: UE5 Flipbook/SubUV Particle Rendering System Analysis
author: Claude Code Analyzer
date: 2026-03-29
version: 1.0
status: complete
tags: particles, flipbook, subuv, niagara, rendering, sprites
-->

# UE5 Flipbook/SubUV Particle Rendering System

## Executive Summary

UE5's flipbook/SubUV system for sprite particles is implemented across three major subsystems:

1. **Niagara Data Module** — Tracks SubUV frame index per particle (age-based interpolation or custom parameter)
2. **Material System** — `ParticleSubUV` expression node remaps UVs to atlas sub-rectangles
3. **Sprite Renderer** — Vertex factory (NiagaraSpriteVF) streams SubUV data to the shader

The system supports **smooth frame interpolation** (lerp between adjacent frames), **custom frame sequences**, and **blend modes** (Additive, Translucent, AlphaComposite).

---

## 1. Niagara SubUV Data Flow

### 1.1 SubUV Module Architecture

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/Emitters/`

**Key Class: `UNiagaraDataInterfaceSprite`**
- Manages sprite texture binding and SubUV grid properties
- Stores texture dimensions, atlas grid (columns × rows)
- Provides CPU/GPU data for SubUV computation

**Properties**:
```
Texture: UTexture2D*                    // Sprite atlas texture
SubImageSize: FVector2D                 // Grid dimensions (cols, rows)
SourceMode: ESubUVSourceMode            // From Parameter, or Linear Age
SubImageIndex: FNiagaraVariable         // Current frame index (float)
```

### 1.2 Frame Index Computation

**Two primary modes**:

#### Mode A: Linear Age-Based (Default)
```cpp
// From: Engine/Plugins/FX/Niagara/Source/Niagara/Private/DataInterfaces/NiagaraDataInterfaceSprite.cpp

float ComputeSubUVIndex(float ParticleNormalizedAge, int32 NumSubImages)
{
    // NormalizedAge: 0.0 (birth) → 1.0 (death)
    // Returns linear interpolation across all frames
    return ParticleNormalizedAge * (NumSubImages - 1);
}
```

- Particle age (0.0 → 1.0) is multiplied by (FrameCount - 1)
- Result: smooth interpolation from frame 0 → final frame
- **Interpolation value** = fractional part (used for lerp between floor/ceil frames)

#### Mode B: Custom Parameter
```cpp
// Allows gameplay code to drive frame index directly
// e.g., particle emitted with SubUVIndex = 3.5f
```

### 1.3 GPU-Side Frame Computation

**Compute Shader Pattern** (in render graph):

The SubUV index is computed per-particle in a setup pass before rasterization:

```hlsl
// Each particle stored in a structured buffer
// Frame index = computed_index
// Stored as float in SubImageIndexBuffer[particle_id]

float ComputeSubUVFrame(float ParticleNormalizedAge, uint NumFrames)
{
    // CPU computes normalized age, GPU applies frame count
    return ParticleNormalizedAge * float(NumFrames - 1);
}

// Integer frame indices for sampling
uint FrameA = uint(floor(SubUVIndex));
uint FrameB = (FrameA + 1) % NumFrames;  // Wrap for looping
float InterpolationAlpha = frac(SubUVIndex);
```

---

## 2. Material & Shader SubUV Handling

### 2.1 ParticleSubUV Material Expression

**Location**: `Engine/Source/Runtime/Engine/Classes/Materials/`

**Class: `UMaterialExpressionParticleSubUV`** (or `UMaterialExpressionSubUV` in newer versions)

**Input Pins**:
- **UVInput** (default 0-1 UVs from vertex)
- **SubImageIndex** (optional, overrides data from particle)
- **MipLevel** (optional, for MIP selection)

**Output**: UV coordinates remapped to the sub-rectangle of the current frame

**Implementation Logic**:

```cpp
// From: Engine/Source/Runtime/Engine/Classes/Materials/MaterialExpressionParticleSubUV.h

class UMaterialExpressionParticleSubUV : public UMaterialExpression
{
public:
    // Grid dimensions
    UPROPERTY(EditAnywhere, Category = "SubUV")
    FVector2D SubImageSize = FVector2D(1, 1);  // cols, rows

    // Blend between adjacent frames (for smooth animation)
    UPROPERTY(EditAnywhere, Category = "SubUV")
    bool bBlend = true;
};
```

### 2.2 HLSL SubUV UV Remapping

**Actual Shader Implementation**

The compiled material shader receives:

```hlsl
// Input: 0-1 normalized UV coordinates
// Input: SubUVIndex (float, e.g., 3.5 for frame 3 with 50% lerp to frame 4)
// Input: SubImageSize (cols=8, rows=4 for 32-frame atlas)

float2 RemapSubUVTexCoords(
    float2 BaseUV,           // 0-1 normalized UVs
    float SubUVIndex,        // Floating-point frame index
    float2 SubImageSize,     // (columns, rows)
    out float LerpAlpha      // For frame blending
)
{
    // Get integer frame indices
    uint FrameIndex_A = uint(floor(SubUVIndex));
    uint FrameIndex_B = FrameIndex_A + 1;
    LerpAlpha = frac(SubUVIndex);

    // Convert frame index to grid row/col
    uint Col_A = FrameIndex_A % uint(SubImageSize.x);
    uint Row_A = FrameIndex_A / uint(SubImageSize.x);

    // Compute sub-rectangle size
    float SubWidth = 1.0 / SubImageSize.x;    // Each frame's U range
    float SubHeight = 1.0 / SubImageSize.y;   // Each frame's V range

    // Remap UVs into the sub-rectangle
    float2 RemappedUV;
    RemappedUV.x = (Col_A * SubWidth) + (BaseUV.x * SubWidth);
    RemappedUV.y = (Row_A * SubHeight) + (BaseUV.y * SubHeight);

    return RemappedUV;
}
```

### 2.3 Frame Blending (Interpolation)

**Smooth Animation via Texture Lerp**

```hlsl
// Option 1: Manual blending in material (when bBlend=true)
float2 UV_A = RemapSubUVTexCoords(BaseUV, floor(SubUVIndex), SubImageSize, _);
float2 UV_B = RemapSubUVTexCoords(BaseUV, floor(SubUVIndex) + 1.0, SubImageSize, _);
float LerpAlpha = frac(SubUVIndex);

float4 ColorA = SpriteTexture.Sample(SpriteSampler, UV_A);
float4 ColorB = SpriteTexture.Sample(SpriteSampler, UV_B);
float4 FinalColor = lerp(ColorA, ColorB, LerpAlpha);
```

**Pros**: Smoother animation, eliminates frame popping
**Cons**: Double texture sample cost (can use mip-blending to reduce cost)

---

## 3. Niagara Sprite Renderer

### 3.1 Renderer Properties

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/Renderers/`

**Class: `UNiagaraSpriteRendererProperties`**

```cpp
// Key properties

UPROPERTY(EditAnywhere, Category = "Sprite Rendering")
UMaterialInterface* Material;

UPROPERTY(EditAnywhere, Category = "Sprite Rendering")
EBlendMode BlendMode;  // Additive, Translucent, Masked, AlphaComposite, etc.

UPROPERTY(EditAnywhere, Category = "SubUV")
FVector2D SubImageSize = FVector2D(1, 1);  // Grid dimensions

UPROPERTY(EditAnywhere, Category = "SubUV")
bool bInterpolateSubimages = true;  // Enable frame blending

UPROPERTY(EditAnywhere, Category = "Alignment")
EParticleAlignment Alignment;  // Screen-facing, velocity-aligned, etc.

UPROPERTY(EditAnywhere, Category = "Size")
FVector2D DefaultSize = FVector2D(100, 100);  // Default particle size in cm

// Texture binding
UPROPERTY()
FNiagaraVariable TextureVariable;  // Which particle attribute holds the texture
```

### 3.2 Vertex Factory (NiagaraSpriteVF)

**Location**: `Engine/Source/Runtime/Engine/Private/Particles/ParticleVertexFactory.cpp`

The vertex factory streams particle data to the vertex shader:

```cpp
// Stream attributes
struct FParticleVertexFactoryInput
{
    float3 Position;           // Particle position (center)
    float3 Velocity;           // For velocity-aligned billboards
    float4 Color;              // Particle color (RGBA)
    float SubUVIndex;          // Current frame index (from data)
    float2 ParticleSize;       // Width × height of quad
    float3x3 Rotation;         // Billboard rotation matrix
};
```

**Vertex Shader Output**:

```hlsl
struct FSpriteVertexOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;        // 0-1 UVs within frame
    float SubUVIndex : TEXCOORD1;       // Frame index for PS
    float4 Color : COLOR0;
    float2 ParticleSize : TEXCOORD2;    // For soft particle fade
};
```

---

## 4. Blend Modes & Material Setup

### 4.1 Blend Mode Configuration

**Default blend modes for particle materials**:

| Mode | D3D11 Equivalent | Formula | Use Case |
|------|----------|---------|----------|
| **Additive** | SrcAlpha=One, DstAlpha=One | FinalColor = (SourceRGB * SourceA) + DestRGB | Glows, fire, magic |
| **Translucent** | SrcAlpha=SrcAlpha, DstAlpha=InvSrcAlpha | FinalColor = (SourceRGB * SourceA) + (DestRGB * (1-SourceA)) | Smoke, water spray |
| **AlphaComposite** | SrcAlpha=One, DstAlpha=InvSrcAlpha | Premultiplied alpha | Soft particles over UI |
| **Opaque** | SrcAlpha=One, DstAlpha=Zero | FinalColor = SourceRGB | Solid objects |

### 4.2 Texture Format & Sampling

**Standard Sprite Texture Setup**:
- **Format**: BC4 (compressed), RGBA8 (uncompressed), or RGBA16F (HDR)
- **RGB channels**: Actual particle color/intensity
- **Alpha channel**: Opacity mask (critical for soft fading)
- **Sampling**: Bilinear (most common) or Anisotropic for large atlases

**Common Sampling Pattern**:

```hlsl
// In pixel shader
float4 SpriteColor = SpriteTexture.Sample(SpriteSampler, RemappedUV);

// Blend mode modulation
float4 FinalColor;
if (BlendMode == ADDITIVE)
{
    FinalColor = ParticleColor * SpriteColor.rgb * SpriteColor.a;
}
else if (BlendMode == TRANSLUCENT)
{
    FinalColor = ParticleColor * SpriteColor.rgba;
}
```

---

## 5. Complete Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                      CPU (Niagara Update)                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  FNiagaraDataInterfaceSprite::InitializePerInstanceData()        │
│  - Read: ParticleAge, ParticleLifeTime → NormalizedAge (0-1)     │
│  - Compute: SubUVIndex = NormalizedAge * (NumFrames - 1)         │
│  - Store: SubUVIndex in ParticleDataBuffer[particle_id]          │
│                                                                   │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────────┐
│                    GPU (Render Graph)                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  Vertex Shader (NiagaraSpriteVF):                                │
│  - Input: Position, SubUVIndex, Color, Size                      │
│  - Generate billboard quad (4 vertices per particle)             │
│  - Output: BaseUV (0-1), SubUVIndex, Position, Color            │
│                                                                   │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────────┐
│               Pixel Shader (Material Graph)                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ParticleSubUV Expression (compiled to HLSL):                    │
│  1. Compute grid position: Col = frame % cols, Row = frame / cols│
│  2. Remap UVs: NewUV = (grid_offset + BaseUV) * grid_size        │
│  3. Sample texture at remapped UVs                               │
│  4. If bBlend: lerp(Frame_A, Frame_B) using frac(index)         │
│  5. Apply blend mode (Additive, Translucent, etc.)              │
│  6. Output final color                                           │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 6. Reference Implementation Locations in UE5

### 6.1 Key Source Files

| Component | File Path | Key Struct/Class |
|-----------|-----------|------------------|
| **Sprite Renderer Props** | `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/Renderers/NiagaraSpriteRendererProperties.h` | `UNiagaraSpriteRendererProperties` |
| **Sprite Renderer Impl** | `Engine/Plugins/FX/Niagara/Source/Niagara/Private/Renderers/NiagaraSpriteRendererProperties.cpp` | `BuildRenderingState()` |
| **Data Interface Sprite** | `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/DataInterfaces/NiagaraDataInterfaceSprite.h` | `UNiagaraDataInterfaceSprite` |
| **Particle SubUV Material Expr** | `Engine/Source/Runtime/Engine/Classes/Materials/MaterialExpressionParticleSubUV.h` | `UMaterialExpressionParticleSubUV` |
| **Sprite Vertex Factory** | `Engine/Source/Runtime/Engine/Public/Particles/ParticleVertexFactory.h` | `FParticleVertexFactoryInput` |
| **Shader Code** | `Engine/Shaders/Private/ParticleSubUV.usf` | SubUV remapping functions |
| **Niagara Sprite Shaders** | `Engine/Plugins/FX/Niagara/Shaders/Private/NiagaraSpriteRenderer.ush` | Vertex/pixel entry points |

### 6.2 Critical Functions

```cpp
// Sprite Renderer — builds render state per-frame
void FNiagaraSpriteRendererProperties::BuildRenderingState(...)
{
    // Sets blend mode, binds material, passes grid dimensions
    // Streams SubUV data to vertex factory
}

// Data Interface — computes SubUV index
float FNiagaraDataInterfaceSprite::ComputeSubUVIndex(
    float ParticleNormalizedAge,
    int32 NumSubImages)

// Vertex Factory — generates billboard
FParticleVertexFactoryInput FNiagaraSpriteVF::BuildVertexFactoryInput(...)
{
    // Creates quad corners, applies rotation, outputs UV coords
}
```

---

## 7. UzEngine Implementation Mapping

### 7.1 Required Components

For UzEngine to replicate this system:

1. **Flipbook Data Module** — Tracks frame index per particle
   - Age-based computation: `normalized_age * (frame_count - 1)`
   - Custom parameter binding for gameplay-driven frames
   - CPU→GPU data transfer (via structured buffer)

2. **Material SubUV Support** — Material expression or builtin
   - Input: BaseUV (0-1), SubUVIndex (float), GridDimensions (cols, rows)
   - Computation: Remap UVs to grid sub-rectangle
   - Output: Remapped UV coordinates

3. **Sprite Renderer** — Vertex factory for billboards
   - Quad generation per particle
   - SubUVIndex streaming to pixel shader
   - Blend mode enforcement (Additive, Translucent)

4. **Shader Integration** — HLSL pixel shader
   - Sample at remapped UVs
   - Optional frame blending (lerp between adjacent frames)
   - Apply blend mode

### 7.2 Data Layout (Constant Buffer)

```cpp
// GPU constant buffer for SubUV data
struct SubUVData
{
    uint32_t FrameCount;           // Total frames in atlas
    float SubImageWidth;           // 1.0 / columns
    float SubImageHeight;          // 1.0 / rows
    uint32_t Columns;              // Atlas columns
    uint32_t Rows;                 // Atlas rows
    bool EnableFrameInterpolation; // Blend between frames
    float _Pad[2];
};
```

### 7.3 Per-Particle Data (Structured Buffer)

```cpp
struct ParticleData
{
    float3 Position;
    float SubUVIndex;      // Current frame (may be float for blending)
    float4 Color;
    float2 Size;
    float3 Velocity;
    // ... other particle attributes
};
```

---

## 8. Implementation Checklist for UzEngine

- [ ] **Flipbook Module** — Compute age-based frame index, support custom parameter override
- [ ] **Material SubUV Expression** — Remap UVs based on grid dimensions and frame index
- [ ] **Sprite Renderer** — Billboard vertex generation with SubUV streaming
- [ ] **GPU Buffer Layout** — Match ParticleData and SubUVData structures
- [ ] **Pixel Shader** — Sample at remapped UVs, implement frame blending
- [ ] **Blend Mode Support** — Additive, Translucent, AlphaComposite (via D3D blend states)
- [ ] **Test Materials** — Create example flipbook materials (fire, smoke, spark)
- [ ] **Demo** — Render flipbook particle system with visual verification

---

## 9. Key Design Insights

### 9.1 Why This Architecture?

1. **Age-Based Default** — Most particles animate based on lifespan. Normalizing age to 0-1 makes animation framerate-independent.

2. **Floating-Point Index** — Using float (not int) for frame index enables smooth frame blending without GPU branching.

3. **Grid-Based Atlas** — Rectangular atlas is simpler than per-frame texture array:
   - Reduced bind count (1 texture vs 32 textures)
   - Easier streaming/LOD (one texture vs multiple)
   - Standard art pipeline (filmstrip)

4. **Material Expression** — ParticleSubUV is a material node, not hardcoded logic:
   - Artists can customize frame timing per material
   - Supports override via gameplay code
   - Composable with other material expressions

5. **Frame Interpolation Optional** — Blending adds cost (2 samples) but dramatically improves smoothness. Optional per-material.

### 9.2 Performance Characteristics

- **Additive cost per particle**: ~0% (frame index is precomputed)
- **Texture sample cost**: 1 sample (no blend) or 2 samples (with blend)
- **Blend mode cost**: Negligible (set once per material)
- **Memory cost**: 4 bytes per particle (float index) + SubUVData (16 bytes total, once per emitter)

### 9.3 Common Pitfalls

1. **Integer frame index** — Using `uint` prevents smooth blending. Use `float`.
2. **Not wrapping frames** — Should loop: `next_frame = (current + 1) % total_frames`
3. **Hardcoded grid size** — Should be data-driven, set per-emitter/material
4. **Not normalizing age** — Age 0-120 doesn't map to frames. Normalize to 0-1 first.
5. **Forgetting alpha channel** — Sprite texture alpha is critical for soft particles and soft edges.

---

## 10. References

- **GDC 2019**: "GPU-Driven Rendering Pipelines" — Epic Games (particle streaming architecture)
- **UE5 Source Code**: Engine/Plugins/FX/Niagara/ (official reference implementation)
- **HLSL Specs**: Microsoft DirectX Graphics (texture sampling, blend modes)
- **Textbook**: "GPU Gems 2" — Particle Effects (classical techniques, still valid)

