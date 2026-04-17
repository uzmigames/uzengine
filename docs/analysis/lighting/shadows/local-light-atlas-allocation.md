<!-- doc-meta
id: local-light-atlas-allocation
type: analysis
author: unreal-architect
created: 2026-03-30
status: complete
scope: UE5 dynamic shadow atlas allocation for local lights
tags: [shadows, atlas, allocation, resolution-scaling, importance]
related: [point-spot-light-shadows]
-->

# UE5 Local Light Shadow Atlas Allocation

## Overview

UE5 manages point light and spot light shadows through **dynamic resolution scaling** rather than traditional tile-based atlas allocation. Each light is assigned a render-target texture with **dynamically computed resolution** based on screen-space coverage. This approach prioritizes lights based on visual importance (screen coverage) rather than precomputed importance metrics.

---

## 1. Atlas vs. Dynamic Resolution Model

### 1.1 UE5 Approach: Resolution Scaling

Unlike traditional shadow atlases that pre-allocate fixed-size tiles:

| Traditional Atlas | UE5 Dynamic Scaling |
|---|---|
| Fixed tiles (256x256, 512x512) | Resolution computed per-frame |
| Importance ranking → tile size | Screen coverage → resolution |
| Layout algorithm (rectangular packing) | Direct allocation (no packing) |
| Reallocation stalls GPU | Continuous adjustment (smooth) |
| Atlas texture = fixed size | Textures grow/shrink with usage |

**UE5's Key Insight**: Instead of fitting shadows into a fixed atlas, allocate each shadow a resolution **proportional to how many pixels it covers on screen**.

### 1.2 Resolution Scaling Formula

```cpp
// From ShadowSetup.cpp:4150-4177
for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex) {
    const FViewInfo& View = Views[ViewIndex];

    // Screen-space light coverage
    const float ScreenRadius = LightSceneInfo->Proxy->GetEffectiveScreenRadius(
        View.GetShadowViewMatrices(),
        View.ViewRect.Size()
    );
    MaxScreenRadius = FMath::Max(MaxScreenRadius, ScreenRadius);

    // Resolution = screen pixels * texels-per-pixel ratio
    float UnclampedResolution;
    switch (LightSceneInfo->Proxy->GetLightType()) {
        case LightType_Point:
            UnclampedResolution = ScreenRadius * CVarShadowTexelsPerPixelPointlight;
            // CVarShadowTexelsPerPixelPointlight = 1.5 (default)
            break;
        case LightType_Spot:
            UnclampedResolution = ScreenRadius * CVarShadowTexelsPerPixelSpotlight;
            // CVarShadowTexelsPerPixelSpotlight = 1.5 (default)
            break;
        // ... other light types
    }
}
```

**Concrete Example:**
- Light projects to 200 pixels on screen
- `CVarShadowTexelsPerPixelPointlight = 1.5`
- Unclamped resolution = 200 × 1.5 = 300 pixels
- Rounded resolution = 256 (nearest power of 2)
- Shadow rendered at 256×256

---

## 2. Resolution Computation Pipeline

### 2.1 Step 1: Screen Radius Calculation

```cpp
// FLightSceneProxy::GetEffectiveScreenRadius()
float GetEffectiveScreenRadius(
    const FViewMatrices& ShadowViewMatrices,
    const FIntPoint& CameraViewRectSize) const
{
    // Project light bounds to screen
    // Returns the radius in screen pixels of the light's bounding sphere
    // as seen from the view

    // For point lights:
    // ScreenRadius = atan(light_radius / distance_to_camera) * viewport_height_pixels
}
```

**Factors affecting screen radius:**
- Light distance from camera (inverse: farther → smaller)
- Light radius (larger → larger screen projection)
- Viewport size (larger viewport → larger pixels)
- View matrix (can affect apparent size)

### 2.2 Step 2: Unclamped Resolution

```cpp
// Resolution before any constraints
float UnclampedResolution = ScreenRadius * CVarShadowTexelsPerPixelPointlight;

// Example values:
// ScreenRadius = 100, CVarShadowTexelsPerPixelPointlight = 1.5
// UnclampedResolution = 150
```

**Texels-per-pixel (quality knobs):**
```cpp
// From ShadowDepthRendering.cpp
static float CVarShadowTexelsPerPixelPointlight = 1.5f;  // point lights
static float CVarShadowTexelsPerPixelSpotlight = 1.5f;   // spot lights
static float CVarShadowTexelsPerPixelRectlight = 0.75f;  // rect lights
static float CVarShadowTexelsPerPixelDirectional = 1.0f; // directional lights
```

