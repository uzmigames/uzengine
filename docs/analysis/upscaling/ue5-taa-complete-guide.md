<!-- doc-meta
title: UE5 TAA Complete Implementation Guide
author: Claude Code
date: 2026-03-31
status: reference
type: implementation-guide
tags: [temporal-anti-aliasing, taa, compute, uav, srv, implementation]
-->

# UE5 TAA Complete Implementation Guide

**READ THIS FIRST if you're implementing TAA in UzEngine.**

This document synthesizes all TAA analysis into a step-by-step implementation guide.

---

## Your 5 Questions — Answered

### 1. How does the TAA resolve compute shader work?

**The Algorithm (High-Level)**:
```
For each pixel (x, y):
  1. Load current HDR color from SceneColorInput[x, y]
  2. Load velocity from SceneVelocity[x, y]  (in pixels)
  3. Compute history coordinate: prev_coord = (x, y) - velocity
  4. Clamp to texture bounds: prev_coord = clamp(prev_coord, 0, size-1)
  5. Sample history (bilinear): history_color = SampleLevel(PrevTAA, prev_coord, 0)
  6. Blend: output = lerp(history_color, current_color, blend_factor)
  7. Write to output UAV: OutputUAV[x, y] = output
```

**Key Details**:
- Thread group size: 8x8 pixels (64 threads)
- Dispatch: `ceil(width/8) × ceil(height/8) × 1`
- All calculations in HDR float space (RGBA32F)
- Velocity is in **pixels**, not normalized 0-1 range
- Blend factor typically 0.85 (85% history, 15% current)

### 2. How is the UAV output bound?

**C++ (Pass Setup)**:
```cpp
// Create output texture with UAV capability
FRDGTextureRef TAAOutput = GraphBuilder.CreateTexture(
    FRDGTextureDesc::Create2D(
        OutputExtent,
        PF_FloatRGBA,
        FClearValueBinding::Black,
        TexCreate_ShaderResource | TexCreate_UAV  // <-- CRITICAL: UAV flag
    ),
    TEXT("TemporalAA.Output")
);

// Bind to compute shader
Parameters->OutputUAV = GraphBuilder.CreateUAV(TAAOutput);
```

**Shader (HLSL)**:
```hlsl
RWTexture2D<float4> OutputUAV : register(u0);

[numthreads(8,8,1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 PixelCoord = DTid.xy;
    float4 OutputColor = float4(FinalColor, 1.0);
    OutputUAV[PixelCoord] = OutputColor;  // Direct UAV write
}
```

**History Ring Buffer**:
```cpp
// Triple-buffered to avoid GPU stalls
struct FTemporalAAHistory {
    IPooledRenderTarget* RT[3];
    uint32 HistoryIndex;
};

// Each frame:
// Frame N: Read from RT[N % 3], Write to RT[(N+1) % 3]
// Frame N+1: Read from RT[(N+1) % 3], Write to RT[(N+2) % 3]
// Frame N+2: Read from RT[(N+2) % 3], Write to RT[N % 3] (back to start)
```

### 3. How does first frame work?

**Detection (C++)**:
```cpp
// When does history become invalid?
if (View.FirstValidFrameNum == View.FrameNumber ||
    CameraMovedTooFar ||
    WindowResized ||
    SceneChanged) {
    View.FirstValidFrameNum = View.FrameNumber;
    TAAHistory.Reset();
}
```

**Shader (HLSL)**:
```hlsl
if (FirstValidFrame == CurrentFrame) {
    // No history — direct passthrough
    float4 CurrentColor = SceneColorInput.Load(int3(DTid.xy, 0));
    OutputUAV[DTid.xy] = CurrentColor;
    return;  // <-- CRITICAL: Don't access history texture
}

// Normal path with history blending
// ...
```

**Why Not Blend First Frame?**
- No valid reprojection data
- Accessing uninitialized history buffer causes artifacts
- Direct passthrough ensures clean transition
- TAA kicks in from frame 2 onward

### 4. How does tonemap read from TAA output?

**Pass 1 (TAA)**: Writes to `TAAOutput` (UAV, HDR)
**Pass 2 (Tonemap)**: Reads from `TAAOutput` (SRV, HDR)

