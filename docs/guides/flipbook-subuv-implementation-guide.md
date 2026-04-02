<!-- doc-meta
title: Flipbook/SubUV Implementation Guide for UzEngine
author: Claude Code
date: 2026-03-29
version: 1.0
tags: particles, flipbook, subuv, implementation, quick-start
-->

# Flipbook/SubUV Implementation Guide

Quick reference for implementing animated sprite particles in UzEngine, following UE5's proven architecture.

---

## 1. Core Concepts

### Age-Based Frame Animation

```
Age: 0s ──────── 1s ──────── 2s (Lifetime)
      ├────────────────────────┤
      0.0      (normalized)    1.0

Frame: [0] → [1] → [2] → [3] (32 total frames)
Index: 0.0 → 7.75 → 15.5 → 31.0
```

**Key Formula**:
```
NormalizedAge = Age / Lifetime           // 0.0 → 1.0
SubUVIndex = NormalizedAge * (N - 1)    // 0.0 → (N-1)
```

Where `N` = total frames in atlas.

### Grid-Based Atlas

```
Texture (8 columns × 4 rows = 32 frames)
┌──┬──┬──┬──┬──┬──┬──┬──┐
│00│01│02│03│04│05│06│07│ Row 0
├──┼──┼──┼──┼──┼──┼──┼──┤
│08│09│10│11│12│13│14│15│ Row 1
├──┼──┼──┼──┼──┼──┼──┼──┤
│16│17│18│19│20│21│22│23│ Row 2
├──┼──┼──┼──┼──┼──┼──┼──┤
│24│25│26│27│28│29│30│31│ Row 3
└──┴──┴──┴──┴──┴──┴──┴──┘

Frame → Grid: Col = Frame % Cols, Row = Frame / Cols
For Frame 17:  Col = 17 % 8 = 1,  Row = 17 / 8 = 2
```

### Frame Blending (Interpolation)

Smooth animation without discrete frame jumps:

```
Frame 3.5 (50% blend between frame 3 and 4):
  ColorA = Sample(Frame 3)
  ColorB = Sample(Frame 4)
  Result = lerp(ColorA, ColorB, 0.5)
```

---

## 2. Implementation Checklist

### 2.1 CPU-Side (Particle Update)

**File**: `runtime/src/engine/vfx/vfx_particle_system.h`

```cpp
struct Particle
{
    float3 Position;
    float Age;              // Current age (seconds)
    float Lifetime;         // Total lifetime
    float4 Color;
    float2 Size;

    // Add method
    float GetNormalizedAge() const {
        return Lifetime > 0 ? Age / Lifetime : 1.0f;
    }
};
```

### 2.2 SubUV Module

**File**: `runtime/src/engine/vfx/subuv_module.h`

```cpp
struct SubUVModule
{
    uint32_t FrameCount = 1;
    bool bEnableInterpolation = true;
    bool bLooping = true;

    struct Result
    {
        float Index;        // Float frame index (e.g., 3.5)
        uint32_t FrameA;
        uint32_t FrameB;
        float LerpAlpha;
    };

    Result Compute(float NormalizedAge) const
    {
        Result R = {};
        if (FrameCount == 0) return R;

        R.Index = NormalizedAge * (FrameCount - 1);
        R.FrameA = (uint32_t)floor(R.Index);
        R.LerpAlpha = R.Index - R.FrameA;

        if (bLooping)
            R.FrameB = (R.FrameA + 1) % FrameCount;
        else
            R.FrameB = min(R.FrameA + 1, FrameCount - 1);

        return R;
    }
};
```

### 2.3 GPU Buffer Layout

**File**: `runtime/src/engine/vfx/vfx_renderer.h`

```cpp
// Per-particle data (GPU structured buffer)
struct ParticleDataGPU
{
    glm::vec3 Position;         // 12 bytes
    float SubUVIndex;           // 4 bytes (CRITICAL: float, not uint!)
    glm::vec4 Color;            // 16 bytes
    glm::vec2 Size;             // 8 bytes
    glm::vec3 Velocity;         // 12 bytes
    // Total: 52 bytes (pad to 64 for alignment)
};

// Per-emitter metadata (constant buffer)
struct SubUVMetaData
{
    uint32_t FrameCount;
    uint32_t Columns;
    uint32_t Rows;
    float SubImageWidth;        // 1.0 / Columns
    float SubImageHeight;       // 1.0 / Rows
    bool EnableFrameBlending;
    // Total: 32 bytes
};
```