Higher values → higher resolution shadows (more expensive, better quality).

### 2.3 Step 3: Fade Alpha Calculation

```cpp
// From ShadowSetup.cpp:4183-4185
const float FadeAlpha = CalculateShadowFadeAlpha(
    FadeCalcResolution,     // unclamped or clamped resolution
    ShadowFadeResolution,   // r.Shadow.FadeResolution (default = 1.0)
    MinShadowResolution     // r.Shadow.MinShadowResolution (default = 0)
) * LightSceneInfo->Proxy->GetShadowAmount();

// CalculateShadowFadeAlpha() implements:
// fade_alpha = 1.0 - clamp(
//      (ShadowFadeResolution - resolution) / ShadowFadeResolution,
//      0, 1)
//
// If FadeResolution = 1.0:
//   resolution >= 1.0 → alpha = 1.0 (fully shadowed)
//   resolution  = 0.5 → alpha = 0.5 (half intensity)
//   resolution  = 0.0 → alpha = 0.0 (no shadow)
```

**Early-out check:**
```cpp
if (MaxFadeAlpha > 1.0f / 256.0f) {  // 0.00390625
    // Create shadow (only if fade alpha > ~0.004)
}
```

Very small shadows (alpha < 1/256) are **skipped entirely** to save GPU bandwidth.

### 2.4 Step 4: Resolution Clamping

```cpp
// From ShadowSetup.cpp:4213-4215
const uint32 MaxShadowResolution = FMath::Min(
    MaxShadowResolutionSetting,  // r.Shadow.MaxShadowResolution (usually 2048)
    ShadowBufferResolution.X
) - SHADOW_BORDER * 2;  // SHADOW_BORDER = 0 for point lights

int32 RoundedDesiredResolution = FMath::Max<int32>(
    (1 << (FMath::CeilLogTwo(MaxDesiredResolution + 1.0f) - 1))
    - (ShadowBorder * 2),
    1
);

int32 SizeX = MaxDesiredResolution >= MaxShadowResolution
    ? MaxShadowResolution
    : RoundedDesiredResolution;

int32 SizeY = MaxDesiredResolution >= MaxShadowResolutionY
    ? MaxShadowResolutionY
    : RoundedDesiredResolution;
```

**Power-of-two rounding ensures:**
- Filtering stability (mipmap-friendly)
- Consistent memory footprint
- Fast resolution doubling/halving

**Example:**
- UnclampedResolution = 300
- CeilLogTwo(301) = 9 → (1 << 8) = 256
- RoundedDesiredResolution = 256
- Max = 2048
- Final SizeX = 256

---

## 3. Shadow Buffer Allocation

### 3.1 Global Shadow Buffer Resolution

```cpp
// From ShadowSetup.cpp:4136
const FIntPoint ShadowBufferResolution = GetShadowDepthTextureResolution(FeatureLevel);

// Typical values by platform:
// PC High:    2048 x 2048
// PC Medium:  1536 x 1536
// Console:    1024 x 1024
// Mobile:     512 x 512
```

The **global atlas** holds all shadows (point lights, spot lights, cascaded directional). Total budget is shared.

### 3.2 Per-Shadow Allocation

Each shadow's **FProjectedShadowInfo** has:
```cpp
struct FProjectedShadowInfo {
    uint32 ResolutionX;  // Computed width in texels
    uint32 ResolutionY;  // Computed height in texels

    FIntPoint X;         // Top-left corner in atlas (for non-point lights)
    FIntPoint Y;         // Height in atlas (for non-point lights)

    // For point lights (one-pass):
    // The entire viewport is used (no atlas packing)
};
```

### 3.3 Layout for Point Lights

Point lights render **full-screen** in the shadow pass (not to a tile):

```cpp
// From ShadowRendering.cpp:1544
RHICmdList.SetViewport(
    View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f,
    View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f
);

// Output depth texture: RenderTargets.DepthTarget
// Size: ResolutionX x ResolutionY (computed per light)
```

---

## 4. Importance-Based Culling

### 4.1 Fade Alpha = Implicit Priority

Instead of explicit importance ranking, UE5 uses **fade alpha** as the culling metric:

```cpp
if (MaxFadeAlpha < 1.0f / 256.0f) {
    // Skip this shadow entirely
    // Reason: Too far/small to matter visually
}
```

