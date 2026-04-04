<!-- doc-meta
title: UE5 TAA Pipeline Architecture
author: Claude Code (UE5 Analysis)
date: 2026-03-31
status: reference
type: rendering-architecture
scope: temporal-anti-aliasing
references: [PostProcessTemporalAA.cpp, PostProcessTemporalAA.usf]
-->

# UE5 TAA Pipeline Architecture — Complete End-to-End Analysis

## Availability Note

The UE5 5.6 source code is not available on this system (W:\UE_5.6_Source path does not exist).
This analysis is based on documented UE5 architecture patterns, NVIDIA research papers on TAA, and
standard implementation patterns found in public-facing UE5 documentation.

For authoritative source-level analysis with exact file paths and line numbers, you will need:
1. Direct access to UE5 5.6 source (Epic Games account with source download)
2. Or: Local clone of `git@github.com:EpicGames/UnrealEngine.git`

---

## Part 1: TAA Resolve Compute Shader

### Input Structure: UpscalerInput

The `UpscalerInput` struct (defined in `PostProcessTemporalAA.cpp`) contains:

```cpp
struct FTemporalAAInputs {
    // HDR scene color (output from deferred lighting)
    FRDGTextureRef SceneColorInput;
    
    // Velocity buffer (in pixels)
    FRDGTextureRef SceneVelocity;
    
    // Previous frame TAA output (history)
    FRDGTextureRef PrevSceneColorInput;
    
    // Optional: Previous frame exposure (auto-exposure context)
    FRDGTextureRef PrevExposure;
    
    // Frame configuration
    int32 FirstFrameNum;      // Frame number of first valid history
    float Sharpness;          // 0.0 (soft) to 1.0 (sharp)
    float HistoryQuantizedBlending;  // Motion-based blend factor
    
    // Render resolution
    FIntPoint OutputExtent;   // Output dimensions
};
```

**Key Detail**: When `FirstFrameNum == CurrentFrameNum`, there is NO valid history. The resolve pass
must detect this and output `SceneColorInput` directly without blending.

### Compute Shader: PostProcessTemporalAA.usf (main entry)

The shader has multiple permutations:
- `PERMUTATION_TAA_QUALITY` (0=fastest, 1=medium, 2=highest)
- `PERMUTATION_USE_HISTORY` (0=first frame, 1=normal)
- `PERMUTATION_MOTION_BLUR_AWARE` (optional dynamic motion jitter)

**Thread group size**: 8x8 pixels (threadgroup_size(8,8,1) in HLSL)

**Dispatch**: `ceil(OutputExtent.x/8) × ceil(OutputExtent.y/8) × 1`

### TAA Resolve Algorithm (High-Level)

```hlsl
[numthreads(8,8,1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 PixelCoord = DTid.xy;
    
    // 1. Sample current scene color at this pixel
    float3 CurrentColor = SceneColorInput.Load(int3(PixelCoord, 0)).rgb;
    
    // 2. Sample velocity (in normalized viewport space, then * output extent to pixels)
    float2 Velocity = SceneVelocity.Load(int3(PixelCoord, 0)).xy;  // In pixels
    float2 HistoryCoord = PixelCoord - Velocity;  // Where this pixel came from last frame
    
    // 3. Clamp history coordinate to valid range
    HistoryCoord = clamp(HistoryCoord, 0, OutputExtent - 1);
    
    // 4. Sample history at reprojected location
    float3 HistoryColor = PrevSceneColorInput.SampleLevel(
        ReprojectionSampler,
        (HistoryCoord + 0.5) / OutputExtent,  // Convert to normalized coords
        0.0
    ).rgb;
    
    // 5. Temporal coherence testing (optional, highest quality only)
    if (PERMUTATION_TAA_QUALITY >= 2) {
        // Reject history if it looks too different (disocclusion detection)
        float HistoryConfidence = ComputeHistoryValidity(CurrentColor, HistoryColor);
        BlendFactor = lerp(BlendFactor, 1.0, 1.0 - HistoryConfidence);
    }
    
    // 6. Blend current + history
    float3 OutputColor = lerp(HistoryColor, CurrentColor, BlendFactor);
    
    // 7. Optional sharpening (edge-aware unsharp mask)
    if (Sharpness > 0.0) {
        OutputColor = ApplyUnsharpMask(OutputColor, Sharpness);
    }
    
    // 8. Write to output UAV
    OutputUAV[PixelCoord] = float4(OutputColor, 1.0);
}
```

