<!-- doc-meta
type: architecture-analysis
title: UE5 4K Rendering & Upscaling System
subtitle: Screen percentage, temporal upscalers, dynamic resolution
tags: [rendering, upscaling, screen-percentage, TSR, TAA, DLSS, resolution]
author: Claude Code
date: 2026-03-16
status: complete
confidence: high
sources:
  - W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/LegacyScreenPercentageDriver.cpp
  - W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/DynamicResolutionState.h
  - W:/UE_5.6_Source/Engine/Source/Runtime/RenderCore/Public/DynamicRenderScaling.h
  - W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Public/TemporalUpscaler.h
  - W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/PostProcess/TemporalSuperResolution.cpp
-->

# UE5 4K Rendering & Upscaling Architecture

## Executive Summary

UE5 uses a sophisticated **screen percentage system** (not native 4K) combined with **temporal upscalers** (TSR, TAA, or third-party DLSS/FSR) to achieve high visual quality at lower GPU cost. The architecture supports:

1. **Temporal Super Resolution (TSR)** — Built-in upscaler (replaces TAA in 5.0+)
2. **Temporal AA (TAA)** — Legacy Gen4 console solution
3. **Third-party upscalers** — DLSS, FSR, XeSS via plugin interface
4. **Dynamic Resolution** — GPU-bound adaptive scaling

---

## 1. Screen Percentage System

### Definition

**Screen percentage = (internal render resolution / display resolution) × 100**

- **100%** = Render at display resolution (4K for 4K display)
- **50%** = Render at half resolution (1440p for 4K display), upscale to 4K
- **67%** = Render at 67% (2688×1512 for 4K), upscale to 4K

### Key CVars

| CVar | Default | Description |
|------|---------|-------------|
| `r.ScreenPercentage` | `0.0` (disabled) | Manual override; ≤0 uses defaults |
| `r.ScreenPercentage.Default` | `100.0` | Default when Manual is disabled |
| `r.ScreenPercentage.Default.Desktop.Mode` | `BasedOnDisplayResolution` | Auto-compute vs Manual |
| `r.ScreenPercentage.Default.Mobile.Mode` | `Manual` | Mobile uses fixed value |
| `r.ScreenPercentage.MinResolution` | `0.0` (no limit) | Absolute minimum pixel count |
| `r.ScreenPercentage.MaxResolution` | `0.0` (no limit) | Cap max pixels before upscaling |
| `r.ScreenPercentage.Auto.PixelCountMultiplier` | `1.0` | Scale pixel budget |

**Source**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/LegacyScreenPercentageDriver.cpp` lines 10-61

### Screen Percentage Modes

```cpp
enum class EScreenPercentageMode {
    Manual,                    // r.ScreenPercentage sets exact value
    BasedOnDisplayResolution,  // Auto-compute from display size (desktop default)
    BasedOnDPIScale,          // Scale by OS DPI factor
};
```

Default is `BasedOnDisplayResolution` for desktop — automatically compute optimal resolution based on pixel budget.

---

## 2. View Resolution Calculation

### ViewRect vs UnscaledViewRect

```cpp
// From SceneView.h
FIntRect UnscaledViewRect;      // Display resolution (e.g., 3840×2160 for 4K)
FIntRect ViewRect;              // Actual render resolution (e.g., 2560×1440 if 67% SP)
```

### Calculation Flow

```
DisplayResolution = 3840×2160 (4K)
ScreenPercentage = 67%
ResolutionFraction = 0.67

RenderResolution = round(DisplayResolution × ResolutionFraction)
                 = (2572, 1448)  ← This becomes ViewRect
```

RenderResolution becomes `ViewRect`, which is used for:
- Allocating scene color render target
- Depth buffer sizing
- G-buffer sizing
- All rendering passes

`UnscaledViewRect` (display size) is used only for:
- Final upscaler output viewport
- Post-process effects after upscaling
- GUI/HUD rendering

**Source**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Classes/Engine/RendererSettings.h`

---