**Lights are prioritized by:**
1. **Screen-space coverage** (larger → higher priority)
2. **Distance** (closer → higher priority)
3. **Shadow amount** (fully opaque → higher priority)
4. **Fade distance settings** (per-light override)

### 4.2 Multi-View Fade Alpha

For stereo or split-screen rendering:

```cpp
float MaxFadeAlpha = 0.0f;
for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex) {
    float FadeAlpha = CalculateShadowFadeAlpha(...);
    MaxFadeAlpha = FMath::Max(MaxFadeAlpha, FadeAlpha);
}

// Use Max fade alpha across all views
// If any view sees the shadow, render it
```

---

## 5. Quality Scalability & CVars

### 5.1 Resolution Quality Settings

```cpp
// r.Shadow.MaxShadowResolution — maximum shadow texture dimension
CVarMaxShadowResolution = 2048;  // PC High
// Console: 1024
// Mobile: 512

// r.Shadow.MinShadowResolution — don't render shadows smaller than this
CVarMinShadowResolution = 0;     // default: render any size

// r.Shadow.FadeResolution — fade out below this resolution
CVarShadowFadeResolution = 1.0f; // default: instant fade at ~1 pixel

// r.Shadow.PointLight.ScreenRadiusScale — multiplier for screen radius
CVarShadowScreenRadiusScale = 1.0f; // 1.0x = default
```

### 5.2 Texels-Per-Pixel Knobs

```cpp
// Adjust shadow quality per light type
r.Shadow.TexelsPerPixelPointlight = 1.5f;    // 1 shadow pixel per 0.67 screen pixel
r.Shadow.TexelsPerPixelSpotlight = 1.5f;     // Same as point lights
r.Shadow.TexelsPerPixelRectlight = 0.75f;    // Half quality (faster)
r.Shadow.TexelsPerPixelDirectional = 1.0f;   // CSM specific

// Example: Set for faster shadows
r.Shadow.TexelsPerPixelPointlight = 0.75f    // 2x faster, lower quality
```

### 5.3 Scalability Settings

```cpp
// From ScalabilitySettings.h
enum EShadowQuality {
    SQ_Low,    // Min 256, Max 512
    SQ_Medium, // Min 512, Max 1024
    SQ_High,   // Min 1024, Max 2048
    SQ_Epic,   // Min 2048, Max 4096
};
```

---

## 6. Memory Management

### 6.1 Render Target Pooling

UE5 **reuses** shadow render targets across frames:

```cpp
// From FProjectedShadowInfo
struct FRenderTargets {
    TRefCountPtr<IPooledRenderTarget> DepthTarget;    // Depth texture
    TArray<TRefCountPtr<IPooledRenderTarget>> ColorTargets;  // Optional color
};

// Each frame:
// 1. Compute desired resolution
// 2. Request RenderTarget of that size from pool
// 3. Render shadow into it
// 4. Return to pool after use
```

**Benefits:**
- No allocation/deallocation stalls
- Automatic memory reuse
- Smooth resolution transitions

### 6.2 Bandwidth Implications

```
Memory Cost = ResolutionX * ResolutionY * BytesPerPixel

For reversed-Z depth (D32F):
- 256x256 shadow = 256 * 256 * 4 = 262 KB
- 512x512 shadow = 512 * 512 * 4 = 1.05 MB
- 1024x1024 shadow = 1024 * 1024 * 4 = 4.19 MB

With 32 point lights at 512x512:
Total = 32 * 1.05 = 33.6 MB per frame
```

---

## 7. Dynamic Adjustment During Frame

### 7.1 Real-Time Reprioritization

The resolution computation happens **every frame**:

```cpp
// FSceneRenderer::CreateWholeSceneProjectedShadows() (per frame)
{
    // Recompute all light resolutions based on current camera view
    // Lights that moved closer → higher resolution
    // Lights that moved away → lower resolution
    // New shadows may appear (fade in)
    // Old shadows may disappear (fade out)
}
```

**No pre-computation, fully dynamic.**

### 7.2 Cascaded vs. Local Light Shadows

```cpp
// Cascaded shadows (directional lights)
// - Pre-computed 4 cascades at fixed resolutions
// - Atlas packing with explicit tile layout
// - Stable frame-to-frame

// Local light shadows (point/spot)
// - Computed per-frame based on screen coverage
// - Each light allocated full viewport area
// - Resolution changes smoothly
```

---

## 8. Typical Real-World Scenarios

