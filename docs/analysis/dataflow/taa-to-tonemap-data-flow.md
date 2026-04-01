<!-- doc-meta
title: Data Flow — TAA Output to Tonemap
author: Claude Code
date: 2026-03-31
status: reference
type: data-flow
tags: [temporal-anti-aliasing, tonemap, rendering-pipeline]
-->

# Data Flow: TAA Output → Tonemap

## Complete Pipeline Data Flow Table

| Stage | Reference | Function/File | Input | Output | Type | Format | Purpose |
|-------|-----------|---------------|-------|--------|------|--------|---------|
| **1. Deferred Lighting** | UE5 `BasePass.usf` | DeferredLighting (PS) | GBuffer SRVs + Lights | `SceneColor` | UAV | RGBA32F (HDR) | Per-pixel lighting, HDR |
| **2. Motion Vectors** | UE5 `BasePass.usf` | CalcMotionVector (VS) | WorldPos (current+prev) | `SceneVelocity` | UAV | RG16F | Pixel-space velocity |
| **3. TAA Resolve** | UE5 `PostProcessTemporalAA.usf` | main (CS 8x8) | SceneColor, Velocity, PrevTAA | `TAAOutput` | UAV | RGBA32F (HDR) | Blended history + current |
| **4. Tonemap** | UE5 `PostProcessTonemap.usf` | main (PS fullscreen) | TAAOutput (HDR) | LDR Final | Backbuffer | RGBA8 (LDR) | ACES curve + grading |

## UzEngine Mapping (Current vs Required)

| Stage | UzEngine File | Exists? | Status |
|-------|---------------|---------|--------|
| Deferred Lighting | `deferred_lighting_ps.hlsl` | YES | Outputs to HDR SRV ✓ |
| Motion Vectors | `motion_vectors_cs.hlsl` | NEEDS CHECK | Must output pixel-space velocity |
| TAA Resolve | `temporal_aa_cs.hlsl` | NO | **NEEDS IMPLEMENTATION** |
| Tonemap | `tonemap_ps.hlsl` | YES | Must read from TAA, not direct lighting |

## Critical Data Type Specifications

### SceneColor (Output of Deferred Lighting)

```cpp
// C++ Definition
struct SceneColorCB {
    float3 Radiance;        // HDR lighting result (0-10+)
    float  Unused;
};

// HLSL Float4 Layout
// .rgb = HDR color (can be > 1.0)
// .a   = 1.0 (alpha unused in deferred)
```

**Range**: `[0.0, 10.0]` or higher (physically-based, unbounded)

### SceneVelocity (Output of Motion Vectors Pass)

```cpp
// C++ Definition  
struct VelocityCB {
    float2 PixelVelocity;   // In pixels, not normalized
    float2 Unused;
};

// HLSL Float4 Layout
// .xy = velocity in pixels (camera space)
// .zw = reserved
```