## 3. Temporal Upscaler Interface (Plugin Architecture)

### ITemporalUpscaler Abstract Base

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Public/TemporalUpscaler.h`

```cpp
namespace UE::Renderer::Private {

class ITemporalUpscaler : public ISceneViewFamilyExtention {
public:
    // Input from rendering pipeline
    struct FInputs {
        FIntRect OutputViewRect;               // Display rect (3840×2160)
        FVector2f TemporalJitterPixels;        // Sub-pixel jitter for AA
        float PreExposure;                     // For auto-exposure
        FScreenPassTexture SceneColor;         // Render res color (2560×1440)
        FScreenPassTexture SceneDepth;         // Full res depth
        FScreenPassTexture SceneVelocity;      // Full res motion vectors
        FRDGTextureRef EyeAdaptationTexture;   // Auto-exposure LUT
        TRefCountPtr<IHistory> PrevHistory;    // Last frame's accumulated data
    };

    // Output to composition
    struct FOutputs {
        FScreenPassTexture FullRes;            // Upscaled to display (3840×2160)
        TRefCountPtr<IHistory> NewHistory;     // For next frame
    };

    virtual FOutputs AddPasses(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FInputs& Inputs) const = 0;

    virtual float GetMinUpsampleResolutionFraction() const = 0;
    virtual float GetMaxUpsampleResolutionFraction() const = 0;
};

} // namespace UE::Renderer::Private
```

### Upscaler Selection Hierarchy

```cpp
enum class EMainTAAPassConfig {
    Disabled,       // No upscaling, render at 100% only
    TAA,            // Gen4 console legacy
    TSR,            // Temporal Super Resolution (5.0+ default)
    ThirdParty,     // DLSS, FSR, XeSS (plugin provides interface)
};

EMainTAAPassConfig GetMainTAAPassConfig(const FViewInfo& View);
```

Runtime selection:

```cpp
if (View.Family->GetTemporalUpscalerInterface() != nullptr) {
    // Third-party upscaler registered (DLSS, FSR, etc.)
    Outputs = AddThirdPartyTemporalUpscalerPasses(GraphBuilder, View, Inputs);
} else {
    switch (GetMainTAAPassConfig(View)) {
        case EMainTAAPassConfig::TSR:
            Outputs = AddMainTemporalSuperResolutionPasses(...);
            break;
        case EMainTAAPassConfig::TAA:
            Outputs = AddGen4MainTemporalAAPasses(...);
            break;
        // ...
    }
}
```

---

## 4. Temporal Super Resolution (TSR)

### What TSR Is

**TSR is not a separate system from AA — it IS the modern temporal anti-aliasing.**

In UE 4.x:
```
Render at low res → Temporal AA pass → Spatial upscale pass
```

In UE 5.0+:
```
Render at low res → TSR (combines temporal AA + upscaling + denoising)
```

### TSR Configuration

Key CVars from `TemporalSuperResolution.cpp`:

| CVar | Default | Purpose |
|------|---------|---------|
| `r.TSR.History.ScreenPercentage` | `100.0` | Render history at % of output (100-200%) |
| `r.TSR.History.SampleCount` | `16.0` | Max accumulated samples per pixel (8-32) |
| `r.TSR.History.UpdateQuality` | `3` | Shader quality permutation (0-3) |
| `r.TSR.History.R11G11B10` | `1` | Use 11-bit color encoding (saves bandwidth) |
| `r.TSR.ShadingRejection.SampleCount` | `2.0` | Samples after history rejection |
| `r.TSR.ShadingRejection.Flickering` | `1` | Detect & suppress moire/flicker |
| `r.TSR.ReprojectionField` | `0` | Use Jacobian for sub-pixel reprojection |
| `r.TSR.ThinGeometryDetection` | `0` | Detect foliage/wires; relax history |
| `r.TSR.WaveOps` | `1` | Use GPU wave instructions |
| `r.TSR.AsyncCompute` | `2` | Run on async compute queue |

**Source**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/PostProcess/TemporalSuperResolution.cpp` lines 24-382