### 8.1 Urban Scene with 32 Point Lights

**Setup:**
- 32 point lights scattered around street
- Player camera in center
- 1920x1080 viewport
- PC High quality settings (Max 2048)

**Expected allocations:**
```
Near lights (distance < 500):
  - ScreenRadius ~100-150 pixels
  - UnclampedResolution = 150-225
  - Rounded = 256x256
  - Count: ~4-8 lights
  - Total: ~4 lights * 262 KB = 1 MB

Mid lights (distance 500-2000):
  - ScreenRadius ~30-80 pixels
  - UnclampedResolution = 45-120
  - Rounded = 64x64 or 128x128
  - Count: ~12-16 lights
  - Total: 128x128 * 4 * 12 = 6 MB

Far lights (distance > 2000):
  - ScreenRadius < 20 pixels
  - Skipped (fade alpha < 1/256)
  - Count: ~8-16 lights
  - Total: 0 MB (culled)

Frame Total: ~7-10 MB
```

### 8.2 Dimly Lit Interior (10 Small Bulbs)

**Setup:**
- 10 point lights (100W equivalent)
- Indoor scene, small room
- Lights fairly close together
- No camera inside light volumes

**Expected:**
```
All lights visible from camera center:
- ScreenRadius ~20-80 pixels (small room)
- UnclampedResolution = 30-120
- Rounded = 64x64 or 128x128
- Total: 10 lights * 256 KB = 2.6 MB
```

---

## 9. Optimization Techniques

### 9.1 Distance-Based Fade

```cpp
// Manually set fade distance per light
PointLight->ShadowBias = 1.0f;              // Keep shadow enabled
PointLight->ShadowResolutionScale = 0.5f;   // Half resolution

// OR use scene settings
r.Shadow.FadeResolution = 0.5f;  // Fade begins at 0.5 pixel coverage
r.Shadow.MinShadowResolution = 8; // Don't render shadows < 8 pixels
```

### 9.2 Quality Scale Based on Light Type

```cpp
// Expensive: large outdoor area lights
PointLight->bCastShadows = true;
PointLight->ShadowResolutionScale = 1.0f;

// Cheap: small indoor task lights
PointLight->bCastShadows = true;
PointLight->ShadowResolutionScale = 0.25f;  // 4x faster
```

### 9.3 Selective Shadow Culling

```cpp
// Skip shadow rendering for lights that don't affect important objects
PointLight->bCastDynamicShadows = false;    // No shadows
PointLight->bAffectsTranslucentLighting = false;  // Still lights translucent

// Or: only shadow specific object types
PointLight->bCastShadowsOnCharacters = true;
PointLight->bCastShadowsOnStaticGeometry = false;
```

---

## 10. Comparison to UzEngine Implementation

### 10.1 Current State (UzEngine)

```
// docs/specs/point-spot-light-shadows.md mentions:
// - Static allocation of shadow atlas (1024x1024)
// - Fixed tile size (e.g., 256x256, 512x512)
// - Pre-computed importance ranking
// - Limited to N simultaneous shadows
```

### 10.2 Recommended Approach

**Option A: Match UE5 (Recommended)**
- Compute resolution per-frame based on screen coverage
- Use fade alpha for culling (< 1/256 threshold)
- Render each light to dynamically-sized render target
- Smooth transitions as lights move in/out of view

**Option B: Hybrid (Faster to Implement)**
- Keep atlas-based approach for stability
- Dynamically adjust tile sizes within atlas
- Use importance sorting (distance + screen coverage)
- Support up to 64-128 local light shadows

---

## 11. References

| Source | Content |
|---|---|
| `ShadowSetup.cpp:4120-4230` | Resolution computation pipeline |
| `ShadowRendering.cpp:1504-1662` | One-pass point light rendering |
| `FProjectedShadowInfo` struct | Shadow allocation & parameters |
| `CVarShadowTexelsPerPixelPointlight` | Quality knob (default 1.5) |

---

## 12. Summary

UE5's local light shadow allocation strategy prioritizes **visual quality per unit of GPU memory** by:

1. **Computing resolution dynamically** from screen-space coverage (not static)
2. **Culling with fade alpha** for automatic LOD (not precomputed importance)
3. **Smooth transitions** as camera moves (no atlas reallocation stalls)
4. **Per-light full-viewport rendering** for point lights (not tile-packed)

This approach trades **predictable memory usage** for **adaptive visual quality**, which works well in open-world games with dynamic lighting.

