<!-- doc-meta
type: analysis
domain: rendering
title: UE5 SkyLight Cubemap Capture—Quick Reference
date: 2026-03-15
status: complete
-->

# UE5 SkyLight Cubemap Capture — Quick Reference

## The Pipeline at a Glance

```
SkyLightComponent (user control)
  ↓
[Deferred Queue] — waits for assets to load
  ↓
CaptureSceneIntoScratchCubemap() OR CopyCubemapToScratchCubemap()
  ├─ Render 6 faces of scene/cubemap
  ├─ Apply lower-hemisphere masking
  └─ Output: Raw cubemap (1 mip)
  ↓
FilterReflectionEnvironment() [async RDG]
  ├─ CreateCubeMips (8 levels for roughness)
  ├─ ComputeDiffuseIrradiance → SH coefficients
  ├─ FilterCubeMap (per-mip GGX convolve)
  ├─ ComputeAverageBrightness → scalar
  └─ Output: Filtered cubemap + SH + brightness
  ↓
FSkyTextureCubeResource (GPU storage)
  ├─ Mipmap pyramid (6 MB for 128×128)
  └─ Used for specular IBL lookups (per-pixel shader)
  +
FSHVectorRGB3 (27 floats)
  └─ Used for diffuse IBL (constant per frame)
```

## Two Capture Modes

### 1. Captured Scene (SLS_CapturedScene)
```cpp
// Renders the live scene + sky atmosphere
// 6 full scene renders @ CubemapResolution
// Respects SkyDistanceThreshold (depth cutoff)

Cost: ~18 ms (6 renders) + 73 ms (filtering) = 91 ms total
Output: Full capture with all materials, lights, fog, clouds
```

### 2. Specified Cubemap (SLS_SpecifiedCubemap)
```cpp
// Copies existing HDR cubemap asset
// 6 copy passes with shader processing
// Applies rotation & lower-hemisphere masking

Cost: ~0.6 ms (copies) + 73 ms (filtering) = 73.6 ms total
Output: Same filtering, faster capture
```

## Key Parameters

| Setting | Default | What It Does |
|---------|---------|--------------|
| `CubemapResolution` | 128 | Face size (8→256, power-of-2) |
| `SkyDistanceThreshold` | 150,000 cm | Depth beyond = sky (avoids terrain) |
| `bLowerHemisphereIsBlack` | true | Zero out downward-facing light |
| `LowerHemisphereColor` | Black | Fill color if not zeroed |
| `bRealTimeCapture` | false | Capture every frame (expensive) |
| `bCaptureEmissiveOnly` | false | Capture light only, skip geometry |
| `SourceType` | SLS_CapturedScene | Scene or cubemap source |

## Scheduling

**NOT synchronous.** Captures are queued and executed later:

```cpp
SetCaptureIsDirty()
  → SkyCapturesToUpdate.Add(this)
  → Checked during FSceneRenderer::Render()
  → When ready: UpdateSkyCaptureContents() executes
  → IrradianceMapFence tracks completion
```

**Game thread can only read results AFTER fence:**
```cpp
IrradianceMapFence.Wait();  // Block until RT completes
float brightness = AverageBrightness;  // Safe now
FSHVectorRGB3 sh = IrradianceEnvironmentMap;  // Safe now
```

## Output Storage

### On GPU
```
FSkyTextureCubeResource::TextureCubeRHI
├─ Format: PF_FloatRGBA (FP32 per channel)
├─ Mips: 8 (128→64→32→16→8→4→2→1)
└─ Memory: 128² × 6 × 4 bytes × 1.33 ≈ 6.4 MB

Owned by: USkyLightComponent::ProcessedSkyTexture
Lifetime: Persists entire scene (can be reused)
Used by: Material shaders for specular IBL
```

### On CPU (Game Thread)
```
FSHVectorRGB3 IrradianceEnvironmentMap
├─ 9 float4 coefficients (27 floats total)
└─ Size: 108 bytes

Used by: Diffuse IBL in base pass shaders
Lifetime: Updated when capture completes
Cost: Minimal; constant lookup per-pixel
```

### On Render Thread (Transient)
```
RDG Intermediate Textures
├─ Raw cubemap (6 faces)
├─ Filtered cubemap
└─ Brightness readback buffer

Freed: After execute() completes
Cost: Temporary VRAM; doesn't accumulate
```

## Diffuse vs Specular Split

### Diffuse (SH Coefficients)
- **Storage**: 27 floats per light
- **Cost**: 108 bytes
- **Quality**: Smooth, representative
- **Usage**: Every material, every pixel
- **Lookup**: Constant buffer fetch