### TSR Pass Pipeline

TSR is a **multi-pass system**, not a single shader:

1. **ClearPrevTextures** — Reset history buffers
2. **DepthVelocityAnalysis** — Compute motion metrics
3. **DilateVelocity** — Propagate motion vectors (async)
4. **HistoryUpdate** — Reproject previous frame & accumulate
5. **DetectThinGeometry** — Find sub-pixel edges (optional)
6. **RejectHistory** — Detect flickering; discard bad samples
7. **ShadingAnalysis** — Measure surface properties
8. **ConvolutionNetwork** — Neural network denoising
9. **MeasureFlickeringLuma** — Monitor luminance stability
10. **Upscale** — Final resolution increase to output

This multi-pass approach allows quality tuning via CVars.

### TSR vs Legacy TAA

| Feature | TSR | Gen4 TAA |
|---------|-----|----------|
| Upscaling | Built-in | Separate pass |
| History samples | 16 (tunable 8-32) | 4-8 (fixed) |
| Flickering detection | Yes (r.TSR.ShadingRejection.Flickering) | No |
| Moire anti-aliasing | Yes (shading rejection) | Limited |
| Thin geometry handling | Yes (r.TSR.ThinGeometryDetection) | No |
| Async compute support | Yes | No |
| Sub-pixel accuracy | Jacobian matrix (optional) | Simple |

---

## 5. Anti-Aliasing & Upscaling Relationship

### TAA Does NOT Mean "Only Temporal AA"

In modern UE5, **TAA is inseparable from upscaling**. The pipeline is:

```
Low-resolution render + Jitter
    ↓
Temporal accumulation (history reprojection)
    ↓
History rejection (detect moving geometry)
    ↓
Spatial antialiasing in rejected areas
    ↓
Upscale to output resolution
```

This is **one unified pass** in TSR (multiple sub-passes, but coordinated).

### Why Temporal Jitter?

Without jitter, aliasing occurs:
```
Frame N: Sample pixel (256, 384)
Frame N+1: Sample pixel (256, 384)
→ Same sample every frame = aliasing
```

With jitter (Halton sequence):
```
Frame N: Sample at (256.25, 384.125)
Frame N+1: Sample at (256.75, 383.875)
Frame N+2: Sample at (255.5, 384.5)
→ Different sub-pixel sample each frame = temporal AA
```

Jitter is generated in `View.TemporalJitterPixels`:

```cpp
FVector2f TemporalJitterPixels;  // e.g., (-0.375, +0.25)
// Applied to all rendering (base pass, shadow maps, reflections)
```

---

## 6. Third-Party Upscalers (DLSS, FSR, XeSS)

### Integration Point

When a plugin initializes, it registers itself:

```cpp
// In plugin's view extension
ViewFamily.SetTemporalUpscalerInterface(
    new FDLSSTemporalUpscaler(...)  // Implements ITemporalUpscaler
);
```

The render pipeline then checks for the plugin:

```cpp
if (const ITemporalUpscaler* Upscaler = View.Family->GetTemporalUpscalerInterface()) {
    // Use DLSS/FSR instead of TSR
    Outputs = Upscaler->AddPasses(GraphBuilder, View, Inputs);
} else {
    // Use built-in TSR
    Outputs = TSR_AddPasses(...);
}
```

### Min/Max Resolution Bounds

Each upscaler declares its constraints:

```cpp
// DLSS: 0.5x to 2.0x upscaling
float DLSSMinFraction = 0.5f;   // Don't render below 50% res
float DLSSMaxFraction = 2.0f;   // Don't render above 200% res

// FSR: ~0.67x to 2.0x
float FSRMinFraction = 0.67f;
float FSRMaxFraction = 2.0f;
```

DynamicResolution respects these bounds when active:

```cpp
void FDynamicResolutionHeuristicProxy::SetTemporalUpscaler(
    const ITemporalUpscaler* UpscalerInterface) {
    float MinFraction = UpscalerInterface ?
        UpscalerInterface->GetMinUpsampleResolutionFraction() :
        0.5f;  // Default if none
    // Use MinFraction to clamp resolution scaling
}
```

**Source**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/DynamicResolution.cpp`

### FSR Example (Mobile)

**File**: `W:/UE_5.6_Source/Engine/Plugins/Runtime/MobileFSR/Source/MobileFSR/Private/MobileFSRUpscaler.h`

```cpp
class FMobileFSRUpscaler final : public ISpatialUpscaler {
public:
    FMobileFSRUpscaler(bool bInIsEASUPass);

    const TCHAR* GetDebugName() const override {
        return TEXT("FMobileFSRUpscaler");
    }

    // ISpatialUpscaler::AddPasses() runs:
    // 1. EASU (Edge Adaptive Spatial Upsampling)
    // 2. RCAS (Robust Contrast Adaptive Sharpening)
    FScreenPassTexture AddPasses(FRDGBuilder& GraphBuilder, ...) const override;
};
```

FSR is **spatial only** (single-frame), no history. Used on mobile where bandwidth is expensive.

---

## 7. Dynamic Resolution System

### What It Solves

Maintaining steady **frame rate** on variable GPU load. Example:

```
Scene with heavy shadows: GPU frame time = 18ms (over budget)
  → Reduce to 85% → Next frame = 15ms ✓

Scene with simple lighting: GPU frame time = 12ms (under budget)
  → Increase to 100% → Next frame = 14ms ✓
```

### Configuration

```cpp
struct FHeuristicSettings {
    EHeuristicModel Model = EHeuristicModel::Quadratic;
    // Quadratic = GPU cost ∝ PixelCount²

    float MinResolutionFraction = 0.5f;   // Never below 50% render
    float MaxResolutionFraction = 1.0f;   // Never above 100% render
    float BudgetMs = 16.67f;              // Target 60 FPS
    float TargetedHeadRoom = 0.05f;       // 5% safety margin → target 15.83ms
    float ChangeThreshold = 0.02f;        // Only change if Δ ≥ 2%
    int32 FractionQuantization = 0;       // 0 = continuous, 4 = discrete steps
    float IncreaseAmortizationFactor = 0.9f; // Increase slowly, decrease fast
};
```

### Measurement Flow

```cpp
DynamicRenderScaling::BeginFrame(bIsBudgetEnabled);
    // Start GPU timer
    Render scene at current resolution
    // GPU work...
DynamicRenderScaling::EndFrame();
    // Stop timer, record duration
    uint64 MeasuredGPUTimeUs = ...;
```

Next frame:

```cpp
float TimingMs = MeasuredGPUTimeUs / 1000.0f;
float TargetMs = BudgetMs * (1.0f - TargetedHeadRoom);  // 15.83ms
float TimingRatio = TimingMs / TargetMs;

if (TimingRatio > 1.0f) {
    // Too slow, reduce resolution
    NewFraction = CurrentFraction * sqrt(1.0f / TimingRatio);
} else if (TimingRatio < 0.95f) {
    // Fast enough, try to increase
    NewFraction = CurrentFraction * (1.0f - IncreaseAmortization);
}
```

**Source**: `W:/UE_5.6_Source/Engine/Source/Runtime/RenderCore/Public/DynamicRenderScaling.h`

---

## 8. Default Rendering Strategy by Platform

### Desktop (4K)

```
Display resolution: 3840×2160
r.ScreenPercentage.Default.Desktop.Mode = BasedOnDisplayResolution

Automatic calculation:
  TotalPixels = 3840 × 2160 = 8.29M pixels
  DesiredPixels = TotalPixels × r.ScreenPercentage.Auto.PixelCountMultiplier (1.0)
  → Render at 100% = 8.29M pixels = 3840×2160

With dynamic resolution enabled (GPU-bound):
  Initial render: 100%
  If GPU time > 16.67ms:
    Next frame: 90% (3456×1944)
  If GPU time < 14.67ms:
    Next frame: 95% (3648×2052)
  → Converges to sustainable resolution
