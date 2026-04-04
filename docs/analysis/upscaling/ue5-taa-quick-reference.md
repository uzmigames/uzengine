<!-- doc-meta
title: UE5 TAA Pipeline — Quick Reference
author: Claude Code
date: 2026-03-31
status: reference
type: quick-reference
tags: [taa, temporal-anti-aliasing, compute, uav, srv]
-->

# UE5 TAA Pipeline — Quick Reference

## Your 5 Questions Answered

### 1. How does the TAA resolve compute shader work? What are the exact inputs and outputs?

**Inputs** (as SRVs):
```
- SceneColorInput      : Texture2D (HDR, 4 channels, float32 each)
- SceneVelocity        : Texture2D (motion vectors, 2 channels, normalized px)
- PrevSceneColorInput  : Texture2D (previous frame TAA output, or first frame buffer)
```

**Outputs** (as UAV):
```
- OutputUAV            : RWTexture2D<float4> (same resolution as inputs)
```

**Algorithm Summary**:
1. Load current pixel color from SceneColorInput
2. Load motion vector from SceneVelocity (in pixels)
3. Reproject previous frame coordinate: `PrevCoord = CurrentPixel - Velocity`
4. Clamp to valid texture bounds
5. Sample history at reprojected location (bilinear filtering)
6. Blend: `Output = lerp(History, Current, BlendFactor)`
7. Write to OutputUAV via UAV store operation

**Thread Group**: 8x8 pixels per threadgroup
**Dispatch**: `ceil(Width/8) × ceil(Height/8) × 1`

---

### 2. How is the UAV output bound for the TAA history write?

**C++ Setup** (in ComputeShader parameter struct):

```cpp
SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputUAV)
```

**Shader Binding**:
```hlsl
RWTexture2D<float4> OutputUAV : register(u0);  // Usually u0 (first UAV)

// Write operation:
OutputUAV[DTid.xy] = float4(FinalColor, 1.0);
```

**RDG Pass Setup**:
```cpp
FRDGTextureRef TAAOutput = GraphBuilder.CreateTexture(
    FRDGTextureDesc::Create2D(
        OutputExtent,
        PF_FloatRGBA,
        FClearValueBinding::Black,
        TexCreate_ShaderResource | TexCreate_UAV  // <-- Must include TexCreate_UAV
    ),
    TEXT("TemporalAA.Output")
);

// Pass parameters bind this texture to the UAV slot
Parameters->OutputUAV = GraphBuilder.CreateUAV(TAAOutput);
```

**History Storage**:
```cpp
// Triple-buffered ring buffer (3 textures, rotates each frame)
struct FTemporalAAHistory {
    IPooledRenderTarget* RT[3];
    uint32 HistoryIndex;  // 0, 1, 2, 0, 1, 2, ...
};

// Each frame:
// - Read from: History.RT[HistoryIndex]
// - Write to: History.RT[(HistoryIndex + 1) % 3]
// - Next frame, HistoryIndex rotates
```

**Why triple-buffered?**
- Frame N: GPU reads buffer[i], writes buffer[i+1]
- Frame N+1: GPU reads buffer[i+1], writes buffer[i+2]
- Frame N+2: GPU reads buffer[i+2], writes buffer[i] (back to start)
- CPU never stalls waiting for GPU to finish a write before reading

---

### 3. How does the first frame work (when there's no valid history)?

**Detection**:
```cpp
// In renderer setup:
if (View.FirstValidFrameNum == View.FrameNumber) {
    // No valid history exists
    bUseHistory = false;
}
```

**Shader Branch**:
```hlsl
if (FirstValidFrame == CurrentFrame) {
    // Path 1: First frame — no history blending
    float4 CurrentColor = SceneColorInput.Load(int3(DTid.xy, 0));
    OutputUAV[DTid.xy] = CurrentColor;
    return;
}

// Path 2: Normal frame — with history blending
// ... reprojection and blending code ...
```

**What resets FirstValidFrame?**
1. Scene load (new world)
2. Camera moved > threshold (game teleport, level portal)
3. Window resized
4. View frustum changed significantly