**C++ Connection (RDG)**:
```cpp
// TAA pass creates and returns output
FRDGTextureRef TAAResolvedColor = AddTemporalAAPass(
    GraphBuilder,
    SceneColor,
    SceneVelocity,
    PrevSceneColor,
    ...
);

// Tonemap consumes it directly
AddTonemappingPass(
    GraphBuilder,
    SceneColorInput = TAAResolvedColor,  // <-- Direct SRV binding
    ...
);
```

**Shader (HLSL)**:
```hlsl
Texture2D SceneColorInput : register(t0);  // Connected to TAAResolvedColor

float4 main(PixelShaderInput Input) : SV_Target {
    float2 UV = Input.UV;
    
    // Read TAA-resolved HDR color
    float4 HDRColor = SceneColorInput.Sample(SceneTextureSampler, UV);
    
    // Apply ACES tone mapping
    float3 TonemappedColor = ApplyACESCurve(HDRColor.rgb);
    
    // Apply color grading
    TonemappedColor = ApplyColorGrading(TonemappedColor);
    
    return float4(TonemappedColor, 1.0);
}
```

**Key Detail**: TAA output is **still HDR** when tonemap reads it.
Tonemap converts HDR → LDR.

### 5. What is the SRV/UAV routing chain?

```
PASS 1: Deferred Lighting
  Input:   GBuffer SRVs (t0-t4), Sky Cubemap (t5)
  Output:  SceneColor → UAV (u0)

PASS 2: Motion Vectors
  Input:   WorldPos from GBuffer (t0)
  Output:  SceneVelocity → UAV (u0)

PASS 3: TAA Resolve
  Input:   SceneColor (t0, SRV)
           SceneVelocity (t1, SRV)
           PrevTAA (t2, SRV) ◄── History
  Output:  TAAOutput → UAV (u0)

PASS 4: Tonemap
  Input:   TAAOutput (t0, SRV) ◄── Connected from TAA output UAV
           ColorGrading LUT (t1, SRV)
           Bloom (t2, SRV)
  Output:  LDR Final → Backbuffer
```

**Resource State Transitions** (RDG automatic):
```
SceneColor:       UAV Write (Pass 1) → SRV Read (Pass 3)
SceneVelocity:    UAV Write (Pass 2) → SRV Read (Pass 3)
TAAOutput:        UAV Write (Pass 3) → SRV Read (Pass 4)
```

---

## Critical Implementation Checklist

### Pass 1: Deferred Lighting
- [ ] Outputs HDR SceneColor to UAV
- [ ] Blend mode: Opaque (no blending)
- [ ] Output extent matches render resolution

### Pass 2: Motion Vectors
- [ ] Computes velocity: `(CurrentPos - PrevPos) * Extent * 0.5`
- [ ] Outputs **pixel-space** velocity (NOT normalized)
- [ ] Velocity points BACKWARD in time (where pixel came FROM)
- [ ] Same extent as SceneColor

### Pass 3: TAA Resolve
- [ ] Create TAAOutput texture with `TexCreate_UAV` flag
- [ ] Bind SceneColor as t0 SRV
- [ ] Bind SceneVelocity as t1 SRV
- [ ] Bind PrevTAA as t2 SRV (or black first frame)
- [ ] Bind OutputUAV as u0
- [ ] Threadgroup: 8x8 pixels
- [ ] Dispatch: `ceil(width/8) × ceil(height/8) × 1`
- [ ] First frame check: if no history, output scene color directly
- [ ] Reproject: `prev_coord = current_coord - velocity`
- [ ] Clamp: `prev_coord = clamp(prev_coord, 0, extent-1)`
- [ ] Sample history with bilinear (SampleLevel, not Load)
- [ ] Blend: `output = lerp(history, current, 1.0 - blend_factor)`
- [ ] Optional: Sharpening filter
- [ ] Write to UAV before returning

### Pass 4: Tonemap
- [ ] Read SceneColorInput from TAA output (SRV)
- [ ] Apply tone mapping curve (ACES)
- [ ] Apply color grading
- [ ] Output LDR to backbuffer

### History Management
- [ ] Allocate 3 buffers (triple-buffered ring)
- [ ] Rotate index each frame: `(index + 1) % 3`
- [ ] Read from `RT[index]`, write to `RT[(index+1)%3]`
- [ ] Reset history when camera moves, scene changes, or window resizes

### Constant Buffers
- [ ] TAAConstantBuffer (32B):
  - OutputWidth, OutputHeight (uint32 × 2)
  - HistoryBlendFactor (float, typically 0.85)
  - Sharpness (float, 0.0-1.0)
  - FirstValidFrame, CurrentFrame (uint32 × 2)
  - MotionScaleFactor (float, for adaptive blend)

