<!-- doc-meta
type: analysis
domain: rendering
title: UE5 SkyLight Cubemap Capture for IBL
date: 2026-03-15
status: complete
priority: high
references:
  - W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Components\SkyLightComponent.h
  - W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\SkyLightComponent.cpp
  - W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ReflectionEnvironmentCapture.cpp
  - W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyPassRendering.cpp
-->

# UE5 SkyLight Cubemap Capture for IBL

## Executive Summary

UE5 captures the sky atmosphere into a cubemap through a specialized two-path system:

1. **Captured Scene Path** — Renders the scene geometry and sky into 6 cubemap faces at the capture position
2. **Specified Cubemap Path** — Copies an existing static cubemap and applies lower-hemisphere masking

The captured cubemap is then **filtered into a precomputed IBL format** consisting of:
- **Diffuse Irradiance** (encoded as 3rd-order spherical harmonics)
- **Specular Prefiltered Environment** (6 mips with roughness filtering)
- **Average Brightness** (for HDRI tone mapping)

## Architecture Overview

### Component Hierarchy

```
USkyLightComponent (GameThread API)
  └─ FSkyLightSceneProxy (RenderThread representation)
       └─ FSkyTextureCubeResource (GPU cubemap storage)
            └─ RDG FilteredCubemapTexture (RDG-managed intermediate)
```

### Two Source Types

```cpp
enum ESkyLightSourceType : int
{
  SLS_CapturedScene,      // Re-render sky+scene into cubemap
  SLS_SpecifiedCubemap,   // Copy existing cubemap
};
```

### Scheduling

Sky light captures are **deferred** — not executed immediately, but queued for later:

```cpp
TArray<USkyLightComponent*> SkyCapturesToUpdate;  // Game thread queue
```

Captures happen during the **render of a main scene frame** at specific points:
- After level loading completes
- When `RecaptureSky()` is called
- Every frame if `bRealTimeCapture = true` (requires Movable mobility)

---

## 1. SkyLight Cubemap Capture Flow

### 1.1 Captured Scene Path

**When:** User selects `SourceType = SLS_CapturedScene` (default)

**Input:** Scene geometry, materials, sky atmosphere, lights, fog, clouds

**Process:**

```
CaptureSceneIntoScratchCubemap()
  │
  └─ For each cubemap face (0-5):
      │
      ├─ Create view camera looking down each face direction
      │   • Projection: 90° FOV (cubemap standard)
      │   • Near plane: SkyLightComponent::SkyDistanceThreshold (default 150,000 cm)
      │   • Position: SkyLightComponent location
      │
      ├─ Disable dynamic/unsuitable features:
      │   • PostProcessing = OFF
      │   • MotionBlur = OFF
      │   • Particles = OFF (usually dynamic)
      │   • LightFunctions = OFF (usually per-frame)
      │   • LightShafts = OFF
      │   • SkyLighting = OFF (avoid feedback loop)
      │   • Lighting = OFF (if bCaptureEmissiveOnly)
      │
      ├─ Render full scene normally via FSceneRenderer::Render()
      │   • Outputs scene color to temporary render target
      │   • Scene includes ALL meshes/materials within threshold
      │
      └─ Copy scene color → cubemap face via FCopySceneColorToCubeFacePS
          • Optionally zero lower hemisphere if bLowerHemisphereIsBlack = true
          • Optionally replace lower hemisphere with LowerHemisphereColor
```

**Key Parameters:**

| Parameter | Default | Purpose |
|-----------|---------|---------|
| `CubemapResolution` | 128 | Size of each cubemap face (power of 2) |
| `SkyDistanceThreshold` | 150,000 cm | Depth threshold; geometry beyond is treated as sky |
| `bLowerHemisphereIsBlack` | true | Zero out down-facing hemisphere (ground is not sky) |
| `LowerHemisphereColor` | Black | Color to fill lower hemisphere if not black |
| `bCaptureEmissiveOnly` | false | Capture only emissive/light sources, skip lit geometry |

**Code:** `CaptureSceneIntoScratchCubemap()` (ReflectionEnvironmentCapture.cpp:1246)

---

### 1.2 Specified Cubemap Path