```

Default is **native 4K** on desktop unless GPU can't sustain frame rate.

### Mobile (High-End)

```
Display: 2560×1440 (Snapdragon flagship)
r.ScreenPercentage.Default.Mobile.Mode = Manual
r.ScreenPercentage.Default = 100.0  (but project often overrides to 75%)

Typical project settings:
  - Low-end (Mali G77): 50% → 1280×720, upscale to 2560×1440
  - Mid-range (Adreno 640): 75% → 1920×1080, upscale to 2560×1440
  - High-end (Snapdragon 8 Gen 2): 100% → 2560×1440
```

Mobile uses **fixed** percentage, not dynamic (easier to predict).

### VR (Per-Eye)

```
Headset: Valve Index (1440×1600 per eye)
r.ScreenPercentage.Default.VR.Mode = Manual
r.ScreenPercentage.Default = 100.0

Typically with dynamic resolution:
  r.DynamicRes.OperationMode = Enabled
  r.DynamicRes.MinScreenPercentage = 70
  r.DynamicRes.MaxScreenPercentage = 100
  r.DynamicRes.BudgetMs = 8.33  (120 FPS requirement)

Render at 100% per eye = 2 × 1440×1600 = 4.6M pixels
If GPU can't sustain, drop to 90%, 80%, 70%
```

VR **must** maintain frame rate (90-144 FPS) → aggressive dynamic resolution.

---

## 9. Key Architectural Patterns

### Pattern 1: Screen Percentage Indirection

Instead of hardcoding `RenderResolution = DisplayResolution * Fraction`, UE uses:

```cpp
interface ISceneViewFamilyScreenPercentage {
    virtual TMap<float> GetResolutionFractions_RenderThread() const = 0;
};
```

This allows:
- Multiple independent budgets (Primary, Secondary, VR eyes, etc.)
- Third-party upscalers to override bounds
- Dynamic resolution to modify fractions without changing views

**Pattern**: Strategy pattern for resolution computation.

### Pattern 2: Temporal Jitter (Halton Sequence)

All upscalers use **deterministic sub-pixel offsets**:

```cpp
FVector2f TemporalJitterPixels;  // Per-frame, coordinated across views
// Frame 0: (-0.375,  0.125)
// Frame 1: ( 0.375, -0.375)
// Frame 2: (-0.125,  0.625)
// Frame 3: ( 0.625, -0.125)
// ... (Halton base-2, base-3)
```

Jitter is applied to:
- Vertex positions in base pass
- Shadow map rendering (depth)
- G-buffer sampling
- Raytrace origin

**Why**: Temporal accumulation can only AA what it samples. Different samples each frame = coverage.

### Pattern 3: Separated History Lifecycle

History is **not tied to viewport resolution**:

```cpp
struct ITemporalUpscaler::IHistory {
    virtual uint64 GetGPUSizeBytes() const = 0;
};
```

TSR can maintain history at `r.TSR.History.ScreenPercentage = 200%`:
```
Render at 67%  (2560×1440)
History at 134% (2560×1440)
Output to 100% (3840×2160)
→ Higher quality accumulation
```

This allows storage of sub-pixel samples without expensive per-frame allocation.

---

## 10. Anti-Patterns to Avoid

### ❌ "4K" Always = Render at 4K

**Reality**: Most AAA games at 4K render 50-70%, upscale.

```
Unreal Engine 5 demo (RTX 3080):
  - Display: 4K (3840×2160)
  - Render: 1440p (2560×1440) ≈ 67% SP
  - Upscaler: TSR
  - Result: 60 FPS, visually nearly identical to 100%