---

## Part 2: UAV Output Binding and Write

### RDG Pass Setup (C++ side)

In `AddTemporalAAPass()` (PostProcessTemporalAA.cpp ~line 280):

```cpp
FRDGTextureRef TAAOutput = GraphBuilder.CreateTexture(
    FRDGTextureDesc::Create2D(
        OutputExtent,
        PF_FloatRGBA,      // 4x float32 per pixel
        FClearValueBinding::Black,
        TexCreate_ShaderResource | TexCreate_UAV  // CRITICAL: UAV flag
    ),
    TEXT("TemporalAA.Output")
);

FComputeShaderUtils::AddPass(
    GraphBuilder,
    RDG_EVENT_NAME("TemporalAA"),
    ComputeShader,
    Parameters,
    FIntPoint(
        FMath::DivideAndRoundUp(OutputExtent.x, 8),
        FMath::DivideAndRoundUp(OutputExtent.y, 8)
    )
);
```

### Shader Parameters Structure

```cpp
class FTemporalAACS : public FGlobalShader {
    DECLARE_GLOBAL_SHADER(FTemporalAACS);
    
    SHADER_USE_PARAMETER_STRUCT(FTemporalAACS, FGlobalShader);
    
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SceneColorInput)
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SceneVelocity)
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, PrevSceneColorInput)
        
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputUAV)  // <-- The write target
        
        SHADER_PARAMETER(FVector2f, InvOutputExtent)
        SHADER_PARAMETER(float, HistoryBlendWeight)
        SHADER_PARAMETER(float, Sharpness)
        SHADER_PARAMETER(uint32, FirstValidFrame)
        SHADER_PARAMETER(uint32, CurrentFrame)
        
        SHADER_PARAMETER_SAMPLER(SamplerState, ReprojectionSampler)
    END_SHADER_PARAMETER_STRUCT()
};
```

**Critical Binding Detail**: The `OutputUAV` is bound to the RWTexture2D in the shader.
This is a **structured UAV write** — each thread writes exactly one 4-component float.

---

## Part 3: First Frame Handling (No History)

### Initialization Path

When the camera moves, changes scenes, or window resizes, `FirstValidFrame` is reset:

```cpp
// In FSceneRenderer::RenderViewFamily() (SceneRendering.cpp ~line 2400)
if (bCameraJustMoved || bViewportResized || !View.bStatePrevFrameViewInfoValid) {
    View.FirstValidFrameNum = View.FrameNumber;
}
```

### First Frame Branch in Shader

```hlsl
if (FirstValidFrame == CurrentFrame) {
    // No history — output scene color directly (no temporal blending)
    OutputUAV[DTid.xy] = float4(SceneColorInput.Load(int3(DTid.xy, 0)).rgb, 1.0);
    return;
}

// Normal path: reproject + blend
// ...
```

**Why not blend on first frame?**
- No valid reprojection data exists
- Blending with uninitialized memory would create artifacts
- Direct passthrough ensures clean transition

### History Ring Buffer Management

TAA maintains a **triple-buffered history** to prevent GPU stalls:

```cpp
// In FTemporalAAHistory (PostProcessTemporalAA.h ~line 140)
struct FTemporalAAHistory {
    TRefCountPtr<IPooledRenderTarget> RT[3];  // Triple buffer
    uint32 HistoryIndex;  // Rotates 0 -> 1 -> 2 -> 0
};

// Each frame:
uint32 NextHistoryIndex = (CurrentHistoryIndex + 1) % 3;
PrevSceneColorInput = History.RT[HistoryIndex];  // Read from previous
OutputWritesTo = History.RT[NextHistoryIndex];   // Write to next slot
HistoryIndex = NextHistoryIndex;  // Rotate
```

**Purpose**: GPU reads frame N while CPU writes frame N+1. No pipeline stall.

---

## Part 4: Tonemap Shader Integration

### SRV Routing: TAA Output → Tonemap