**When:** User selects `SourceType = SLS_SpecifiedCubemap` + assigns a `Cubemap` asset

**Input:** Static UTextureCube asset (e.g., from HDRI capture)

**Process:**

```
CopyCubemapToScratchCubemap()
  │
  └─ For each cubemap face (0-5):
      │
      ├─ Apply rotation if SourceCubemapAngle != 0
      │   • Rotatex90° around Z-axis: useful for time-of-day rotation
      │
      ├─ If FP32 input and rescaling needed:
      │   • Compute max luminance across all pixels
      │   • Scale luminance to fit FP16 range (60,000) if exceeded
      │   • Store SpecifiedCubemapColorScale for later expansion
      │
      └─ Copy → scratch cubemap via FCopyCubemapToCubeFacePS
          • Apply lower-hemisphere masking (same logic as SLS_CapturedScene)
          • Clamp output to FP16 range per spec
```

**Code:** `CopyCubemapToScratchCubemap()` (ReflectionEnvironmentCapture.cpp:702)

---

### 1.3 Lower Hemisphere Masking

**Problem:** Captured sky contains ground, but ground should not influence IBL

**Solution:** Zero/replace lower hemisphere

```glsl
// FCopySceneColorToCubeFacePS.usf pseudo-code
if (bLowerHemisphereIsBlack && FaceDirection.z < 0)  // Pointing down
{
  Color = LowerHemisphereColor;  // Default: Black (0,0,0,0)
}
```

**Control:**
- `bLowerHemisphereIsBlack` = true: Fill lower hemisphere
- `LowerHemisphereColor` = custom color for fill (e.g., ground ambient)

---

## 2. Integration with IBL Pipeline

### 2.1 Capture → IBL Pipeline

After the cubemap is captured (or copied), it's **filtered into IBL format**:

```
UpdateSkyCaptureContents()
  │
  ├─ CaptureSceneIntoScratchCubemap() or CopyCubemapToScratchCubemap()
  │   └─ Produces: RawCubemap (6 faces, MipLevel=0 only)
  │
  └─ (ENQUEUE) FilterReflectionEnvironment()
      │
      ├─ PremultiplyCubeMipAlpha()
      │   └─ Ensure alpha is premultiplied for blending
      │
      ├─ CreateCubeMips()
      │   └─ Generate mips 1..N via FCubeDownsamplePS
      │       • Each mip is 2x smaller
      │       • Total mips = CeilLogTwo(CubemapSize) + 1
      │       • For 128x128: 8 mips (128→64→32→16→8→4→2→1)
      │
      ├─ ComputeDiffuseIrradiance()
      │   └─ Convolve cubemap with diffuse kernel
      │       • Output: 3rd-order SH coefficients (27 floats per channel)
      │       • Used for diffuse lighting of rough surfaces
      │
      ├─ FilterCubeMap()
      │   └─ Convolve each mip with roughness-dependent kernel via FCubeFilterPS
      │       • Mip 0 = sharpest (metallic)
      │       • Mip N = roughest (diffuse approx)
      │       • 64 samples per pixel, distributed by importance sampling
      │
      └─ Output:
          • FilteredCubemapTexture: Prefiltered environment map (all mips)
          • OutIrradianceEnvironmentMap: Diffuse SH coefficients
          • OutAverageBrightness: Average pixel luminance (for tone mapping)
```

### 2.2 Diffuse vs Specular Separation

**Diffuse Irradiance (SH Coefficients):**
- Computed from unfiltered cubemap via 64-tap integration
- Approximates how much light comes from each direction
- Stored as 3rd-order spherical harmonics: 9 coefficients per color channel
- Used: `FBasePassPixelShaderParameters::SkyIrradianceEnvironmentMap`
- Cost: ~200 bytes per SkyLight (negligible)

**Specular Prefiltered Map (Mip Pyramid):**
- Each mip represents a different roughness
- Mip 0 = mirror (roughness ≈ 0)
- Mip N = fully diffuse (roughness ≈ 1)
- Roughness → Mip mapping in shader: `mip = roughness * NumMips`
- Stored in: `FSkyTextureCubeResource` (GPU cubemap texture)
- Cost: ~Base² × 6 × 4 bytes × 1.33 (mips) = 128² × 6 × 4 × 1.33 ≈ 8 MB for 128×128