**Effect**:
- First frame: Direct passthrough of scene color (no temporal artifacts)
- Frames 2+: Full TAA with reprojection and blending
- **Result**: No startup jitter, clean first frame

---

### 4. How does the tonemap shader read from the TAA output?

**Pass 1 (TAA)**: Outputs to `TAAOutput` UAV (still HDR)

**Pass 2 (Tonemap)**: Reads from `TAAOutput` as SRV

**C++ Connection**:
```cpp
// After TAA pass completes:
FRDGTextureRef TAAResolvedColor = AddTemporalAAPass(
    GraphBuilder,
    SceneColor,       // Input
    SceneVelocity,    // Input
    PrevSceneColor,   // Input
    ...
);

// Tonemap consumes TAA output:
AddTonemappingPass(
    GraphBuilder,
    SceneColorInput = TAAResolvedColor,  // <-- Direct SRV binding
    ...
);
```

**Tonemap Shader**:
```hlsl
Texture2D SceneColorInput : register(t0);
SamplerState SceneTextureSampler : register(s0);

float4 main(PixelShaderInput Input) : SV_Target {
    float2 UV = Input.UV;
    
    // Read TAA-resolved HDR color
    float4 HDRColor = SceneColorInput.Sample(SceneTextureSampler, UV);
    
    // Apply tone mapping curve
    float3 TonemappedColor = ApplyACESTonemapping(HDRColor.rgb);
    
    // Apply color grading (LUT lookup, etc.)
    TonemappedColor = ApplyColorGrading(TonemappedColor);
    
    return float4(TonemappedColor, 1.0);
}
```

**Key Detail**: TAA output is **still HDR** when tonemap reads it.
The tone mapping curve is what converts HDR → LDR.

---

### 5. What is the exact SRV/UAV routing chain from HDR scene color → TAA → tonemap?

```
FRAME N:
┌──────────────────────────────────────────────┐
│ 1. G-Buffer Pass                             │
│    Output: GBuffer.Albedo, GBuffer.Normal,   │
│             GBuffer.Metallic, etc. (SRVs)    │
└───────────────┬────────────────────────────┘
                │
                ▼
┌──────────────────────────────────────────────┐
│ 2. Deferred Lighting Pass (Pixel Shader)    │
│    Inputs:                                   │
│      - GBuffer SRVs ◄─────────────────┐      │
│      - PointLight CB (b0)             │      │
│      - SkyLight Cubemap (t5)          │      │
│    Output: SceneColor (HDR) → UAV     │      │
└───────────────┬────────────────────────────┘
                │ (SceneColor UAV is now an SRV for next pass)
                ▼
┌──────────────────────────────────────────────┐
│ 3. Motion Vectors Pass (Pixel/Compute)      │
│    Input: GBuffer.WorldPos (SRV)            │
│    Output: SceneVelocity → UAV              │
│             (velocity in pixel space)        │
└───────────────┬────────────────────────────┘
                │
         ┌──────┴──────┐
         ▼             ▼
    (SRV)         (SRV)
         │             │
┌────────┴─────────────┴──────────────────────┐
│ 4. TAA Resolve Compute Shader                │
│    Inputs (SRV):                            │
│      - SceneColor (t0)                       │
│      - SceneVelocity (t1)                    │
│      - PrevTAAOutput (t2) ◄─────┐           │
│    Output (UAV):                │           │
│      - TAAOutput (u0) ──┐       │           │
│                        │       │           │
│    History Ring:       │       │           │
│      Buffer[0] ────────┼───────┘           │
│      Buffer[1] ◄───────┤ (alternate frames)│
│      Buffer[2] ────────┘                   │
└───────────────┬────────────────────────────┘
                │ (TAAOutput is now an SRV for next pass)
                ▼
┌──────────────────────────────────────────────┐
│ 5. Tonemap Pass (Pixel Shader)              │
│    Inputs (SRV):                            │
│      - SceneColorInput (t0) ◄ TAAOutput     │
│      - ColorGradingLUT (t1)                 │
│      - BloomTexture (t2)                    │
│    Output: LDR Final Image → Backbuffer    │
└──────────────────────────────────────────────┘
```