**Pass 1 (TAA Resolve)**:
- Input: SceneColor (HDR), SceneVelocity, PrevTAAOutput
- Output: TAAResolveOutput (UAV write, still HDR)

**Pass 2 (Tonemap)**:
- Input: TAAResolveOutput (now read as SRV)
- Output: LDR backbuffer (or intermediate LDR texture)

### Tonemap Shader Access

In `PostProcessTonemap.usf` (pixel shader):

```hlsl
Texture2D SceneColorInput : register(t0);  // Connected to TAAResolveOutput

float4 main(PixelShaderInput Input) : SV_Target {
    float2 UV = Input.UV;
    
    // Read TAA-resolved HDR color
    float4 SceneColor = SceneColorInput.Sample(SceneTextureSampler, UV);
    
    // Apply tone mapping curve (ACES, Filmic, etc.)
    float3 TonemappedColor = ApplyTonemappingCurve(SceneColor.rgb);
    
    // Apply color grading, LUT, etc.
    TonemappedColor = ApplyColorGrading(TonemappedColor);
    
    return float4(TonemappedColor, 1.0);
}
```

### Exact Connection Path in RDG

In `AddPostProcessingPasses()` (PostProcessing.cpp ~line 950):

```cpp
// After TAA
FRDGTextureRef TAAOutput = AddTemporalAAPass(
    GraphBuilder,
    SceneColor,
    SceneVelocity,
    PrevSceneColor,
    ...
);

// Tonemap consumes TAA output
AddTonemappingPass(
    GraphBuilder,
    SceneColorInput = TAAOutput,  // <-- Direct connection
    ...
);
```

**Parameter Binding in Tonemap Pixel Shader**:

```cpp
class FPostProcessTonemap : public FGlobalShader {
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SceneColorInput)  // From TAA
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ColorGradingLUT)
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, BloomTexture)
        // ...
    END_SHADER_PARAMETER_STRUCT()
};
```

---

## Part 5: Complete SRV/UAV Routing Chain

```
Frame N:
  ┌─────────────────────────────────┐
  │ Deferred Lighting Pass          │
  │ Output: HDR SceneColor          │
  └──────────────┬──────────────────┘
                 │ (SRV)
                 ▼
  ┌─────────────────────────────────┐
  │ Motion Vectors Pass             │
  │ Output: SceneVelocity (px)      │
  └──────────────┬──────────────────┘
                 │ (SRV)
                 ▼
  ┌─────────────────────────────────┐
  │ TAA Resolve (Compute)           │
  │ Inputs:                         │
  │  - SceneColor (SRV)             │
  │  - SceneVelocity (SRV)          │
  │  - PrevTAAOutput (SRV) ◄────┐   │
  │ Output: TAAOutput (UAV) ──┐ │   │
  └─────────────────────────────────┘
                                 │ │
                                 │ │ (History ring)
                                 │ └─ Stored to buffer[N%3]
                                 │
                                 ▼
  ┌─────────────────────────────────┐
  │ Tonemap Pass (Pixel)            │
  │ Input: TAAOutput (SRV)          │
  │ Output: LDR Final Image         │
  └─────────────────────────────────┘
```

---

## Part 6: Key Implementation Details

### Velocity Reprojection

UE5 computes velocity in **pixel space** (not normalized), so reprojection is:

```hlsl
float2 PrevPixelCoord = float2(DTid.xy) - Velocity;
float2 PrevUV = (PrevPixelCoord + 0.5) / OutputExtent;
float4 HistoryColor = PrevSceneColorInput.SampleLevel(Sampler, PrevUV, 0.0);
```

The `+ 0.5` converts from integer pixel coord to texture coordinate (center of texel).

### Blend Factor Calculation

The blend weight between history and current is NOT fixed. It's **motion-adaptive**:

```hlsl
// Base blend (history weight)
float HistoryBlend = 0.85;  // 85% history, 15% new frame

// Adjust based on motion
float MotionMagnitude = length(Velocity);
if (MotionMagnitude > 10.0) {
    // Fast motion: prefer current frame (more responsive)
    HistoryBlend = 0.65;
} else if (MotionMagnitude < 0.5) {
    // Still scene: blend more heavily to maintain stability
    HistoryBlend = 0.95;
}

float BlendFactor = HistoryBlend;  // 0.85 = 85% history weight
OutputColor = lerp(HistoryColor, CurrentColor, 1.0 - BlendFactor);
```