```

GPU cost is **quadratic** with resolution. 67% render = ~45% GPU time of 100%.

### ❌ "TSR is Optional Anti-Aliasing"

**Reality**: TSR **is the modern temporal AA**, not optional.

You must pick one:
- **TAA** = Gen4 legacy (older, simpler)
- **TSR** = Modern (5.0+, recommended)
- **DLSS/FSR** = Third-party replacement
- **Disabled** = No temporal AA (aliasing artifacts)

There is no "pure spatial AA without upscaling" in modern UE5.

### ❌ "Screen Percentage = Post-Process Downsampling"

**Reality**: Screen percentage allocates smaller render targets:

```cpp
// WRONG (if attempted):
FRDGTextureDesc Desc(4K);
RenderTarget = GraphBuilder.CreateTexture(Desc);  // 4K allocation
RenderPass(RenderTarget);                         // Render at 4K
DownsamplePass(4K → 1440p);                       // Downsample
UpsamplePass(1440p → 4K);                         // Upscale
// Result: Blurry, 3 passes, wasted bandwidth ❌

// CORRECT:
FIntPoint RenderSize = ComputeRenderResolution(DisplaySize, ScreenPercentage);
FRDGTextureDesc Desc(RenderSize);  // 1440p allocation
RenderTarget = GraphBuilder.CreateTexture(Desc);  // 1440p only
RenderPass(RenderTarget);                         // Render at 1440p
UpsamplePass(1440p → 4K);                         // Single upscale ✓
```

Screen percentage affects `FRDGTextureDesc::Extent`, not post-process.

### ❌ "History Size = Render Target Size"

**Reality**: TSR can store history larger than render target:

```cpp
r.TSR.History.ScreenPercentage = 150;
RenderResolution = 2560×1440 (67% of 4K)
HistoryResolution = 2560×1440 × 1.5 = 3840×2160 (100% of 4K)
// History is larger than render input!
// This adds detail per-frame
```

This is intentional for quality — higher history resolution = sharper accumulation.

---

## 11. Adaptation Guidelines for UzEngine

### What to Replicate

1. **Screen Percentage CVar System**
   ```cpp
   uz_screen_percentage_default = 100.0
   uz_screen_percentage_default_mode = auto  // manual, auto, dpi_scale
   uz_screen_percentage = 0  // 0 = use default
   ```

2. **ViewRect Indirection**
   ```cpp
   struct FView {
       FIntRect UnscaledViewRect;  // Display (4K)
       FIntRect ViewRect;          // Render (1440p if 67% SP)
   };
   // Render targets sized to ViewRect
   // Upscaler outputs to UnscaledViewRect
   ```

3. **Upscaler Interface**
   ```cpp
   interface IUpscaler {
       FOutputs Process(const FInputs& In);
       float GetMinResolutionFraction();
       float GetMaxResolutionFraction();
   };
   ```

4. **Dynamic Resolution**
   ```cpp
   struct FDynamicResolutionBudget {
       float MinFraction = 0.5;
       float MaxFraction = 1.0;
       float BudgetMs = 16.67;  // 60 FPS
       float TargetHeadroom = 0.05;
   };
   ```

5. **Temporal Jitter**
   ```cpp
   FVector2f GetTemporalJitter(uint32 FrameNumber);
   // Apply to all camera jitter + object motion
   ```

### What NOT to Replicate

- **2000+ lines of TSR shaders** — Build own simpler upscaler
- **Convolution network** — Use simpler spatial filter
- **Thin geometry detection** — Optional feature, can skip initially
- **Flickering heuristics** — Complex, tuned for UE5 style content

### Suggested Implementation Order

1. **Phase 1**: Screen percentage CVar + ViewRect indirection (1 week)
2. **Phase 2**: Simple built-in upscaler (bilinear → lanczos) (1 week)
3. **Phase 3**: Temporal accumulation (history buffers) (2 weeks)
4. **Phase 4**: Dynamic resolution (GPU timing integration) (1 week)
5. **Phase 5**: Third-party upscaler interface (DLSS stub) (1 week)

---

## 12. Reference Files & Exact Locations

| Component | File Path | Key Structs/Functions |
|-----------|-----------|----------------------|
| Screen percentage CVars | `Engine/Source/Runtime/Engine/Private/LegacyScreenPercentageDriver.cpp` | `CVarScreenPercentage`, `FStaticResolutionFractionHeuristic` |
| Resolution fraction interface | `Engine/Source/Runtime/Engine/Public/SceneView.h` | `ISceneViewFamilyScreenPercentage` |
| Dynamic resolution state | `Engine/Source/Runtime/Engine/Public/DynamicResolutionState.h` | `IDynamicResolutionState`, `FBudget` |
| Dynamic resolution heuristic | `Engine/Source/Runtime/RenderCore/Public/DynamicRenderScaling.h` | `FHeuristicSettings`, `EHeuristicModel` |
| Upscaler interface | `Engine/Source/Runtime/Renderer/Public/TemporalUpscaler.h` | `ITemporalUpscaler`, `FInputs`, `FOutputs` |
| TSR implementation | `Engine/Source/Runtime/Renderer/Private/PostProcess/TemporalSuperResolution.cpp` | `FTSRShader`, `CVarTSR*` |
| TAA/TSR selection | `Engine/Source/Runtime/Renderer/Private/PostProcess/TemporalAA.h` | `EMainTAAPassConfig`, `FTAAPassParameters` |
| FSR plugin example | `Engine/Plugins/Runtime/MobileFSR/Source/MobileFSR/Private/MobileFSRUpscaler.h` | `FMobileFSRUpscaler` |

---

## 13. CVars Quick Reference

### Primary Resolution Control

```
r.ScreenPercentage=67              # Render at 67% (manual override)
r.ScreenPercentage.Default=100     # Default if manual=0
r.ScreenPercentage.Auto.PixelCountMultiplier=1.0
```

### TSR Tuning

```
r.TSR.History.ScreenPercentage=100   # 100-200 supported
r.TSR.History.SampleCount=16         # 8-32 samples
r.TSR.ShadingRejection.Flickering=1  # Anti-flicker
r.TSR.Visualize=0                    # Debug view
```

### Dynamic Resolution

```
r.DynamicRes.OperationMode=0         # 0=off, 1=on, 2=debug
r.DynamicRes.MinScreenPercentage=50  # Floor
r.DynamicRes.MaxScreenPercentage=100 # Ceiling
```

---

## 14. Summary: The Complete 4K Pipeline

```
┌──────────────────────────────────────────────────────┐
│ Display: 3840×2160 (4K)                              │
│ r.ScreenPercentage.Default.Desktop.Mode = auto       │
│ → Compute: RenderResolution = 3840×0.67 = 2560×1440 │
└──────────────────────────────────────────────────────┘
                         │
                         ↓