**Range**: `[-ScreenWidth, ScreenWidth]` (can be negative if moving backward)
**Semantics**: Points BACKWARD in time (where pixel came FROM, not where it's GOING)

### TAAOutput (Output of TAA Resolve, Input to Tonemap)

```cpp
// C++ Definition
struct TAAOutputCB {
    float3 Radiance;        // Blended HDR (history + current)
    float  Unused;
};

// HLSL Float4 Layout (same as SceneColor)
// .rgb = HDR color, blended via lerp(history, current, blend)
// .a   = 1.0
```

**Range**: Same as SceneColor `[0.0, 10.0]+`
**Blend Formula**: `Output.rgb = lerp(HistoryColor, CurrentColor, 1.0 - HistoryBlend)`
- `HistoryBlend = 0.85`: 85% history, 15% current
- `HistoryBlend = 0.65`: 65% history, 35% current (fast motion)

## Register and Descriptor Bindings

### Pass: Deferred Lighting (Pixel Shader)

| Register | Resource | Type | Binding | Comment |
|----------|----------|------|---------|---------|
| b0 | SceneConstantBuffer | CBV | View+Projection | Camera matrices |
| b1 | LightDataCB | CBV | Point/Spot lights | Up to 128 lights |
| t0-t4 | GBuffer.Albedo/Normal/Metallic/Roughness/AO | SRV | Read | G-Buffer channels |
| t5 | SkyLight.Cubemap | SRV | Environment | IBL cubemap |
| u0 | SceneColor | UAV | Write | HDR output |
| s0 | BilinearSampler | Sampler | State | For texture sampling |

### Pass: Motion Vectors (Vertex + Pixel Shader)

**Vertex Shader Input**:
```hlsl
struct VSInput {
    float3 Position : POSITION;
    uint VertexId : SV_VertexID;
};
```

**Computed Output**:
```hlsl
// In VS: Transform position
float4 CurrentPos = mul(float4(Position, 1), ViewProj);
float4 PrevPos = mul(float4(Position, 1), PrevViewProj);

// In PS: Compute velocity
float2 Velocity = (CurrentPos.xy / CurrentPos.w - PrevPos.xy / PrevPos.w) * OutputExtent * 0.5;
```

| Register | Resource | Type | Binding | Comment |
|----------|----------|------|---------|---------|
| b0 | ViewProjCB | CBV | Current matrices | View+Projection frame N |
| b1 | PrevViewProjCB | CBV | Previous matrices | View+Projection frame N-1 |
| t0 | GBuffer.WorldPos | SRV | Positions | World position per pixel |
| u0 | SceneVelocity | UAV | Write | Output velocity |

### Pass: TAA Resolve (Compute Shader — 8x8)

| Register | Resource | Type | Binding | Comment |
|----------|----------|------|---------|---------|
| b0 | TAAConstantBuffer | CBV | Parameters | Extent, blend factor, etc. |
| t0 | SceneColor | SRV | Read HDR | Input from deferred |
| t1 | SceneVelocity | SRV | Read Velocity | Pixel-space motion |
| t2 | PrevSceneColor | SRV | Read History | Previous frame TAA output |
| u0 | OutputUAV | UAV | Write | Resolved TAA color |
| s0 | BilinearSampler | Sampler | State | For history reprojection |

**Threadgroup Configuration**:
```cpp
numthreads(8, 8, 1)  // 64 threads per group

Dispatch(
    ceil(Width / 8),    // X groups
    ceil(Height / 8),   // Y groups
    1                   // Z groups
)
```

### Pass: Tonemap (Pixel Shader — Fullscreen)

| Register | Resource | Type | Binding | Comment |
|----------|----------|------|---------|---------|
| b0 | TonemapCB | CBV | Parameters | Exposure, gamma, etc. |
| t0 | SceneColor | SRV | Read HDR | **THIS IS TAAOutput** |
| t1 | ColorGradingLUT | SRV | 3D LUT | Color grading curve |
| t2 | BloomTexture | SRV | Optional | Additive bloom |
| u0 | FinalImage | UAV | Write LDR | Or render target |
| s0 | PointSampler | Sampler | State | For LUT lookup |

## Memory Layout: Constant Buffers

### TAAConstantBuffer (b0 in TAA Compute)

```cpp
struct TAAConstantBuffer {
    uint32_t  OutputWidth;              // Offset 0
    uint32_t  OutputHeight;             // Offset 4
    float     HistoryBlendFactor;       // Offset 8  (0.65 - 0.95)
    float     Sharpness;                // Offset 12 (0.0 - 1.0)
    
    uint32_t  FirstValidFrame;          // Offset 16
    uint32_t  CurrentFrame;             // Offset 20
    float     MotionScaleFactor;        // Offset 24 (for adaptive blend)
    float     _Padding;                 // Offset 28
};
// Total: 32 bytes (2 cache lines)
```

### TonemapCB (b0 in Tonemap Pixel)

```cpp
struct TonemapCB {
    float     Exposure;                 // EV adjustment (-4.0 to 4.0)
    float     Gamma;                    // Typically 2.2
    float     WhitePoint;               // Max brightness before clipping
    float     ContrastScale;            // Curve contrast (0.5 - 2.0)
    
    float3    ColorGradeBias;           // Shadow lift
    float     _Padding1;
    
    float3    ColorGradeGain;           // Highlight boost
    float     _Padding2;
};
// Total: 64 bytes (4 cache lines)
```

## Resource State Transitions (GPU Memory Barriers)

```
After Pass 2 (Motion Vectors):
  SceneColor:      UAV Write → SRV Read
  SceneVelocity:   UAV Write → SRV Read
  (RDG inserts memory barrier)

After Pass 3 (TAA Resolve):
  TAAOutput:       UAV Write → SRV Read
  (RDG inserts memory barrier)

After Pass 4 (Tonemap):
  FinalImage:      UAV Write → Present to Backbuffer
  (Presentation sync barrier)
```

## Texture Format Specifications

| Texture | Format | Channels | Bits/Channel | Range | Why |
|---------|--------|----------|--------------|-------|-----|
| SceneColor | RGBA32F | 4 | 32-bit float | [0, 10+] | Unbounded HDR |
| SceneVelocity | RG16F | 2 | 16-bit float | [-1024, 1024] | Sub-pixel precision |
| PrevSceneColor | RGBA32F | 4 | 32-bit float | [0, 10+] | History HDR |
| TAAOutput | RGBA32F | 4 | 32-bit float | [0, 10+] | Blended HDR |
| ColorGradingLUT | RGBA8 | 4 | 8-bit unorm | [0, 1] | sRGB gamma space |
| FinalImage | RGBA8 | 4 | 8-bit unorm | [0, 1] | LDR, tonemapped |

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────┐
│ Frame N Rendering                                   │
│                                                     │
│ [GBuffer Pass] ──────┐                             │
│                      ▼                             │
│            [Deferred Lighting PS]                  │
│                      │                             │
│                      │ UAV Write:                  │
│                      │ SceneColor(HDR)             │
│                      ▼                             │
│            [Motion Vectors VS+PS]                  │
│                      │                             │
│                      │ UAV Write:                  │
│                      │ SceneVelocity(px)           │
│                      ▼                             │
│ ┌──────────────────────────────────────────┐      │
│ │ [TAA Resolve CS (8x8)]                   │      │
│ │                                          │      │
│ │ Inputs (SRV):                            │      │
│ │  t0: SceneColor (HDR) ─────────────┐    │      │
│ │  t1: SceneVelocity (px) ───────┐   │    │      │
│ │  t2: PrevSceneColor (history) ◄┼┐  │    │      │
│ │                               ││  │    │      │
│ │ Processing:                   ││  │    │      │
│ │  1. Load Current (t0)         ││  │    │      │
│ │  2. Load Velocity (t1)        ││  │    │      │
│ │  3. Reproject: Prev = Curr - Vel ││  │      │
│ │  4. Sample History (t2) @Prev ││  │    │      │
│ │  5. Blend = lerp(H, C, factor) ││  │    │      │
│ │  6. Write to UAV (u0) ◄───────┴┘  │    │      │
│ │                                   │    │      │
│ │ Output (UAV):                      │    │      │
│ │  u0: TAAOutput (HDR, blended) ────┤────┤──┐   │
│ └──────────────────────────────────────────┘ │   │
│                                              │   │
│ ┌────────────────────────────────────────────┤──┐│
│ │ [Tonemap PS Fullscreen]                    │ ││
│ │                                            │ ││
│ │ Inputs (SRV):                              │ ││
│ │  t0: SceneColor ◄─────────────────────────┼─┘│
│ │                 (this is TAAOutput)        │  │
│ │  t1: ColorGradingLUT                       │  │
│ │  t2: BloomTexture                          │  │
│ │                                            │  │
│ │ Processing:                                │  │
│ │  1. Load TAAOutput (HDR) from t0           │  │
│ │  2. Apply ACES curve                       │  │
│ │  3. Sample ColorGrading LUT (t1)           │  │
│ │  4. Add Bloom (t2) if enabled              │  │
│ │  5. Write LDR to backbuffer                │  │
│ │                                            │  │
│ │ Output (Backbuffer):                       │  │
│ │  LDR Final Image (RGBA8) ◄─────────────────┘  │
│ └──────────────────────────────────────────────┘ │
│                                                  │
└──────────────────────────────────────────────────┘

History Ring Buffer Management:
┌─────────────────────────────────┐
│ Frame N-2:  History.RT[i]   (R) │
│ Frame N-1:  History.RT[i+1] (W) │
│ Frame N:    History.RT[i+2] (W) │
│             (rotates modulo 3)   │
└─────────────────────────────────┘
```

## Implementation Checklist for UzEngine

- [ ] **SceneColor path**: Deferred → UAV write → becomes TAAInput SRV
- [ ] **SceneVelocity path**: Motion vectors → UAV write → becomes TAAInput SRV
- [ ] **TAA Compute**: Reads SceneColor + Velocity + PrevTAA → writes TAAOutput UAV
- [ ] **History ring**: 3 buffers, rotates each frame, `History.RT[(frame % 3)]`
- [ ] **First frame branch**: If `FirstValidFrame == CurrentFrame`, bypass history blend
- [ ] **Tonemap input**: Reads from TAAOutput (via SRV), not directly from SceneColor
- [ ] **Register binding**: Verify t0=SceneColor, t1=Velocity, t2=PrevTAA, u0=Output, s0=Sampler
- [ ] **Constant buffers**: TAAConstantBuffer (32B), TonemapCB (64B)
- [ ] **Blend formula**: `lerp(History, Current, 1.0 - HistoryBlend)` where HistoryBlend typically 0.85
- [ ] **Clamp history coords**: No out-of-bounds sampling
- [ ] **RDG state transitions**: UAV Write → SRV Read (automatic, no manual barrier)

## Common Mistakes

| Mistake | Symptom | Fix |
|---------|---------|-----|
| Velocity in normalized space | Ghosting artifacts, wrong reprojection | Store velocity in pixels, not 0-1 range |
| History blend inverted | Temporal flicker, mostly current | Use `lerp(H, C, 1.0 - blend)`, not `lerp(H, C, blend)` |
| No first frame check | Black first frame or crash | Check `FirstValidFrame == CurrentFrame` before history access |
| Tonemap reads SceneColor | TAA never used, jittered image | Connect Tonemap to TAAOutput, not SceneColor |
| Point-sampled history | Blocky reprojection artifacts | Use `SampleLevel()` with bilinear sampler, not `Load()` |
| Single-buffered history | GPU stalls waiting for write | Use 3-buffer ring to avoid GPU-CPU sync |
| Wrong velocity sign | Objects reproject to wrong location | Velocity should point BACKWARD (where pixel came FROM) |

