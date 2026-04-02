<!-- doc-meta
title: Data Flow — Flipbook/SubUV Particle Rendering (CPU→GPU)
author: Claude Code Analyzer
date: 2026-03-29
version: 1.0
tags: particles, flipbook, subuv, dataflow, rendering
-->

# Data Flow: Flipbook/SubUV Particle Rendering

This document traces the complete data flow for animated sprite particles from Niagara emitter through pixel shader output.

---

## Stage-by-Stage Reference

| Stage | Component | File/Function | Input | Output | Purpose |
|-------|-----------|---------------|-------|--------|---------|
| **1. Update** | Niagara Emitter | `FNiagaraEmitterInstance::UpdateFrameTime()` | Particle age, lifetime | `ParticleNormalizedAge` (0-1) | Compute normalized age across all particles |
| **2. Data Module** | SubUV Module | `UNiagaraDataInterfaceSprite::ComputeSubUVIndex()` | `ParticleNormalizedAge`, `FrameCount` | `SubUVIndex` (float) | Convert age to frame index with interpolation |
| **3. Buffer Upload** | Niagara Renderer | `FNiagaraSpriteRendererProperties::BuildRenderingState()` | `SubUVIndex` per particle | `ParticleDataBuffer[SubUVIndex]` | Stream SubUV data to GPU |
| **4. Vertex Shader** | Sprite Vertex Factory | `NiagaraSpriteVF::main()` | `SubUVIndex`, Position, Color, Size | `BaseUV` (0-1), `SubUVIndex` to PS | Generate billboard quad, pass frame index |
| **5. Material Graph** | ParticleSubUV Expression | `ComputeSubUVUVs()` | `BaseUV`, `SubUVIndex`, `GridDims` | `RemappedUV` (to atlas location) | Compute actual texture coordinates |
| **6. Sampling** | Pixel Shader | Material PS | `RemappedUV`, Sprite Texture | `SpriteColor` (RGBA) | Fetch color from atlas at frame location |
| **7. Blend** | Pixel Shader | Material PS | `SpriteColor`, `ParticleColor` | `FinalColor` (RGBA) | Apply blend mode (Add, Alpha, etc.) |
| **8. Output** | Render Target | Device | `FinalColor` | Frame Buffer | Final rendered result |

---

## Detailed Data Structures

### 2.1 Particle Update (CPU)

```cpp
// Source: Niagara emitter instance

struct FParticleData_Update
{
    float Age;              // Current particle age (0 → Lifetime)
    float Lifetime;         // Total lifetime in seconds
    float3 Position;        // World position
    float3 Velocity;        // Velocity vector
    float4 Color;           // RGBA (multiplied by sprite)
    float2 Size;            // Width × height (cm)
};

// Compute normalized age (0 → 1)
float NormalizedAge = Age / Lifetime;  // or: Age / MaxLifetime if clamping
```

**Key Insight**: Age is normalized before any other computation. This decouples animation from framerate.

### 2.2 SubUV Data Module (CPU)

```cpp
// Input: NormalizedAge, FrameCount
// Output: SubUVIndex (float, allows blending)

struct SubUVComputeInput
{
    float NormalizedAge;        // 0.0 → 1.0
    int32_t FrameCount;         // Total frames in atlas
    bool bLooping;              // Whether to loop or clamp to last frame
};

struct SubUVComputeOutput
{
    float SubUVIndex;           // Float frame index (e.g., 3.5 = frame 3 + 50% lerp to frame 4)
    uint32_t FrameA;            // Floor of index
    uint32_t FrameB;            // Ceil of index (with wrapping)
    float InterpolationAlpha;   // Fractional part (0-1)
};

// Computation
float ComputeSubUVIndex(float NormalizedAge, int32_t FrameCount)
{
    return NormalizedAge * (FrameCount - 1);  // Maps [0,1] → [0, FrameCount-1]
}

SubUVComputeOutput result;
result.SubUVIndex = ComputeSubUVIndex(NormalizedAge, FrameCount);
result.FrameA = (uint32_t)floor(result.SubUVIndex);
result.FrameB = (result.FrameA + 1) % FrameCount;  // Wrapping for loops
result.InterpolationAlpha = frac(result.SubUVIndex);
```

**Critical Detail**: SubUVIndex is stored as float, not int. This preserves the fractional part needed for smooth blending.