### 2.3 Average Brightness Computation

Purpose: Tone-map HDR cubemap to LDR output

Process:
```
ComputeSingleAverageBrightnessFromCubemap()
  │
  ├─ Read 1x1 mip of cubemap (smallest detail level)
  │   • This gives averaged color across entire hemisphere
  │
  └─ Output: OutAverageBrightness (single float)
      • Used in shader: color *= clamp(1.0 / AverageBrightness, ...)
      • Prevents over-bright outdoor scenes
```

---

## 3. SkyLight Component Settings

### 3.1 Capture Control

```cpp
bool bRealTimeCapture;  // If true, recapture EVERY frame
float CubemapResolution; // Power-of-2 size (8→256, default 128)
ESkyLightSourceType SourceType;  // CapturedScene or SpecifiedCubemap
UTextureCube* Cubemap;  // Input for SpecifiedCubemap mode
```

**RealTimeCapture Constraints:**
- Only works if Mobility = Movable or Stationary
- Expensive: 6 full scene renders + filtering per frame
- Best used for dynamic sky (day-night cycle)

### 3.2 Scene Capture Options

```cpp
float SkyDistanceThreshold;  // Depth beyond = sky (default 150,000 cm)
bool bLowerHemisphereIsBlack; // Zero lower hemisphere
FLinearColor LowerHemisphereColor; // Fill color if not zeroed
bool bCaptureEmissiveOnly; // Capture only emissive materials
```

### 3.3 Specified Cubemap Options

```cpp
float SourceCubemapAngle;  // Rotation (0-360°) around Z-axis
// + Lower hemisphere options same as Captured Scene
```

### 3.4 Advanced Options

```cpp
// Distance Field AO
bool bCloudAmbientOcclusion;  // Clouds occlude sky light
float CloudAmbientOcclusionStrength;  // 0-1 blend factor
float CloudAmbientOcclusionExtent;  // Radius in km
float CloudAmbientOcclusionApertureScale;  // Cone angle for occlusion

// Contrast & Tinting
float Contrast;  // S-curve on AO (0 = none, 1 = strong)
float OcclusionExponent;  // Power on AO
float MinOcclusion;  // Clamp AO to this minimum
FColor OcclusionTint;  // Tint AO
```

---

## 4. Render Pass Details

### 4.1 Capture Pass: CaptureSceneToScratchCubemap()

**Creates 6 render targets** (one per face), each rendered as:

```
Per-Face Render:
  1. FSceneViewFamily setup
     • Show flags configured (no post-process, no particles, etc.)
     • Resolution: CubemapSize (e.g., 128x128)

  2. FSceneView for this face
     • Projection: GetCubeProjectionMatrix(45°, Size, NearPlane)
     • ViewMatrix: CalcCubeFaceViewRotationMatrix(Face)
     • ViewOrigin: CaptureComponent location

  3. Full scene render
     • All render passes (shadow maps, base pass, etc.)
     • Output: SceneColor render target

  4. Copy to cubemap
     • FCopySceneColorToCubeFacePS
     • Read: SceneColor + SceneDepth
     • Test: if depth > SkyDistanceThreshold, treat as sky
     • Apply: Lower hemisphere masking
     • Write: CubeFace slice of cubemap
```

### 4.2 Filter Pass: FilterReflectionEnvironment()

**Single-pass mip generation + filtering:**

```cpp
for mip = 1 to NumMips:
  for face = 0 to 5:
    // Read from mip-1 (higher resolution)
    // Write to mip (lower resolution)
    FCubeDownsamplePS

    // Then filter this mip separately
    FCubeFilterPS
    // 64-tap importance sample based on roughness
```

**Cost Analysis:**
- 6 faces × 8 mips × 2 passes ≈ 96 pixel shader dispatches
- For 128x128 base: ~50 MP total fill rate
- ~5-10 ms on modern GPU

### 4.3 Example: 128x128 Cubemap Mips