### Samplers
- [ ] BilinearSampler (for history reprojection)
  - Address mode: Clamp
  - Filter: Bilinear
  - No anisotropic filtering needed

---

## Common Mistakes & Fixes

### Mistake 1: Velocity in Normalized Space
**Symptom**: Objects appear ghosted, motion blur wrong direction
**Root Cause**: Velocity stored as 0-1 normalized, then used directly as pixels
**Fix**: Store velocity in pixel space
```hlsl
// WRONG
float2 Velocity = VelocityTexture.Load(...).xy * OutputExtent;

// CORRECT
float2 Velocity = VelocityTexture.Load(...).xy;  // Already in pixels
```

### Mistake 2: Inverted Blend Factor
**Symptom**: Temporal flicker, mostly current frame, no history accumulation
**Root Cause**: Blend factor semantics reversed
**Fix**: Use correct lerp direction
```hlsl
// WRONG
Output = lerp(History, Current, BlendFactor);  // BlendFactor weights "Current"

// CORRECT
Output = lerp(History, Current, 1.0 - BlendFactor);  // BlendFactor weights "History"
// If BlendFactor = 0.85: 85% History + 15% Current
```

### Mistake 3: Accessing History on First Frame
**Symptom**: Black first frame, NaN values, shader validation errors
**Root Cause**: Trying to read uninitialized history buffer
**Fix**: Check first frame condition
```hlsl
// WRONG
float4 History = PrevTAA.Load(...);  // Undefined on first frame!

// CORRECT
if (FirstValidFrame == CurrentFrame) {
    OutputUAV[DTid.xy] = SceneColor.Load(...);
    return;
}
// Only access history here (after first frame check)
```

### Mistake 4: Point-Sampling History
**Symptom**: Blocky, aliased history reprojection
**Root Cause**: Using Load() instead of SampleLevel()
**Fix**: Use bilinear sampling
```hlsl
// WRONG
float4 History = PrevTAA.Load(int3(HistoryCoord, 0));

// CORRECT
float4 History = PrevTAA.SampleLevel(BilinearSampler, HistoryUV, 0.0);
```

### Mistake 5: Tonemap Reads Direct Scene Color
**Symptom**: Jittered output (camera jitter visible), TAA ineffective
**Root Cause**: Tonemap reads from SceneColor UAV, not TAA output
**Fix**: Wire tonemap to TAA output
```cpp
// WRONG
AddTonemappingPass(GraphBuilder, SceneColorInput = SceneColor, ...);

// CORRECT
AddTonemappingPass(GraphBuilder, SceneColorInput = TAAOutput, ...);
```

### Mistake 6: Single-Buffered History
**Symptom**: GPU stalls, frame rate drops every 2-3 frames
**Root Cause**: Reading from buffer while GPU is still writing to it
**Fix**: Use triple-buffered ring
```cpp
// WRONG
FRDGTextureRef History = HistoryBuffer;  // Same buffer each frame

// CORRECT
FRDGTextureRef History = HistoryRing[FrameIndex % 3];  // Rotates
```

### Mistake 7: Wrong Velocity Sign
**Symptom**: Objects reproject to wrong locations, ghosting in wrong direction
**Root Cause**: Velocity points forward in time instead of backward
**Fix**: Velocity = Current - Previous (not Previous - Current)
```hlsl
// In motion vector shader:
float2 CurrentScreenPos = (CurrentPos.xy / CurrentPos.w) * ViewportSize;
float2 PrevScreenPos = (PrevPos.xy / PrevPos.w) * ViewportSize;
float2 Velocity = CurrentScreenPos - PrevScreenPos;  // Points forward
// Then in TAA: PrevCoord = CurrentCoord - Velocity  // Corrects to backward
```

---

## Step-by-Step Implementation for UzEngine

### Step 1: Verify Motion Vector Pass
```
Goal: Produce pixel-space velocity buffer
- Read: WorldPos from GBuffer
- Compute: (CurrentScreenPos - PrevScreenPos) in pixels
- Verify: Velocity ranges -2048 to +2048 (for typical 2K render)
- Write: SceneVelocity UAV
```