### 2.4 Vertex Shader

**File**: `runtime/shaders/vfx/sprite_billboard_vs.hlsl`

```hlsl
struct VSInput
{
    float2 VertexUV : TEXCOORD0;        // Per-quad corner
    uint InstanceID : SV_InstanceID;
};

StructuredBuffer<ParticleDataGPU> ParticleBuffer : register(t0);

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 BaseUV : TEXCOORD0;          // 0-1 within frame
    float SubUVIndex : TEXCOORD1;       // For material
    float4 Color : COLOR0;
};

VSOutput main(VSInput input)
{
    ParticleDataGPU P = ParticleBuffer[input.InstanceID];

    // Billboard expansion
    float2 Offset = input.VertexUV * 2.0 - 1.0;  // [-1,1]
    float3 WorldPos = P.Position + float3(Offset * P.Size * 0.5, 0.0);

    VSOutput output;
    output.Position = float4(WorldPos, 1.0);
    output.BaseUV = input.VertexUV;
    output.SubUVIndex = P.SubUVIndex;
    output.Color = P.Color;

    return output;
}
```

### 2.5 Pixel Shader

**File**: `runtime/shaders/vfx/sprite_billboard_ps.hlsl`

```hlsl
cbuffer SubUVMetaData : register(b0)
{
    uint FrameCount;
    uint Columns;
    uint Rows;
    float SubImageWidth;
    float SubImageHeight;
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

float2 RemapUV(uint FrameIndex, float2 BaseUV)
{
    uint Col = FrameIndex % Columns;
    uint Row = FrameIndex / Columns;
    float2 Offset = float2(Col, Row) * float2(SubImageWidth, SubImageHeight);
    return Offset + BaseUV * float2(SubImageWidth, SubImageHeight);
}

float4 main(PSInput input) : SV_Target
{
    uint FrameA = uint(floor(input.SubUVIndex));
    uint FrameB = (FrameA + 1) % FrameCount;
    float Alpha = frac(input.SubUVIndex);

    float2 UV_A = RemapUV(FrameA, input.BaseUV);
    float2 UV_B = RemapUV(FrameB, input.BaseUV);

    float4 ColorA = SpriteTexture.Sample(SpriteSampler, UV_A);
    float4 ColorB = SpriteTexture.Sample(SpriteSampler, UV_B);

    float4 SpriteColor = EnableFrameBlending
        ? lerp(ColorA, ColorB, Alpha)
        : ColorA;

    return input.Color * SpriteColor;
}
```

---

## 3. Step-by-Step Integration

### Step 1: Add SubUVModule to Particle System

```cpp
// vfx_particle_system.h
struct Emitter {
    SubUVModule SubUV;  // Add this
    // ... other fields
};
```

### Step 2: Compute SubUV Index During Update

```cpp
// vfx_particle_system.cpp
void UpdateParticles(float DeltaTime)
{
    for (auto& Particle : Particles)
    {
        Particle.Age += DeltaTime;
        float NormalizedAge = Particle.GetNormalizedAge();
        Particle.SubUVIndex = Emitter.SubUV.Compute(NormalizedAge).Index;
    }
}
```

### Step 3: Upload to GPU

```cpp
// In render graph
void UploadParticleData()
{
    for (size_t i = 0; i < NumParticles; ++i)
    {
        GPUData[i].SubUVIndex = Particles[i].SubUVIndex;
        GPUData[i].Position = Particles[i].Position;
        // ... other fields
    }
    RHI->UploadBuffer(ParticleBuffer, GPUData);
}
```

### Step 4: Bind Shaders & Constant Buffer

```cpp
// Render graph pass setup
void SetupSpriteRenderPass()
{
    // Vertex shader
    Pass.SetVertexShader("vfx/sprite_billboard_vs.hlsl");

    // Pixel shader
    Pass.SetPixelShader("vfx/sprite_billboard_ps.hlsl");

    // Bindings
    Pass.SetStructuredBuffer(0, ParticleBuffer);
    Pass.SetConstantBuffer(0, SubUVMetaBuffer);
    Pass.SetTexture(0, SpriteTexture);
    Pass.SetSampler(0, BilinearSampler);

    // Blend mode (Additive for fire, Translucent for smoke)
    Pass.SetBlendMode(BlendMode::Additive);

    // Draw instanced (one draw call for all particles)
    Pass.DrawInstancedQuad(NumParticles);
}
```