### Specular (Mipmap Pyramid)
- **Storage**: 6.4 MB (128×128 × 6 × 1.33 mips)
- **Cost**: Per-frame texture memory
- **Quality**: Sharp reflections (8 roughness levels)
- **Usage**: Reflective materials (metallic/smooth)
- **Lookup**: Texture fetch (roughness → mip)

## Lower Hemisphere Masking

**Applied during capture, not preprocessing:**

```glsl
// In FCopySceneColorToCubeFacePS shader
if (bLowerHemisphereIsBlack && CubeDirection.z < 0)  // Pointing down
{
  OutColor = LowerHemisphereColor;  // Usually black
}
```

**Why this design:**
- Single source cubemap, multiple masking options
- Can change `LowerHemisphereColor` at runtime
- No data duplication or preprocessing step
- Works for both capture modes

## Filtering Details

### CreateCubeMips
```
Input:  Raw 128×128 cubemap (1 mip)
Output: 8 mips (128→64→32→...→1)
Method: Bilinear downsample (FCubeDownsamplePS)
Cost:   ~24 ms
```

### ComputeDiffuseIrradiance
```
Input:  Full cubemap
Output: SH coefficients (9 per channel)
Method: Monte Carlo 64-tap integration
Cost:   ~10 ms
```

### FilterCubeMap
```
Input:  All 8 mips
Output: GGX-filtered all 8 mips
Method: Importance sampling, 64 taps per pixel
Cost:   ~48 ms
```

### ComputeAverageBrightness
```
Input:  1×1 mip (smallest detail)
Output: Single float luminance
Method: Read GPU texture (readback)
Cost:   ~1 ms
```

## RealTimeCapture

**Only if `bRealTimeCapture = true` AND Mobility = Movable or Stationary**

```cpp
Every frame:
  UpdateSkyCaptureContents(SkyLight, ...);

Cost: 92 ms/frame @ 60 FPS = 5.5× over budget
Solution: LOD
  • 64×64 instead of 128×128 (−75% cost)
  • 4 mips instead of 8 (−50% cost)
  • Capture every 4 frames (−75% cost)
  • Combined: ~6 ms/frame (practical)
```

## Architecture Insights for UzEngine

### Pattern 1: Deferred Scheduling
Don't capture synchronously in `RegisterComponent()`. Instead:
- Mark dirty
- Queue for later
- Process when ready

### Pattern 2: Two-Phase Filtering
Separate capture from IBL generation:
- Allows caching raw cubemap
- Reuse filtering for different sources
- Smooth blending between two SkyLights

### Pattern 3: Shader-Based Masking
Don't preprocess lower hemisphere. Apply in shader:
- Flexibility (can change color dynamically)
- No data duplication
- Minimal cost

### Pattern 4: Separate RealTime Path
RealTime ≠ Offline. Keep as distinct code:
```cpp
if (bRealTimeCapture) {
  // Inline into RenderGraph each frame
} else {
  // Async task, not part of main render
}
```

### Pattern 5: SH + Mips Hybrid
Use SH for diffuse (efficient), mips for specular (sharp):
- Diffuse: 108 bytes/light
- Specular: Per-mip texture fetch
- Best of both worlds

## File References

| File | Lines | Purpose |
|------|-------|---------|
| SkyLightComponent.h | 100-370 | Component definition + settings |
| SkyLightComponent.cpp | 731-904 | UpdateSkyCaptureContentsArray (scheduler) |
| ReflectionEnvironmentCapture.cpp | 1740-1870 | UpdateSkyCaptureContents (main pipeline) |
| ReflectionEnvironmentCapture.cpp | 1246-1412 | CaptureSceneIntoScratchCubemap |
| ReflectionEnvironmentCapture.cpp | 384-427 | FilterReflectionEnvironment + helpers |

## One-Liner Pseudocode

```cpp
// Game thread
RecaptureSky() → SetCaptureIsDirty() → SkyCapturesToUpdate.Add(this)

// Render thread (when ready)
UpdateSkyCaptureContents()
  → CaptureSceneIntoScratchCubemap() [render scene]
  → ENQUEUE FilterReflectionEnvironment() [async filtering]
     ├─ CreateCubeMips()
     ├─ ComputeDiffuseIrradiance() → IrradianceEnvironmentMap
     ├─ FilterCubeMap()
     └─ ComputeAverageBrightness() → AverageBrightness

// Shaders
Material.BaseLighting
  += IrradianceEnvironmentMap * (1 - metallic) [diffuse]
  += ProcessedSkyTexture.Sample(roughness) * metallic [specular]
```