### Step 2: Create TAA Compute Shader
```
File: shaders/runtime/passes/temporal_aa_cs.hlsl
- Header: Include TAAConstantBuffer definition
- Entry: [numthreads(8,8,1)] void main(uint3 DTid)
- Inputs: t0=SceneColor, t1=SceneVelocity, t2=PrevTAA, s0=BilinearSampler
- Output: u0=OutputUAV
- First frame branch: if no history, output scene color
- Reprojection: prev_coord = current_coord - velocity
- Sampling: bilinear fetch from prev_coord
- Blending: lerp(history, current, 1-blend)
- Write: UAV store
```

### Step 3: Wire RDG Pass
```cpp
FRDGTextureRef TAAOutput = GraphBuilder.CreateTexture(
    FRDGTextureDesc::Create2D(
        Extent,
        PF_FloatRGBA,
        FClearValueBinding::Black,
        TexCreate_ShaderResource | TexCreate_UAV
    ),
    TEXT("TAA.Output")
);

FComputeShaderUtils::AddPass(
    GraphBuilder,
    RDG_EVENT_NAME("TemporalAA"),
    ComputeShader,
    Parameters,
    FIntPoint(
        FMath::DivideAndRoundUp(Extent.x, 8),
        FMath::DivideAndRoundUp(Extent.y, 8)
    )
);

return TAAOutput;
```

### Step 4: Wire Tonemap to TAA Output
```cpp
// Before:
AddTonemappingPass(GraphBuilder, SceneColorInput = SceneColor, ...);

// After:
FRDGTextureRef TAAOutput = AddTemporalAAPass(GraphBuilder, ...);
AddTonemappingPass(GraphBuilder, SceneColorInput = TAAOutput, ...);
```

### Step 5: Allocate History Ring
```cpp
struct FTemporalAAHistory {
    TRefCountPtr<IPooledRenderTarget> RT[3];
    uint32 HistoryIndex = 0;
    
    void Reset() {
        for (int i = 0; i < 3; ++i) RT[i] = nullptr;
        HistoryIndex = 0;
    }
};

// In per-view data:
FTemporalAAHistory TAAHistory;
```

### Step 6: Update History Ring Each Frame
```cpp
if (View.FirstValidFrameNum == View.FrameNumber) {
    // First frame: no history
    PrevTAABuffer = CreateBlackBuffer();
} else {
    // Use previous frame's output as history
    PrevTAABuffer = TAAHistory.RT[TAAHistory.HistoryIndex];
}

// After TAA pass:
uint32 NextIndex = (TAAHistory.HistoryIndex + 1) % 3;
TAAHistory.RT[NextIndex] = TAAOutput;
TAAHistory.HistoryIndex = NextIndex;
```

### Step 7: Test & Debug
```bash
# First: Verify motion vectors
uzengine.exe --vis scene_velocity --screenshot-stdout --frames 8

# Second: Verify TAA is reading them
uzengine.exe --vis taa_output --screenshot-stdout --frames 8

# Third: Full render to compare with/without TAA
uzengine.exe --pass-toggle temporal-aa --screenshot-stdout --frames 120

# Fourth: Check blend factors
uzengine.exe --cvar taa_history_blend=0.65 --screenshot-stdout --frames 120
```

---

## Validation Checklist

After implementation, verify:

- [ ] First frame is clean (no artifacts)
- [ ] Subsequent frames are stable (no flicker)
- [ ] Motion causes smooth reprojection (no ghosting)
- [ ] Static scene converges (noise reduces over frames)
- [ ] Fast camera motion doesn't cause ghosting
- [ ] No out-of-bounds crashes
- [ ] History ring prevents GPU stalls
- [ ] Tonemap receives TAA output (not direct scene color)
- [ ] Sharpness slider (if implemented) works smoothly
- [ ] Blend factor tuning changes temporal weight correctly

---

## Performance Notes

**GPU Time**: ~1.0-1.5ms for 2K TAA compute (8x8 threadgroups, 8 samples per pixel)

**Memory**: 3× output extent in float4 format
- 1440p: 3 × 1440×2560×16 bytes = 177 MB (acceptable)
- 4K: 3 × 3840×2160×16 bytes = 1.6 GB (watch for VRAM pressure)

**Bandwidth**: Moderate (each pixel reads 3-4 textures, writes 1)

---

## References

- `ue5-taa-pipeline-architecture.md` — Full end-to-end architecture
- `ue5-taa-quick-reference.md` — Quick reference + debugging
- `taa-to-tonemap-data-flow.md` — Detailed data flow table (register bindings, formats, ranges)