### 2.3 GPU Upload (CPU→GPU)

```cpp
// Niagara Renderer uploads particle data to structured buffer

struct ParticleDataGPU
{
    float3 Position;            // World position
    float SubUVIndex;           // From SubUV module (float)
    float4 Color;               // RGBA
    float2 Size;                // Width × height
    float3 Velocity;            // For alignment
    // ... other attributes
};

// Constant buffer (once per frame)
struct SubUVMetaData
{
    uint32_t FrameCount;
    uint32_t Columns;           // Atlas grid columns
    uint32_t Rows;              // Atlas grid rows
    float SubImageWidth;        // 1.0 / Columns
    float SubImageHeight;       // 1.0 / Rows
    bool EnableFrameBlending;
    float _Padding[3];
};

// Texture binding
Texture2D SpriteTexture;
SamplerState SpriteSampler;
```

**Buffer Binding**:
- Particle data → Vertex buffer (UAV for compute) or Structured Buffer
- Metadata → Constant Buffer (CB0 or CB_SubUVData)
- Texture → Texture register (t0 or t_SpriteTexture)
- Sampler → Sampler state (s0 or s_Linear)

### 2.4 Vertex Shader (GPU)

```hlsl
// Input: Per-instance particle data
struct VSInput
{
    // Per-vertex (quad corner)
    float2 VertexUV : TEXCOORD0;        // 0-1 within quad (billboard corner)
    uint VertexID : SV_VertexID;

    // Per-instance (from particle buffer)
    float3 ParticlePosition : TEXCOORD1;
    float SubUVIndex : TEXCOORD2;
    float4 ParticleColor : COLOR0;
    float2 ParticleSize : TEXCOORD3;
};

struct VSOutput
{
    float4 Position : SV_POSITION;      // Screen position
    float2 BaseUV : TEXCOORD0;          // 0-1 within frame
    float SubUVIndex : TEXCOORD1;       // For material expression
    float4 Color : COLOR0;               // RGBA
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    // Generate billboard quad (4 verts per particle)
    // VertexID: 0=bottom-left, 1=bottom-right, 2=top-right, 3=top-left
    float2 QuadOffset = float2(
        (input.VertexID % 2) ? 1.0 : -1.0,    // X: ±1
        (input.VertexID / 2) ? 1.0 : -1.0     // Y: ±1
    );

    // Billboard expansion (in screen space or world space)
    float3 BillboardPosition = input.ParticlePosition + QuadOffset * input.ParticleSize * 0.5;

    // Output
    output.Position = float4(BillboardPosition, 1.0);
    output.BaseUV = (QuadOffset + 1.0) * 0.5;  // Convert [-1,1] to [0,1]
    output.SubUVIndex = input.SubUVIndex;
    output.Color = input.ParticleColor;

    return output;
}
```

**Output Invariant**: `BaseUV` is always 0-1 within the frame, `SubUVIndex` is passed to material.

### 2.5 Material/Pixel Shader — SubUV Computation

```hlsl
// Input from vertex shader
struct PSInput
{
    float2 BaseUV : TEXCOORD0;          // 0-1 within frame
    float SubUVIndex : TEXCOORD1;
    float4 Color : COLOR0;
};

// Constant buffer
cbuffer SubUVData : register(b0)
{
    uint FrameCount;
    uint Columns;
    uint Rows;
    float SubImageWidth;        // 1.0 / Columns
    float SubImageHeight;       // 1.0 / Rows
    bool EnableFrameBlending;
};

// Compute remapped UVs for frame A and B
float2 ComputeSubUVCoords(uint FrameIndex, float2 BaseUV)
{
    // Convert frame index to grid position
    uint Col = FrameIndex % Columns;
    uint Row = FrameIndex / Columns;

    // Offset into grid + local UVs
    float2 GridOffset = float2(
        Col * SubImageWidth,
        Row * SubImageHeight
    );

    return GridOffset + BaseUV * float2(SubImageWidth, SubImageHeight);
}

float4 main(PSInput input) : SV_Target
{
    // Get frame indices
    uint FrameA = uint(floor(input.SubUVIndex));
    uint FrameB = (FrameA + 1) % FrameCount;
    float LerpAlpha = frac(input.SubUVIndex);

    // Compute texture coordinates
    float2 UV_A = ComputeSubUVCoords(FrameA, input.BaseUV);
    float2 UV_B = ComputeSubUVCoords(FrameB, input.BaseUV);

    // Sample both frames
    float4 ColorA = SpriteTexture.Sample(SpriteSampler, UV_A);
    float4 ColorB = SpriteTexture.Sample(SpriteSampler, UV_B);

    // Interpolate (if enabled)
    float4 SpriteColor;
    if (EnableFrameBlending)
    {
        SpriteColor = lerp(ColorA, ColorB, LerpAlpha);
    }
    else
    {
        SpriteColor = ColorA;  // Use frame A only
    }

    // Apply particle color (modulation)
    float4 FinalColor = input.Color * SpriteColor;

    return FinalColor;
}
```