---

## 4. Texture Format & Art Requirements

### Texture Specifications

| Property | Value | Reason |
|----------|-------|--------|
| **Format** | BC4 or RGBA8 | BC4 = compressed, RGBA8 = quality |
| **Grid** | 8×4, 4×4, 16×1 (flexible) | Power-of-2 optimal |
| **RGB** | Color/intensity data | What you see |
| **Alpha** | Soft edge mask (0-1) | Critical for soft particles |
| **Mips** | Yes | For distant particles |
| **Filtering** | Bilinear min | Smooth frame transitions |

### Example: Fire Flipbook
```
Frame 0  Frame 1  Frame 2  Frame 3  Frame 4  Frame 5  Frame 6  Frame 7
[core]   [spread] [spread] [fade]   [fade]   [tail]   [tail]   [glow]
8 frames, 1 row (1×8 atlas)
```

### Example: Smoke Flipbook
```
Frame 0  Frame 1  Frame 2  Frame 3
[wispy]  [dense]  [sparse] [dissipate]

Frame 4  Frame 5  Frame 6  Frame 7
[wispy]  [dense]  [sparse] [dissipate]

4 columns × 2 rows (4×2 atlas)
```

---

## 5. Common Mistakes & How to Avoid Them

| Mistake | Problem | Fix |
|---------|---------|-----|
| Using `uint` for frame index | Can't interpolate between frames | Use `float` (fractional part = lerp alpha) |
| Hardcoding grid size | Can't swap textures easily | Make Columns/Rows data-driven |
| Not normalizing age | 0-120 seconds doesn't map to 0-32 frames | Always compute `Age / Lifetime` first |
| Not wrapping frames | Last frame doesn't loop smoothly | Use `(FrameB) % FrameCount` |
| Ignoring texture alpha | Soft edges disappear | Ensure sprite texture has proper alpha |
| Double-sampling both frames unconditionally | Wastes GPU (2× cost) | Make blending optional via constant buffer |

---

## 6. Testing & Validation

### Quick Test

```cpp
// Test SubUV computation
float Age = 0.5f;       // 50% through lifetime
float Lifetime = 1.0f;
uint32_t FrameCount = 32;

float NormAge = Age / Lifetime;  // 0.5
float Index = NormAge * (FrameCount - 1);  // 0.5 * 31 = 15.5

// Expected: Frame 15 with 50% blend to Frame 16
assert(floor(Index) == 15);
assert(frac(Index) == 0.5f);  // ✓
```

### Visual Validation Checklist

- [ ] Particles appear on screen (not invisible)
- [ ] Animation plays smoothly (no frame popping)
- [ ] All 32 frames visible (not just first frame)
- [ ] Looping works (animation repeats)
- [ ] Soft edges work (alpha blending visible)
- [ ] Multiple particles animate independently (not locked to same frame)
- [ ] Performance acceptable (60+ FPS with 1000 particles)

---

## 7. Performance Notes

| Component | Cost | Notes |
|-----------|------|-------|
| **Frame index computation (CPU)** | Negligible | 1 float multiply per particle per frame |
| **Frame index upload (CPU→GPU)** | Negligible | 4 bytes per particle (included in particle buffer) |
| **UV remapping (VS)** | Negligible | Grid math in VS is cheap |
| **Texture sampling (PS)** | ~1-2 ALU cycles | No blending: 1 sample. Blending: 2 samples |
| **Interpolation (PS)** | ~1 lerp instruction | Only if blending enabled |
| **Blend mode (ROP)** | Negligible | Set once per material |

**Total overhead**: ~5-10% vs non-animated sprites (mostly the extra texture sample).

---

## 8. References & Further Reading

- **UE5 Source**: `Engine/Plugins/FX/Niagara/Shaders/` (actual HLSL)
- **GDC 2019**: "GPU-Driven Rendering Pipelines" (particle architecture)
- **GPU Gems 2**: "Particle Effects Using Billboard Sprites" (classical techniques)
- **This Codebase**: `/docs/analysis/particles/ue5-flipbook-subuv-system.md` (detailed reference)

---

## 9. Next Steps

1. **Implement** following the checklist above
2. **Test** with a simple fire flipbook material
3. **Profile** with 1000+ particles to measure cost
4. **Optimize** if needed (frame blending can be toggled)
5. **Document** final result in material/emitter properties