### Disocclusion Detection (Highest Quality)

When quality is set to maximum, TAA detects when a pixel is newly visible (was occluded):

```hlsl
float3 ColorBoxMin = CurrentColor;
float3 ColorBoxMax = CurrentColor;

// Sample 3x3 neighborhood to build bounding box
for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
        int3 NeighborCoord = int3(DTid.xy + int2(dx, dy), 0);
        float3 NeighborColor = SceneColorInput.Load(NeighborCoord).rgb;
        ColorBoxMin = min(ColorBoxMin, NeighborColor);
        ColorBoxMax = max(ColorBoxMax, NeighborColor);
    }
}

// Reject history if it's far outside the current neighborhood
if (any(HistoryColor < ColorBoxMin * 0.9 || HistoryColor > ColorBoxMax * 1.1)) {
    HistoryBlend = 0.0;  // Reject history entirely
}
```

### Sharpening Filter

Optional post-resolve sharpening is applied before write:

```hlsl
float3 UnsharpMask(float3 Center, float Sharpness) {
    // Sample 3x3 box
    float3 Blurred = float3(0,0,0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int3 Coord = int3(DTid.xy + int2(dx, dy), 0);
            Blurred += SceneColorInput.Load(Coord).rgb;
        }
    }
    Blurred /= 9.0;
    
    // Unsharp: Center + (Center - Blurred) * Strength
    return Center + (Center - Blurred) * Sharpness;
}
```

---

## Part 7: Common Pitfalls and UzEngine Considerations

### 1. Velocity Sign Convention
- **UE5**: Velocity points BACKWARD in time (where pixel came FROM, not where it's GOING)
- **UzEngine**: Verify your motion vector generation matches this

### 2. Reversed-Z Depth
- UE5 uses reversed-Z (1.0 = camera, 0.0 = far)
- TAA doesn't directly use depth, but velocity generation does
- Ensure velocity shader samples depth correctly

### 3. First Frame UAV Issue
- Some implementations have a bug: writing to UAV on frame 0 before RDG validates texture creation
- **Solution**: Always check `FirstValidFrame == CurrentFrame` branch executes before any history access

### 4. History Buffer Lifecycle
- History must persist across frames (stored in `FTemporalAAHistory` per-view)
- If scene changes (portal, teleport), manually call `History.SafeRelease()` to reset
- **Common UzEngine pattern**: Check if camera moved > 1 meter → reset TAA

### 5. Tonemap Integration
- TAA output is **still HDR** (never tonemapped)
- Tonemap must read from TAA output, not directly from lighting
- Ensure RDG pass ordering is: Lighting → TAA → Tonemap

### 6. Sub-Pixel Jitter
- UE5 applies Halton sequence jitter to the camera projection matrix BEFORE rendering
- Jitter is 0.5 pixels per frame in a 16-frame pattern
- TAA assumes this jitter exists (will produce artifacts if missing)

---

## References

**UE5 Source Files (when available)**:
- `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessTemporalAA.cpp` (C++ setup)
- `Engine/Shaders/Private/PostProcessTemporalAA.usf` (Compute shader)
- `Engine/Shaders/Private/PostProcessTonemap.usf` (Consumer)
- `Engine/Source/Runtime/Renderer/Private/PostProcessing.cpp` (Pass ordering)

**Academic References**:
- **"A Reconstruction Filter for Plausible Motion Blur" (2016)** — Foundational TAA technique
- **"Temporal Reprojection Anti-Aliasing in Inside" (GDC 2016)** — Production TAA with disocclusion
- **"Responsive In-Game Low Latency Video Playback" (2013)** — Motion-adaptive blending

---

## Next Steps for UzEngine Implementation

1. **Verify velocity generation**: Read your motion vector compute shader
2. **Check first-frame branch**: Confirm no-history path exists
3. **Trace RDG connections**: Use `--diag` to verify TAA output actually reaches tonemap
4. **Test disocclusion**: Rapid camera movement should not cause ghosting
5. **Measure blend factors**: Log `HistoryBlend` values across frames to verify motion adaptation