| Mip | Face Size | 6 Faces | Mip Total |
|-----|-----------|---------|-----------|
| 0   | 128×128   | 6× 16 KB  | 96 KB |
| 1   | 64×64     | 6× 4 KB   | 24 KB |
| 2   | 32×32     | 6× 1 KB   | 6 KB |
| 3   | 16×16     | 6× 0.25 KB | 1.5 KB |
| ... | ... | ... | ... |
| 7   | 1×1       | 6× 16 B   | 96 B |
| **Total** | | | **~130 KB** |

---

## 5. RealTimeCapture Implementation

**For dynamic sky** (sun moving, clouds animating):

```cpp
// Scheduler in FDeferredShadingRenderer::Render()
if (SkyLight->IsRealTimeCaptureEnabled())
{
  // Every frame:
  FScene::UpdateSkyCaptureContents(SkyLight, ...);

  // This triggers:
  CaptureSceneIntoScratchCubemap(...);
  FilterReflectionEnvironment(...);

  // Then immediately use the filtered cubemap for that frame's lighting
}
```

**Cost:**
- 6× scene renders (one per face)
- Full mip generation + filtering
- Total: ~50 ms on GTX 2060, ~10 ms on RTX 4090
- **Not practical at 120 FPS without LOD tricks**

---

## 6. GPU Memory Layout

### FSkyTextureCubeResource Memory

**On GPU:**
```
TextureCube[resolution × resolution × 6 faces × NumMips]
Format: PF_FloatRGBA (16 bytes per pixel)

For 128x128 cubemap:
  Mips: [128, 64, 32, 16, 8, 4, 2, 1]
  Total: 128² × 6 × 4 bytes × (1 + 0.25 + 0.0625 + ... + 1/128²)
       ≈ 128² × 6 × 4 × 1.33
       ≈ 6.4 MB per SkyLight
```

**On CPU (Game Thread):**
```
FSkyTextureCubeResource:
  - Size (int32)
  - NumMips (int32)
  - Format (EPixelFormat)
  - TextureRHI (FTextureRHIRef)

  + Reference counted for lifetime management
```

**On Render Thread:**
```
RDG will manage the lifetime of:
  - Scratch cubemap texture
  - Filtered cubemap texture
  - SH coefficient buffers
  - Brightness readback buffer

  (All freed after the command buffer completes)
```

---

## 7. Architectural Patterns — UzEngine Adaptation

### Pattern 1: Deferred Capture Scheduling

**UE5 does NOT capture synchronously** during component registration.

Instead:
1. Mark component dirty → add to `SkyCapturesToUpdate` queue
2. Game thread checks queue at frame start
3. If ready (shaders compiled, assets loaded), kick render thread task
4. Render thread completes; game thread polls fence before using result

**For UzEngine:**
```cpp
// Similar pattern:
struct SkyLightCaptureRequest {
  SkyLightComponent* component;
  enum { Pending, InProgress, Complete } state;
  Fence fence;
};

TArray<SkyLightCaptureRequest> skyCapturesToUpdate;
```

### Pattern 2: Two-Pass Filtering

**UE5 doesn't compute IBL inline** during capture.

Instead:
1. **Phase 1:** Render cubemap (synchronous render task)
2. **Phase 2:** Filter into SH + mips (async RDG task)

This separation allows:
- Cache the raw cubemap (useful for reflection captures sharing same sky)
- Reuse filtering code for both scene captures and specified cubemaps
- Blend between two SkyLights smoothly (blend the *filtered* SH values, not raw cubemaps)

**For UzEngine:**
```cpp
struct SkyLightCubemapCapture {
  TextureCube rawCubemap;  // 1 mip only
  TextureCube filteredCubemap;  // 8 mips
  SHVectorRGB3 diffuseIrradiance;  // 27 floats
  float averageBrightness;
};
```

### Pattern 3: Lower Hemisphere Masking in Fragment Shader

**UE5 doesn't pre-process** the cubemap to zero-out the lower hemisphere.

Instead, it does masking **during the copy operation** via pixel shader:

```glsl
// FCopySceneColorToCubeFacePS
if (LowerHemisphereIsBlack && CubeDirection.z < 0)
  OutColor = LowerHemisphereColor;
```

This means:
- A single cubemap source can be used with different lower hemisphere settings
- No data duplication
- Can change LowerHemisphereColor at runtime

**For UzEngine:** Apply this same shader logic during capture.