**Critical Path**:
1. `BaseUV` + `SubUVIndex` → `UV_A` and `UV_B` (via grid math)
2. Sample at both UVs
3. Lerp results using fractional part of index
4. Multiply by particle color
5. Output to frame buffer

---

## UzEngine Implementation Mapping

### Stage 1: Update → Normalized Age

**Current UzEngine Status**: Particle system exists (VFX system task)

**Changes Needed**:
```cpp
// file: runtime/src/engine/vfx/vfx_particle_system.h

struct ParticleState
{
    float age;                  // Current age (seconds)
    float lifetime;             // Total lifetime (seconds)
    // ... other fields

    // Add:
    float GetNormalizedAge() const
    {
        return (lifetime > 0.0f) ? (age / lifetime) : 1.0f;
    }
};
```

### Stage 2: SubUV Module

**New File**: `runtime/src/engine/vfx/subuv_module.h/.cpp`

```cpp
struct SubUVModuleData
{
    uint32_t FrameCount;        // Total frames in atlas
    bool EnableInterpolation;   // Blend between frames
    bool EnableLooping;         // Loop or clamp to last frame
};

struct ComputedSubUV
{
    float Index;                // Float frame index
    uint32_t FrameA;
    uint32_t FrameB;
    float LerpAlpha;
};

ComputedSubUV ComputeSubUV(
    float NormalizedAge,
    const SubUVModuleData& Data)
{
    ComputedSubUV result = {};

    if (Data.FrameCount == 0) return result;

    result.Index = NormalizedAge * (Data.FrameCount - 1);
    result.FrameA = (uint32_t)floor(result.Index);
    result.LerpAlpha = frac(result.Index);

    if (Data.EnableLooping)
        result.FrameB = (result.FrameA + 1) % Data.FrameCount;
    else
        result.FrameB = min(result.FrameA + 1, Data.FrameCount - 1);

    return result;
}
```

### Stage 3: Buffer Upload

**File**: `runtime/src/engine/vfx/vfx_renderer.cpp`

```cpp
// In render graph setup
void VFXRenderer::BuildRenderGraph(...)
{
    // Create structured buffer for particles
    Buffer ParticleBuffer = RHI->CreateBuffer({
        .Size = NumParticles * sizeof(ParticleDataGPU),
        .Type = BufferType::Structured,
        .Usage = Usage::Dynamic,
    });

    // Fill with computed SubUV indices
    for (size_t i = 0; i < NumParticles; ++i)
    {
        Particle& p = Particles[i];
        float NormalizedAge = p.GetNormalizedAge();
        ComputedSubUV SubUV = ComputeSubUV(NormalizedAge, SubUVModuleData);

        ParticleData[i].SubUVIndex = SubUV.Index;  // Store float
        ParticleData[i].Position = p.Position;
        ParticleData[i].Color = p.Color;
        ParticleData[i].Size = p.Size;
    }

    RHI->UploadBuffer(ParticleBuffer, ParticleData);
}
```

### Stage 4: Vertex Shader

**File**: `runtime/shaders/vfx/sprite_billboard_vs.hlsl`