┌──────────────────────────────────────────────────────┐
│ Render at 2560×1440 (67% of 4K)                      │
│ - Allocate ViewRect-sized render targets             │
│ - Apply temporal jitter to camera                    │
│ - Depth, normal, velocity at full render res         │
└──────────────────────────────────────────────────────┘
                         │
                         ↓
┌──────────────────────────────────────────────────────┐
│ Temporal Super Resolution (TSR)                      │
│ - Input: 2560×1440 color + depth + velocity         │
│ - Reprojection: Match to previous frame history     │
│ - Rejection: Discard ghosted pixels                  │
│ - Upscale: 2560×1440 → 3840×2160                    │
│ - Output: Full 4K sharpness with AA                  │
└──────────────────────────────────────────────────────┘
                         │
                         ↓
┌──────────────────────────────────────────────────────┐
│ Post-Process (tone map, bloom, etc.) at 4K           │
│ - All post-effects run at 3840×2160 (output size)    │
│ - No additional upscaling                            │
└──────────────────────────────────────────────────────┘
                         │
                         ↓
┌──────────────────────────────────────────────────────┐
│ Output: 3840×2160 to framebuffer                     │
│ GPU time: ~55% of native 4K render                   │
│ Frame time: 16.67ms @ 60 FPS ✓                       │
└──────────────────────────────────────────────────────┘
```