**Registry Mapping**:

| Register | Pass 2 (Deferred) | Pass 3 (Motion) | Pass 4 (TAA) | Pass 5 (Tonemap) |
|----------|------------------|-----------------|--------------|------------------|
| t0       | GBuffer.Albedo   | GBuffer.WorldPos| SceneColor   | TAAOutput        |
| t1       | GBuffer.Normal   | (unused)        | SceneVelocity| ColorGradingLUT  |
| t2       | (unused)         | (unused)        | PrevTAA      | BloomTexture     |
| t5       | SkyLight.Cube    | (unused)        | (unused)     | (unused)         |
| u0       | SceneColor       | SceneVelocity   | TAAOutput    | (none — final)   |

**UAV → SRV Transition**:
After a UAV write completes, the texture becomes an SRV for the next pass.
RDG handles this automatically via resource state management.

---

## Critical Implementation Details for UzEngine

### Velocity Must Be in Pixels, Not Normalized

**WRONG**:
```hlsl
float2 Velocity = SceneVelocity.Load(...).xy;  // Already in pixels ✓
float2 PrevCoord = PixelCoord - Velocity * OutputExtent;  // WRONG — double-scaling
```

**CORRECT**:
```hlsl
float2 Velocity = SceneVelocity.Load(...).xy;  // In pixels
float2 PrevCoord = PixelCoord - Velocity;      // Direct subtraction
```

### SampleLevel vs Load for Reprojection

**Should use SampleLevel** (bilinear filtering for smoother history):
```hlsl
float4 History = PrevSceneColorInput.SampleLevel(
    BilinearSampler,
    (HistoryCoord + 0.5) / OutputExtent,
    0.0  // LOD 0
);
```

Not Load (which is point-sampled):
```hlsl
float4 History = PrevSceneColorInput.Load(int3(HistoryCoord, 0));  // Blocky
```

### BlendFactor Semantics

Be careful with lerp direction:
```hlsl
// If HistoryBlend = 0.85 (85% history weight):
float3 Output = lerp(History, Current, 1.0 - HistoryBlend);
//                ^^^^^^  ^^^^^^^  ^^^^^^^^^^^^^^^^^^^^^^^^
//                From    To      Interpolant (15% = weight of "To")
// Result: 85% History + 15% Current ✓
```

### First Frame Must Not Access History

```hlsl
if (CurrentFrame == FirstValidFrame) {
    // ❌ DO NOT DO THIS:
    // float4 History = PrevSceneColorInput.Load(...);  // Undefined!
    
    // ✓ DO THIS:
    OutputUAV[DTid.xy] = SceneColorInput.Load(int3(DTid.xy, 0));
    return;
}
```

Accessing PrevSceneColorInput when history is invalid can cause:
- Shader compiler errors (uninitialized descriptor)
- GPU validation errors
- Black frames (history = (0,0,0,1))

---

## RDG Resource State Transitions

**Pass 4 (TAA) writes**: `TAAOutput` as `UAV`
```
ResourceState: ERHIAccess::UAVCompute
```

**Pass 5 (Tonemap) reads**: `TAAOutput` as `SRV`
```
ResourceState: ERHIAccess::SRVMask
```

**RDG automatically inserts** the memory barrier and state transition between passes.
No manual barrier code needed.

---

## Debugging Checklist

- [ ] SceneColorInput actually contains HDR values (0-4+, not 0-1)
- [ ] SceneVelocity in pixel space (not normalized 0-1 range)
- [ ] PrevSceneColorInput is valid history (not black first frame)
- [ ] OutputUAV created with `TexCreate_UAV` flag
- [ ] First frame detection: if no history, output scene color directly
- [ ] Tonemap reads from TAAOutput, not SceneColor
- [ ] Blend factor: 0.85 = 85% history, 15% current
- [ ] No out-of-bounds texture access (clamp reprojected coords)
- [ ] History ring: 3 buffers, rotates each frame, no GPU stall