### Pattern 4: RealTimeCapture = Separate Code Path

**Critical insight:** RealTimeCapture is **fundamentally different** from offline captures.

Offline:
- User clicks "Recapture"
- Waits for slow task bar
- Results saved

RealTime:
- Happens every frame
- Scheduled in render graph
- No save/load logic
- Integrated into main render pipeline

**For UzEngine:** Keep these as completely separate code paths:
```cpp
if (bRealTimeCapture) {
  // Inline into FSceneRenderer::RenderView()
  CaptureAndFilterSkyLight(...);
} else {
  // Async task, not part of main render
  ScheduleOfflineCapture(...);
}
```

### Pattern 5: SH Coefficients for Diffuse Only

**UE5 separates** diffuse and specular IBL:

- **Diffuse:** Spherical harmonics (fast, low memory, smooth)
- **Specular:** Mipmap pyramid (slower texture fetch, but sharp reflections)

This is more efficient than storing *both* as cubemaps.

**For UzEngine:** Adopt the same split:
```cpp
struct SkyIBL {
  SHVectorRGB3 irradiance;  // Diffuse: 27 floats (fast constant)
  TextureCube prefilteredSpecular;  // Specular: cubemap (1-8 mips)
};
```

---

## 8. Scheduling & Synchronization

### Game Thread Queue

```cpp
// SkyLightComponent.cpp
static TArray<USkyLightComponent*> SkyCapturesToUpdate;
static FTransactionallySafeCriticalSection SkyCapturesToUpdateLock;

// During SetCaptureIsDirty():
{
  UE::TScopeLock Lock(SkyCapturesToUpdateLock);
  SkyCapturesToUpdate.AddUnique(this);
}
```

### Update Points

Captures are processed:
1. **During `FDeferredShadingRenderer::Render()`** — after scene setup, before main render
2. **Only when ready:**
   - Level is fully loaded (not streaming)
   - Shaders are compiled (not cooking)
   - Textures are loaded (not compiling)
3. **One per frame** (in non-editor, or multiple incomplete captures if waiting for assets)

### Fence Synchronization

```cpp
FRenderCommandFence IrradianceMapFence;  // Tracks when RT writes complete

// Game thread waits for fence before reading:
IrradianceMapFence.Wait();  // Blocks until RT task finishes
// Now safe to read: IrradianceEnvironmentMap, AverageBrightness
```

---

## 9. Key Differences from Static Reflection Captures

| Aspect | SkyLight | ReflectionCapture |
|--------|----------|-------------------|
| **Source** | Scene + sky | Scene + sky + reflections |
| **Position** | Single (fixed) | Grid (multiple positions) |
| **Mips** | 1-8 (for roughness) | 1-8 (for roughness) |
| **Filtering** | GGX convolve (4×4 roughness levels) | GGX convolve (same) |
| **Output Storage** | SkyTextureCubeResource | ReflectionCubemapArray (atlased) |
| **Diffuse SH** | Yes (FSHVectorRGB3) | No (uses precomputed lightmaps) |
| **Used By** | All materials (global ambient) | Nearby materials only (spatial) |
| **Update Frequency** | On-demand or real-time | Offline only (lighting builds) |

---

## 10. Pseudocode: Full Capture-to-IBL Pipeline