```hlsl
/// @ref UE5 NiagaraSpriteVF — billboard vertex generation

struct VSInput
{
    float2 VertexUV : TEXCOORD0;        // 0-1 per-quad corner
    uint InstanceID : SV_InstanceID;
};

StructuredBuffer<ParticleDataGPU> ParticleBuffer : register(t0);

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 BaseUV : TEXCOORD0;
    float SubUVIndex : TEXCOORD1;
    float4 Color : COLOR0;
};

VSOutput main(VSInput input)
{
    ParticleDataGPU Particle = ParticleBuffer[input.InstanceID];

    VSOutput output = (VSOutput)0;

    // Billboard quad expansion
    float2 Offset = input.VertexUV * 2.0 - 1.0;  // [-1,1]
    float3 WorldPos = Particle.Position +
        float3(Offset.x, Offset.y, 0.0) * Particle.Size * 0.5;

    output.Position = float4(WorldPos, 1.0);
    output.BaseUV = input.VertexUV;  // [0,1]
    output.SubUVIndex = Particle.SubUVIndex;
    output.Color = Particle.Color;

    return output;
}
```

### Stage 5-7: Pixel Shader + Material

**File**: `runtime/shaders/vfx/sprite_billboard_ps.hlsl`

```hlsl
/// @ref UE5 ParticleSubUV material expression — SubUV remapping + blend

cbuffer SubUVMetaData : register(b0)
{
    uint FrameCount;
    uint Columns;
    uint Rows;
    float SubImageWidth;        // 1.0 / Columns
    float SubImageHeight;       // 1.0 / Rows
    bool EnableFrameBlending;
};

Texture2D SpriteTexture : register(t0);
SamplerState SpriteSampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 BaseUV : TEXCOORD0;
    float SubUVIndex : TEXCOORD1;
    float4 Color : COLOR0;
};

float2 RemapSubUVCoords(uint FrameIndex, float2 BaseUV)
{
    uint Col = FrameIndex % Columns;
    uint Row = FrameIndex / Columns;

    float2 GridOffset = float2(
        Col * SubImageWidth,
        Row * SubImageHeight
    );

    return GridOffset + BaseUV * float2(SubImageWidth, SubImageHeight);
}

float4 main(PSInput input) : SV_Target
{
    // Compute frame indices
    uint FrameA = uint(floor(input.SubUVIndex));
    uint FrameB = (FrameA + 1) % FrameCount;
    float LerpAlpha = frac(input.SubUVIndex);

    // Remap UVs
    float2 UV_A = RemapSubUVCoords(FrameA, input.BaseUV);
    float2 UV_B = RemapSubUVCoords(FrameB, input.BaseUV);

    // Sample
    float4 ColorA = SpriteTexture.Sample(SpriteSampler, UV_A);
    float4 ColorB = SpriteTexture.Sample(SpriteSampler, UV_B);

    // Interpolate
    float4 SpriteColor = EnableFrameBlending
        ? lerp(ColorA, ColorB, LerpAlpha)
        : ColorA;

    // Apply particle color + blend mode
    float4 FinalColor = input.Color * SpriteColor;

    return FinalColor;
}
```

---

## Constant Buffer Layout (GPU)

```cpp
// SubUVMetaData — 32 bytes (CB alignment)
struct SubUVMetaData
{
    uint32_t FrameCount;        // Offset 0 (4 bytes)
    uint32_t Columns;           // Offset 4 (4 bytes)
    uint32_t Rows;              // Offset 8 (4 bytes)
    uint32_t _Padding1;         // Offset 12 (4 bytes)
    float SubImageWidth;        // Offset 16 (4 bytes)
    float SubImageHeight;       // Offset 20 (4 bytes)
    uint32_t EnableFrameBlending; // Offset 24 (4 bytes)
    uint32_t _Padding2;         // Offset 28 (4 bytes)
};
```

**Register Binding**: `register(b0)` (per-material constant buffer)

---

## Summary: Complete Pipeline

```
Input:
  - Particle.Age (float, seconds)
  - Particle.Lifetime (float, seconds)
  - Emitter.FrameCount (uint, from material)
  - Emitter.EnableBlending (bool, from material)

Step 1: NormalizedAge = Age / Lifetime
Step 2: SubUVIndex = NormalizedAge * (FrameCount - 1)  [CPU]
Step 3: SubUVIndex uploaded to GPU (one float per particle)
Step 4: VS generates billboard quad, passes SubUVIndex to PS
Step 5: PS computes FrameA/B, LerpAlpha from SubUVIndex
Step 6: PS remaps UVs: UV_A = grid_offset_A + BaseUV * grid_size
Step 7: PS samples both frames, lerps if enabled
Step 8: PS applies particle color and blend mode
Step 9: Output to frame buffer

Output:
  - Frame buffer with animated sprite particle at correct frame
```

This completes the full data flow from emitter update through final pixel output.