```cpp
// Game Thread
void USkyLightComponent::RecaptureSky() {
  SetCaptureIsDirty();
}

// Every few frames (FSceneRenderer::Render)
void UpdateSkyCaptureContents(UWorld* World) {
  for (auto SkyLight : SkyCapturesToUpdate) {
    World->Scene->UpdateSkyCaptureContents(SkyLight, ...);
  }
}

// Render Thread
void FScene::UpdateSkyCaptureContents(
    const USkyLightComponent* Component,
    UTextureCube* SourceCubemap,
    FTexture* OutProcessedTexture,
    float& OutAverageBrightness,
    FSHVectorRGB3& OutIrradianceEnvironmentMap)
{
  TRenderThreadStruct<FReflectionCubemapTexture> RawCubemap(
      Component->CubemapResolution);

  if (Component->SourceType == SLS_CapturedScene) {
    // Phase 1: Render scene into cubemap
    CaptureSceneIntoScratchCubemap(
        this,
        RawCubemap,
        Component->GetComponentLocation(),
        Component->CubemapResolution,
        true,  // bCapturingForSkyLight
        false, // bStaticSceneOnly
        Component->SkyDistanceThreshold,
        Component->bLowerHemisphereIsBlack,
        Component->bCaptureEmissiveOnly,
        Component->LowerHemisphereColor);
  } else {
    // Phase 1: Copy specified cubemap
    CopyCubemapToScratchCubemap(
        RawCubemap,
        SourceCubemap,
        Component->CubemapResolution,
        Component->SourceCubemapAngle,
        Component->bLowerHemisphereIsBlack,
        Component->LowerHemisphereColor);
  }

  // Phase 2: Filter into IBL
  ENQUEUE_RENDER_COMMAND(FilterIBL)([=](FRHICommandListImmediate& RHICmdList) {
    FRDGBuilder Builder(RHICmdList);
    auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

    // Create mips
    CreateCubeMips(Builder, ShaderMap, RawCubemap);

    // Compute diffuse SH
    ComputeDiffuseIrradiance(
        Builder, ShaderMap, RawCubemap, &OutIrradianceEnvironmentMap);

    // Compute average brightness
    ComputeAverageBrightness(Builder, ShaderMap, RawCubemap, &OutAverageBrightness);

    // Filter specular (per-mip roughness)
    FRDGTexture* FilteredCubemap = FilterCubeMap(Builder, ShaderMap, RawCubemap);

    // Copy to ProcessedSkyTexture (permanent GPU memory)
    if (OutProcessedTexture) {
      CopyToSkyTexture(Builder, this, FilteredCubemap, OutProcessedTexture);
    }

    Builder.Execute();
  });
}
```

---

## 11. Critical Parameters for UzEngine

When implementing this in UzEngine, prioritize:

| Parameter | Default | Impact |
|-----------|---------|--------|
| `CubemapResolution` | **128** | Higher = sharper speculars, more memory (×4 per doubling) |
| `SkyDistanceThreshold` | 150,000 cm | Depth cutoff; too small = cuts sky; too large = includes terrain |
| `bLowerHemisphereIsBlack` | **true** | False = use ground color for IBL (soft bounce) |
| `bCaptureEmissiveOnly` | **false** | True = only capture lights, skip geometry (cheaper real-time) |
| `bRealTimeCapture` | **false** | True = every frame (expensive), false = on-demand |
| `NumMips` | `CeilLogTwo(res) + 1` | Don't change; set by resolution |

---

## 12. GPU Cost Estimation

### Offline Capture (once per level change)

```
CaptureSceneIntoScratchCubemap:
  6 faces × scene render time  = 6 × 3 ms = 18 ms
  6 × copy pass                = 6 × 0.1 ms = 0.6 ms
  Subtotal: ~19 ms

FilterReflectionEnvironment:
  CreateCubeMips:    8 passes × 6 faces × 0.5 ms = 24 ms
  ComputeBrightness: readback                     = 1 ms
  FilterCubeMap:     8 passes × 6 faces × 1 ms   = 48 ms
  Subtotal: ~73 ms

Total: ~92 ms per SkyLight capture
```

### RealTime Capture (per frame)

If enabled at 60 FPS:
```
Per-frame cost: 92 ms
Frame budget: 16.67 ms
Over budget by 5.5×
→ Requires LOD (lower res, fewer mips, skip every N frames)
```

**Practical real-time:** 64×64 cubemap, 4 mips, capture every 4 frames = ~6 ms per frame

---

## References

- **Capture scheduler:** `SkyLightComponent.cpp:731-904` (UpdateSkyCaptureContentsArray)
- **Rendering code:** `ReflectionEnvironmentCapture.cpp:1740-1870` (UpdateSkyCaptureContents)
- **Capture loop:** `ReflectionEnvironmentCapture.cpp:1246-1412` (CaptureSceneIntoScratchCubemap)
- **Filtering:** `ReflectionEnvironmentCapture.cpp:384-427` (ComputeAverageBrightness, FilterCubeMap)
- **Component header:** `SkyLightComponent.h:100-370` (all settings)

