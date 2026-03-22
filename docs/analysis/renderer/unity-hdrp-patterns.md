<!-- doc-meta title="Unity HDRP Rendering Patterns for UzEngine" -->

# Unity HDRP Rendering Patterns for UzEngine

**Analysis Date**: 2026-03-22
**Status**: Complete reference for UzEngine visual quality parity
**Scope**: HDRP 16+ (Unity 2023 LTS+)

---

## Executive Summary

Unity's High Definition Render Pipeline (HDRP) is designed for **maximum visual quality** in real-time AAA rendering. This analysis extracts HDRP's technical approach to lighting, shadows, reflections, post-processing, and materials for adaptation into UzEngine's DX12/Vulkan renderer.

### Key Principles HDRP Enforces
1. **Physical light units** — lux, lumens, candelas (not arbitrary [0,1] ranges)
2. **Deferred + Forward hybrid** — deferred for unlimited lights, forward for alpha/special cases
3. **Quality tiers** — configurable per platform (mobile, console, PC ultra)
4. **Probe-based indirect lighting** — APV (Adaptive Probe Volumes) replaces old probe grids
5. **Temporal stability** — TAA mandatory, all effects accumulate across frames
6. **Layer separation** — light layers decouple lights from meshes without performance cost

---

## 1. LIGHTING MODEL

### 1.1 Physical Light Units

HDRP uses real-world lighting units, not arbitrary values:

| Unit | Type | Use | Typical Range |
|------|------|-----|----------------|
| **Lux** | Illuminance | Directional lights (sun, moon) | 50,000–120,000 (sun) |
| **Lumens** | Luminous flux | Point/spot lights (bulbs, fires) | 500–5,000 |
| **Candela** | Luminous intensity | Used internally; not exposed to user |  |

#### Sun (Directional Light)
```
Physical intensity (lux) = Luminous intensity (cd) / distance²
Typical daylight: 100,000 lux at midday
Typical overcast: 10,000 lux
Typical twilight: 100 lux
Typical night with moon: 0.25 lux
```

**HDRP Implementation**:
```csharp
// HDAdditionalLightData.cs (C#)
public float intensity = 1.0f;  // Multiplier on physical value
public float luxAtDistance = 1.0f; // For point lights: apparent lux at 1m
public LightUnit lightUnit = LightUnit.Lux; // Enum: Lux, Lumens, Ev100
```

#### Point/Spot Lights
```
HDPointLightData:
  - intensity (lux or lumens)
  - range (meters)
  - inner/outer angle (degrees for spots)
  - shadows enabled (boolean)
```

**Default Values**:
- Point light 100W equivalent ≈ 500 lumens
- Spot light (stadium) ≈ 5,000 lumens
- Falloff: quadratic (1/r²) physically correct

### 1.2 Area Lights (LTC)

HDRP uses **Linearly Transformed Cosines (LTC)** for physically plausible area light shading.

#### LTC Technique Overview
```
LTC maps texture coordinates from cosine lobe → fitted LTC distribution
Enables per-pixel evaluation of rectangular light intensity
Approximation error < 3% vs. analytical integration
```

#### HDAreaLight Data
```
Rectangle area light (most common):
  - width (meters)
  - height (meters)
  - intensity (lumens)
  - two-sided (boolean) — emits from both sides
  - shadows (baked only; no realtime area shadows)
  - color (linear RGB, HDR)
```

**LTC Evaluation** (in shader):
```hlsl
// Fetch LTC matrix for current roughness
float2 uv = float2(roughness, dot(N, V));
float3 ltcMatrixCol0 = tex2D(ltcMatrixTexture, uv * 0.5 + float2(0.0, 0.0)).rgb;
float3 ltcMatrixCol1 = tex2D(ltcMatrixTexture, uv * 0.5 + float2(0.0, 0.5)).rgb;
float3 ltcMatrixCol2 = cross(ltcMatrixCol0, ltcMatrixCol1);

// Compute light contribution
float3 rectCornerWorldPos[4] = { ... };  // 4 corners of area light
float ltcIntensity = EvaluateLTC(rectCornerWorldPos, N, V, ltcMatrixCol0, ltcMatrixCol1, ltcMatrixCol2);
float3 lightColor = areaLight.intensity * areaLight.color;
```

**Texture Assets**:
- `LTC_D.tga` — LTC matrix column 0 (width × height = roughness × cos_theta)
- `LTC_M.tga` — LTC matrix column 1
- Both are 64×64 FP16 textures, precomputed offline

**Integration**: Area lights are only used for **baked indirect lighting** in HDRP. Realtime area lights are approximated as spheres (for fast shadow evaluation).

### 1.3 Screen-Space GI (SSGI)

HDRP's **Screen-Space Global Illumination** (SSGI) ray-traces screen-space surfaces to approximate indirect diffuse lighting.

#### Pipeline
```
1. Render depth + normal to G-buffers
2. Ray-trace from each pixel outward (Hilbert curve order)
3. Compare ray depth vs. depth buffer — hit when ray_depth > depth_buffer
4. Sample diffuse from hit position + neighborhood
5. Temporal reprojection + history blending
```

#### Key Parameters

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| `rayLength` | 0.5 | 0.1–1.0 | Screen-space ray distance (as fraction of screen height) |
| `raySteps` | 16 | 4–64 | Samples per ray (quality/performance tradeoff) |
| `thickness` | 0.1 | 0.01–1.0 | Depth tolerance for ray hits (prevents surface acne) |
| `useHistory` | true | — | Enable temporal history accumulation |
| `historyBlend` | 0.95 | 0.8–0.99 | Bias toward previous frame (> 0.95 = ghosting) |
| `sampleCount` | 1 | 1–4 | Rays per pixel per frame (1 = temporal accumulation) |
| `maxDistance` | 1000 | — | World-space cutoff beyond which GI = 0 |

#### Output
```
Result: low-frequency indirect diffuse (monochrome or RGB)
Quality: motion artifacts common without TAA
Typical contribution: 20–40% of total scene brightness
```

#### Limitations
- **Screen-dependent**: Off-screen surfaces don't contribute (GI "pops" at screen edges)
- **Temporal artifacts**: Requires 8+ frames for convergence without ghosting
- **Normal-sensitive**: Relies on accurate G-buffer normals

### 1.4 Ray-Traced GI (Optional, PC-only)

HDRP can optionally use DXR for true global illumination instead of SSGI.

```
Pipeline:
  1. Trace rays from pixels outward (AnyHit shaders)
  2. Sample hit surface albedo + normal from closest hit
  3. Recursive: optional bounces (usually 1–2 to avoid cost)
  4. Temporal accumulation across frames
```

**Trade-offs**:
- **Pros**: Physically correct indirect lighting, off-screen surfaces work, no temporal artifacts
- **Cons**: 2–5× slower than SSGI, requires DXR capable GPU, memory overhead

**Default**: SSGI only (DXR disabled)

### 1.5 Light Layers & Light Probe Groups

#### Light Layers
```csharp
// HDAdditionalLightData.cs
public uint lightLayerMask = 1 << 0;  // Which layers this light affects (32-bit)

// On mesh renderer
public uint renderingLayerMask = 1 << 0;  // Which layers this mesh receives from
```

**Purpose**: Decouple lights from meshes without performance cost. A mesh on layer 5 only receives light from lights on layer 5.

**Use Cases**:
- Screen-space GI has separate layer mask
- UI lights don't affect 3D geometry
- Character lights (rims, portraits) isolated from world lights

#### Light Probe Groups (Deprecated → Adaptive Probe Volumes)

**Old system** (still works):
```
LightProbeGroup:
  - 3D positions array [x, y, z]
  - Baked during build: captures radiance at each position
  - Runtime: sphere interpolation to nearest 4 probes
```

**New system (APV — Recommended)**:
```
AdaptiveProbeVolume:
  - 3D bricks of probes (e.g., 4×4×4 probes per brick)
  - Automatic placement during bake
  - Streaming: only load probes near camera
  - Blending: smooth interpolation between brick boundaries
  - Bake time: hours for large scenes
```

**APV Workflow**:
```
1. Mark scene Volume Probe Placement Override
2. Volume → Bake Probes (offline, can take 12+ hours for AAA scenes)
3. Result: Binary .apvol files (one per streaming section)
4. Runtime: Stream near camera, interpolate SH3 radiance
```

---

## 2. SHADOW SYSTEM

### 2.1 Cascaded Shadow Maps (CSM)

HDRP uses **Cascaded Shadow Maps** (DirectX/UE5 style) for directional lights.

#### Configuration

```csharp
// HDAdditionalLightData.cs (DirectionalLight)
public int cascadeCount = 4;
public float[] cascadeRatios = {0.1f, 0.3f, 0.5f, 1.0f};  // Cascade extents
public float cascadeDistribution = 0.5f;  // Linear vs log distribution
public float shadowNearClip = 0.1f;  // Near plane (meters)
public bool useContactShadows = true;
```

#### Cascade Layout

| Cascade | Typical Distance | Resolution | Use Case |
|---------|------------------|------------|----------|
| 0 (closest) | 0–10m | 2048×2048 | Near character, details |
| 1 | 10–30m | 2048×2048 | Mid-distance |
| 2 | 30–100m | 2048×2048 | Background |
| 3 (farthest) | 100–1000m | 2048×2048 | Far scenery |

**Allocation**: All 4 cascades share a **4096×4096 atlas** (2×2 grid of 2048 maps).

#### Cascade Transitions

HDRP applies **cascade blending** at transitions:
```
Distance to cascade boundary: 2 meters
Blend range: ±1 meter → smooth PCF weight interpolation
Avoids hard transitions between cascade resolutions
```

#### Shadow Bias & Filtering

```csharp
public float shadowBias = 0.01f;  // Constant depth bias (meters)
public float normalBias = 0.1f;   // Normal-space bias (helps surfaces facing away)
public float shadowFilterMode = ShadowFilterMode.PCF_5x5;
  // Options: Point (no filter), PCF_3x3, PCF_5x5, PCF_7x7
```

**Typical values for 2048 texel depth**:
- `shadowBias = 0.005–0.02` (prevents peter-panning)
- `normalBias = 0.1–0.3` (reduces shadow acne on steep surfaces)
- `filterMode = PCF_5x5` (good quality/performance balance)

### 2.2 Contact Shadows

**Screen-space shadows** cast by nearby geometry on itself.

#### Implementation

```
1. Render depth + normal to G-buffer
2. For each pixel, march ray backward along light direction
3. If ray hits depth buffer → contact shadow (fully opaque)
4. Blend with cascade shadow
```

#### Parameters

| Parameter | Default | Effect |
|-----------|---------|--------|
| `length` | 0.5 | Ray march distance (world units) |
| `distanceScale` | 0.5 | Fade range near/far |
| `maxSteps` | 8 | Ray march iterations |
| `fadeDistance` | 100 | Distance at which contact shadows fade to zero |
| `fadeInDist` | 5 | Soft transition at contact shadow boundary |

**Quality**:
- **Pros**: Captures details cascade shadows miss (crevices, cloth, hair)
- **Cons**: Screen-space only, off-screen geometry doesn't cast shadows

**Typical contribution**: 10–20% shadow darkening combined with cascade

### 2.3 Point/Spot Light Shadows

#### Allocation Strategy

```
SharedShadowMap (per-light):
  - Point light: 6 cube faces (2048×2048 each = 24 MB total)
  - Spot light: 1 perspective (2048×2048 = 4 MB)

Total budget: sum(all lit point/spot lights) — automatically culls if over budget
```

#### Quality Settings

| Quality | Point Resolution | Spot Resolution | Filter | PC? | Console? |
|---------|------------------|-----------------|--------|-----|----------|
| Ultra | 2048 | 2048 | PCF_7x7 | ✓ | ✗ |
| High | 1024 | 1024 | PCF_5x5 | ✓ | ✓ |
| Medium | 512 | 512 | PCF_3x3 | ✓ | ✓ |
| Low | 256 | 256 | Point | ✓ | ✓ |

**HDRP Default**: Medium (512×512)

#### Bias & Normal Offset

```
shadowBias = 0.02  // Constant offset
normalBias = 0.1   // Along normal
perspective bias = (1 - light.range / distance) * 0.1  // Prevents back-face shadows
```

### 2.4 Baked Shadow Support

HDRP supports **pre-computed baked shadows** (lightmaps).

```
Renderer:
  - receiveGI = GIReceivedFrom.Lightmap
  - receiveGI = GIReceivedFrom.LightProbes (for dynamic objects)
```

**Typical workflow**:
- Static geometry: baked shadows (instant, no runtime cost)
- Dynamic objects: realtime shadows + contact shadows

---

## 3. REFLECTION SYSTEM

### 3.1 Reflection Probes (Baked & Realtime)

#### Baked Reflection Probes

```csharp
// ReflectionProbe.cs
public int resolution = 128;  // 64, 128, 256, 512
public Cubemap customTexture;
public float intensity = 1.0f;
public Vector3 size;  // Influence volume (box)
public float blendDistance = 1.0f;  // Smooth falloff
public bool boxProjection = true;  // Correct parallax
```

**Typical Usage**:
- One probe per room/major area
- 256×256 cubemaps (8 KB each after compression)
- Baked offline during asset build

**Box Projection** (prevents distortion):
```hlsl
// Given: N (normal), V (view), boxMin, boxMax, probePos
float3 boxProjectedDir = ReflDir;
[unroll]
for (int i = 0; i < 3; ++i) {
    if (abs(ReflDir[i]) > 0.001) {
        float t = (boxMax[i] - probePos[i]) / ReflDir[i];
        if (ReflDir[i] < 0) t = (boxMin[i] - probePos[i]) / ReflDir[i];
        boxProjectedDir[i] = t;
    }
}
```

#### Realtime Reflection Probes

```csharp
public bool realtimeRendering = true;
public RenderTexture customReflectionTexture;
public int refreshMode = ReflectionProbeRefreshMode.EveryFrame;  // or OnAwake, ViaScripting
```

**Cost**: One probe = 6 camera renders/frame → expensive, use sparingly (1–2 max).

#### Probe Blending

```hlsl
// At pixel, find 2–4 nearest probes
float3 reflColor = 0;
float totalWeight = 0;
for (Probe p : nearestProbes) {
    float distToProbe = distance(pixelPos, p.position);
    float weight = 1.0 / (distToProbe + 0.1);  // Inverse distance
    weight *= max(0, 1 - distToProbe / p.blendDistance);  // Falloff
    reflColor += tex2D(p.cubemap, reflDir) * weight;
    totalWeight += weight;
}
reflColor /= totalWeight;
```

### 3.2 Screen-Space Reflections (SSR)

HDRP's SSR is a **Hi-Z ray-tracing** pass for screen-space surfaces.

#### Pipeline

```
1. Build Hi-Z mip chain (1/2, 1/4, 1/8... resolution depth pyramid)
2. For each pixel, ray-march in screen space using Hi-Z mips
3. Coarse trace (top mip) → refine (lower mips)
4. PBR accumulation: blend with analytical reflection probe result
```

#### Parameters

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| `rayLength` | 0.5 | 0.0–1.0 | Maximum ray distance (fraction of screen) |
| `thickness` | 0.1 | 0.01–1.0 | Depth tolerance (prevents false hits) |
| `maxSteps` | 16 | 4–64 | Ray iterations (quality/perf) |
| `useHistory` | true | — | Accumulate across frames (TAA reprojection) |
| `maxRoughness` | 0.8 | 0.0–1.0 | Disable SSR for roughness > this (blur too severe) |

**Accumulation**:
```hlsl
// SSR output is low-frequency due to low sample count
// Accumulate via temporal reprojection
float3 current = SSRRadiance;
float3 history = tex2D(historyBuffer, reprojectedUV);
float3 accumulated = mix(current, history, 0.95);  // 95% history weight
```

### 3.3 Ray-Traced Reflections (DXR, Optional)

```
1. Trace rays into scene (ClosestHit shaders)
2. Sample radiance from direct + indirect at hit point
3. Recursive bounces (typically 1–2)
4. Temporal accumulation
```

**Parameters**:
- `rayCount`: 1, 2, 4 rays per pixel (1 = full 4× temporal accumulation)
- `bounceCount`: 1–2 (more = slower)
- `maxDistance`: World-space ray cutoff
- `denoise`: Optional bilateral blur for convergence

**Default**: Disabled (DXR-capable PC only)

### 3.4 Planar Reflections

```csharp
public PlanarReflectionProbe planarProbe;
planarProbe.renderScale = 0.5f;  // Render at 50% resolution
planarProbe.refreshRate = 1;  // Every frame
```

**Use**: Flat surfaces (water, mirrors). Renders mirrored camera view to texture.

**Typical cost**: 1–2 ms per probe (one extra camera render).

---

## 4. POST-PROCESSING PIPELINE

### 4.1 Bloom (Physically-Based)

#### Pipeline

```
1. Extract bright pixels (luminance > threshold) → downsampled texture
2. Blur via mip chain (Karis approximation)
3. Add lens dirt (optional)
4. Composite onto final image
```

#### Parameters

```csharp
// VolumeComponent for bloom
public float threshold = 1.0f;  // Pixels below this don't bloom
public float intensity = 0.5f;  // Bloom contribution [0, 8]
public float scatter = 0.7f;  // Bloom softness [0, 1]
public float clamp = 65472.0f;  // Prevent extreme values (HDR)
public Texture lensDirt;  // Optional: blurs bloom pattern
```

#### Karis Downsampling (Anti-Firefly)

```hlsl
// Blur is separable, applied twice (horizontal + vertical)
// Input: 4×4 neighborhood
float3 karis = (a + b + c + d) / ((1/sqr(lum(a))) + (1/sqr(lum(b))) + ...);
// Lower-weight high-luminance samples → prevents bloom fireflies
```

#### Typical Values
- Daylit scene: threshold = 1.0–2.0, intensity = 0.5–1.0
- Dark interior: threshold = 0.5, intensity = 0.3–0.5
- Night/high contrast: threshold = 0.2, intensity = 0.5–0.8

### 4.2 Tonemapping (ACES, Neutral, Custom)

HDRP supports **three tonemapping operators**.

#### ACES Filmic (Industry Standard)

```hlsl
// Academy Color Encoding System (ACES 1.0.3)
// Reference: https://github.com/ampas/aces-dev
float3 ACESFilm(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
// Bias: slightly warm (gamma ≈ 2.2 in shadows, ≈ 2.0 in highlights)
```

**Parameters**:
- `exposure`: Brightness adjustment before tone curve (in EV: ±2–3 range typical)
- `contrast`: Slope of tone curve (1.0 = neutral, > 1.0 = increase blacks)
- `saturation`: Color saturation boost (1.0 = neutral)

#### Neutral Tonemapper

```hlsl
// More linear, less artistic
float3 NeutralTonemap(float3 x, float p = 1.0) {
    return x / (1.0 + x);  // Simple Reinhard
}
```

**Parameters**:
- `exposure`: Brightness
- `white point`: Where maximum intensity clips

#### Custom LUT

```
User-supplied 2D texture (typically 256×256×256 → flattened to 256×16)
Input: [0,1] RGB → lookup in 3D LUT
Output: tonemapped color
```

### 4.3 Color Grading

#### LDR vs HDR

**LDR Grading** (on [0,1] space):
```
1. Tonemap HDR to [0,1]
2. Apply LDR color grading (shadows/mids/highlights)
3. Output final [0,1] color
```

**HDR Grading** (on [0,∞) space):
```
1. Apply color grading directly on HDR values
2. Then tonemap
(More artistic control, but unintuitive)
```

#### Parameters

```csharp
public float saturation = 1.0f;  // [0, 2]: desaturate → saturate
public float contrast = 1.0f;    // [0.5, 2]: reduce → increase blacks
public float temperature = 0.0f; // [-100, 100]: cooler → warmer (blue shift ↔ orange shift)
public float tint = 0.0f;        // [-100, 100]: green shift ↔ magenta shift

// Split controls
public Vector3 shadowsColor = (1, 1, 1);  // Color cast in shadows
public Vector3 midtonesColor = (1, 1, 1);  // Color cast in mids
public Vector3 highlightsColor = (1, 1, 1);  // Color cast in highlights

public float shadowsStart = 0.0f;    // Where shadows end [0, 1]
public float shadowsEnd = 0.3f;      // Fade range
public float highlightsStart = 0.55f;  // Where highlights begin [0, 1]
public float highlightsEnd = 1.0f;     // Fade range
```

#### Lift/Gamma/Gain (Legacy LUT API)

```
Lift:  Add constant to shadows (raises black level)
Gamma: Adjust midtones exponentially
Gain:  Multiply highlights (raises white level)
```

### 4.4 Auto-Exposure (Eye Adaptation)

Dynamically adjusts exposure based on scene brightness.

#### Modes

```csharp
public enum AutoExposureMode {
    Progressive,  // Smoothly adapt (handheld camera feel)
    Fixed,        // Lock to current value
    UseCurrentFrame  // One-shot compute based on frame histogram
}
```

#### Progressive Algorithm

```hlsl
// Each frame:
float targetExposure = ComputeExposureFromLuminance(sceneLuminance);
float currentExposure = tex2D(exposureTexture).r;
float dt = deltaTime;
float convergenceSpeed = 2.0;  // Seconds to adapt fully
float newExposure = mix(currentExposure, targetExposure, dt * convergenceSpeed);
```

**Parameters**:
- `autoExposureMin`: Minimum EV (e.g., -10 for night scenes)
- `autoExposureMax`: Maximum EV (e.g., +8 for bright daylight)
- `compensation`: Offset to target (e.g., -0.5 to expose for shadows)
- `speedUp`: Adaptation speed when scene brightens (typically 1–5 sec)
- `speedDown`: Adaptation speed when scene darkens (typically 2–10 sec)

#### Histogram-Based (Alternative)

```
1. Render scene to log2(luminance) histogram (256 bins)
2. Find weighted percentile (e.g., 70th) → target exposure
3. Clamp to [min, max] EV range
```

### 4.5 Temporal Anti-Aliasing (TAA)

HDRP's TAA is **mandatory** for temporal features (SSGI, SSR, contact shadows).

#### Configuration

```csharp
public float jitterStrength = 1.0f;  // Halton sequence offset magnitude
public float mipBias = 0.0f;         // Texture LOD bias (usually 0)
public float sharpness = 0.5f;       // Output sharpening [0, 1]
public float stationaryBlending = 0.95f;  // History blend weight
public float motionAmplification = 100.0f;  // Temporal scale for motion
```

#### Halton Jitter Pattern

```hlsl
// Per-frame jitter based on frame index
float2 JitterOffset(uint frameId) {
    float2 jitter = Halton(frameId, vec2(2, 3));  // Halton(2,3)
    return (jitter - 0.5) * jitterStrength;
}
// Applied to camera projection matrix
NDCpos += jitter / screenDimensions;
```

#### History Blending

```hlsl
float3 current = CurrentFrameColor;
float3 history = tex2D(historyBuffer, reprojectedUV);
float weight = stationaryBlending;
// Motion detection: reduce weight if motion is detected
float motionFactor = length(velocity);
if (motionFactor > 0.01) {
    weight = mix(weight, 0.5, clamp(motionFactor * motionAmplification, 0, 1));
}
float3 result = mix(current, history, weight);
```

### 4.6 Temporal Upsampling (DLSS/FSR Integration)

HDRP supports **DLSS 2** and **FSR 2** as post-processing passes.

```csharp
public enum TemporalUpsampling {
    None,
    DLSS,  // NVIDIA (RTX only)
    FSR2   // AMD (any GPU with compute)
}
public float dlssQuality = 0.66f;  // 0.5 = perf, 0.66 = balanced, 0.77 = quality, 1.0 = native
```

**Typical gains**: 1.5–2.5× performance for negligible visual loss.

### 4.7 Additional Post-Process Effects

#### Motion Blur

```csharp
public float intensity = 0.5f;  // [0, 1]
public int sampleCount = 8;    // [4, 32]
```

**Implementation**: Velocity buffer (per-pixel velocity) → sample color along velocity vector.

#### Depth of Field (Physical Camera Model)

```csharp
public float focalLength = 50.0f;  // mm
public float aperture = 5.6f;      // f-stop (larger = shallower DOF)
public float focusDistance = 10.0f;  // Distance to in-focus plane (meters)
public float maxBlur = 15.0f;       // Max blur radius (pixels)
```

**Circle of Confusion**:
```
CoC = aperture * focalLength * abs(depth - focusDistance) / depth
```

**Bokeh Blur**: Separable blur with CoC-weighted kernels.

#### Film Grain

```csharp
public float intensity = 0.5f;  // [0, 1]
public float response = 1.0f;   // Grain strength curve
public Texture grain;           // Blue-noise pattern
```

#### Chromatic Aberration

```csharp
public float intensity = 0.5f;  // Color channel offset (pixels)
```

Sample R, G, B at slightly offset UV coordinates.

#### Vignette

```csharp
public float intensity = 0.5f;  // Darkness at edges
public float smoothness = 0.8f;  // Falloff curve
public float roundness = 1.0f;   // Oval vs circle shape
```

---

## 5. MATERIAL MODEL (PBR Lit Shader)

### 5.1 Standard PBR Parameters

HDRP's **Lit shader** (default) implements full physically-based rendering.

#### Base Parameters

```csharp
// Shader input
public float3 baseColor = (0.5, 0.5, 0.5);  // Albedo [0, 1]
public float metallic = 0.0;                 // [0, 1]: non-metal → full metal
public float smoothness = 0.5;               // [0, 1]: rough → mirror
public float3 normal;                        // From normal map
public float ambientOcclusion = 1.0;         // [0, 1]: occluded → unoccluded
```

#### Derived Parameters

```hlsl
// In shader (computed from inputs)
float roughness = 1.0 - smoothness;
float3 albedo = baseColor * (1.0 - metallic);  // Dielectrics: albedo, Metals: 0
float3 F0 = lerp(float3(0.04), baseColor, metallic);  // Fresnel at normal incidence
// 0.04 ≈ refractive index (1.5) → F0 for dielectrics
// Metal uses full color (e.g., gold = (1.0, 0.8, 0.2))
```

### 5.2 GGX Microfacet BRDF

```hlsl
// Cook-Torrance BRDF
float3 cookTorranceBRDF(float3 L, float3 V, float3 N, float3 albedo, float roughness) {
    float3 H = normalize(L + V);
    float cosHalfAngle = dot(N, H);

    // Distribution (GGX)
    float alpha = roughness * roughness;
    float D = GGX_D(alpha, cosHalfAngle);

    // Fresnel (Schlick approximation)
    float3 F = F0 + (1.0 - F0) * pow(1.0 - dot(H, V), 5.0);

    // Geometry (Smith with Schlick-GGX visibility)
    float G = G_Smith_GGX(alpha, dot(N, L), dot(N, V));

    // Specular BRDF
    float3 specular = D * F * G / max(0.001, 4.0 * dot(N, L) * dot(N, V));

    // Diffuse BRDF (Lambertian)
    float3 diffuse = albedo / PI;

    return diffuse + specular;
}
```

**HDRP-Specific Implementation**:
```
D function: GGX (Trowbridge-Reitz)
G function: Smith with Schlick-GGX (simplified Disney)
F function: Schlick (0.04 base for dielectrics)
```

### 5.3 Subsurface Scattering (SSS)

Optional **diffusion profiles** for skin, cloth, marble.

```csharp
public enum DiffusionProfile {
    Skin,    // Fast skin SSS (blurred backlit transmission)
    Cloth,   // Soft diffusion
    Marble,  // Dense translucent material
    Custom   // User-defined absorption/scattering
}
```

#### Per-Pixel SSS Setup

```hlsl
[SSS_Enable]
float thickness;  // [0, 1]: material thickness (for backlit transmission)
float3 transmissionColor = baseColor;  // Color of transmitted light
float sssDistortion = 0.0;  // Normal distortion for IOR simulation
```

**Pipeline**:
```
1. Separate G-buffer pass: write thickness → separate texture
2. Gaussian blur of albedo × transmissive light (post-process)
3. Composite: diffuse + blurred SSS contribution
```

### 5.4 Clear Coat (Lacquer, Varnish)

Two-layer BRDF: base material + glossy coat.

```csharp
[Feature("_ENABLE_COAT")]
public float coatStrength = 0.5f;  // [0, 1]: coat opacity
public float coatRoughness = 0.1f;  // [0, 1]: coat smoothness
public float coatIOR = 1.5f;        // Refractive index (typically 1.4–1.8)
```

**Evaluation**:
```hlsl
// Two-layer render
float3 baseResult = StandardPBR(baseColor, metallic, roughness, N, L, V);
float3 coatResult = StandardPBR(float3(1), 0.0, coatRoughness, N, L, V);
// Fresnel determines blend
float F = FresnelSchlick(1.0, coatIOR, dot(N, V));  // IOR-dependent F0
float3 final = mix(baseResult, coatResult, coatStrength * F);
```

### 5.5 Anisotropy

Brushed metal, satin fabric effect.

```csharp
[Feature("_ENABLE_ANISO")]
public float anisotropyStrength = 0.5f;  // [0, 1]
public float anisotropyAngle = 0.0f;     // [0, 360] degrees (rotation)
```

**Anisotropic GGX**:
```hlsl
float3 tangent = ComputeTangentFromAngle(anisotropyAngle, normal);
float3 bitangent = cross(normal, tangent);

float alphaX = roughness;
float alphaY = roughness * (1.0 - anisotropyStrength);  // Anisotropy reduces one axis

float D = GGX_Aniso(alphaX, alphaY, H, tangent, bitangent);
// Rest of BRDF same (but use anisotropic D)
```

### 5.6 Iridescence

Thin-film interference (soap bubbles, oil slicks).

```csharp
[Feature("_ENABLE_IRIDESCENCE")]
public float iridescenceStrength = 0.5f;  // [0, 1]
public float iridescenceLayer = 0.5;      // Film thickness (wavelengths)
```

**Thin-Film Fresnel**:
```hlsl
// OPD = optical path difference (film thickness)
float3 iridescenceColor = EvaluateThinFilm(OPD, incidentAngle);
// Color shifts rainbow-like across viewing angles
float3 metallic_with_iridescence = mix(metallicBRDF, iridescenceColor, iridescenceStrength);
```

### 5.7 Parallax Occlusion Mapping (Displacement)

High-detail surface deformation via ray-marching height field.

```csharp
public float displacementStrength = 0.1f;  // [0, 1]: height amplitude
public int displacementSteps = 8;          // [4, 32]: ray march samples
public Texture2D displacementMap;          // Grayscale height [0, 1]
```

**Ray March** (in view-space):
```hlsl
float3 V_TS = TransformViewToTangentSpace(V);
float2 parallaxOffset = 0;

for (int i = 0; i < displacementSteps; ++i) {
    float height = tex2D(displacementMap, UV + parallaxOffset).r;
    float depth = (i / float(displacementSteps)) - height * displacementStrength;
    parallaxOffset += (height - depth) * V_TS.xy / V_TS.z;
}

float3 finalNormal = normalize(cross(...));  // Recompute normals from displaced surface
```

---

## 6. ATMOSPHERE & VOLUMETRIC EFFECTS

### 6.1 Physically-Based Sky (Bruneton)

HDRP implements **Bruneton's analytic sky model** (2008) with baked LUTs.

#### LUT Structure

**Transmittance LUT** (256×64):
```
X-axis: zenith angle (0 = sun up, 1 = sun at horizon)
Y-axis: altitude (0 = ground, 1 = top of atmosphere)
Value: RGB transmittance from camera to top-of-atmosphere
```

**Irradiance LUT** (64×16):
```
X-axis: sun zenith angle
Y-axis: altitude
Value: RGB radiance in sky hemisphere (sky irradiance at point)
```

**Scattering LUT** (32×32×32, accessed via 2D):
```
4D data: (altitude, view-zenith, sun-zenith, sun-view-angle)
Compressed into 3×(32×128) 2D textures via mip packing
Value: RGB inscattered radiance
```

#### Parameters

```csharp
public enum AtmosphereModel {
    Bruneton,  // Physically correct (HDRP default)
    Simple,    // Gradient + sun disk (faster)
}

public float sunDiskSize = 0.05f;     // Angular size [0, 0.3]
public float sunDiskExposure = 1.0f;  // EV adjustment
public Vector3 groundColor = (0.4, 0.4, 0.4);  // Albedo at horizon
public float skyRotation = 0.0f;      // Rotate sky (degrees)
```

#### Shader Usage

```hlsl
// Per-pixel sky ray casting
float3 viewDir = normalize(worldPos - cameraPos);
float zenithAngle = acos(dot(viewDir, upVector));
float altitudeFactor = (cameraAltitude - groundLevel) / atmosphereHeight;

// Sample LUTs
float transmittance = SampleTransmittance(zenithAngle, altitudeFactor);
float inscatter = SampleScattering(zenithAngle, sunZenithAngle, viewSunAngle);
float3 skyColor = (inscatter + sunDiskColor) * transmittance;
```

### 6.2 Volumetric Fog

**3D froxel-based fog** (deferred shading compatible).

#### Froxel Grid

```
3D grid: (screenX, screenY, depth)
Typical: 160×90×64 (down-scaled for performance)
Each cell: RGB radiance + density
```

#### Injection Pipeline

```
1. For each object with volumetric material:
   - Compute extinction coefficient (density × absorption)
   - Accumulate in froxel grid via atomic adds
2. Raytrace through grid (slice-by-slice)
3. Composite onto final image
```

#### Parameters

```csharp
public float baseHeight = 0.0f;       // Height at which fog density = 100%
public float baseExtinction = 0.1f;   // Extinction coefficient [0, 1]
public float anisotropy = 0.0f;       // [-1, 1]: backlit → forward scattering
public float ambientLighting = 0.5f;  // How much fog receives ambient light
public Vector3 albedo = (1, 1, 1);    // Fog color
```

#### Density Function

```hlsl
float fogDensity(float height) {
    float h = max(0, height - baseHeight);
    return baseExtinction * exp(-h / heightFalloff);
}

// In each froxel:
float density = fogDensity(cellCenterHeight);
float transmittance = exp(-density * cellDepth);
float3 inscatter = fogAlbedo * ambientLight * (1 - transmittance);
```

### 6.3 Volumetric Clouds

**Screen-space or froxel-based clouds** for performance.

#### Common Approaches in HDRP

**1. Screen-Space Marching** (Fast, limited quality):
```
1. Raymarch per-pixel in screen space
2. Sample 3D noise (cloud shape)
3. Lighting via temporal history (no per-step tracing)
4. Temporal accumulation → converges in 8+ frames
```

**2. Froxel-Based** (High quality, expensive):
```
1. Inject cloud density into 3D grid
2. Trace through grid with multi-scatter lighting
3. Temporal filtering across grid cells
```

#### Cloud Parameters

```csharp
public float3 cloudColor = (1, 1, 1);
public float coverage = 0.5f;        // [0, 1]
public float density = 0.5f;         // [0, 1]
public float windSpeed = 0.1f;       // Clouds drift with wind
public float anisotropy = 0.5f;      // Forward scattering direction
```

#### Noise Function

```hlsl
// Multi-octave Perlin noise
float cloudDensity(float3 pos, float3 windOffset) {
    pos += windOffset * time;
    float n1 = Perlin(pos * 0.001);
    float n2 = Perlin(pos * 0.004);
    float n3 = Perlin(pos * 0.01);
    return n1 * 0.5 + n2 * 0.3 + n3 * 0.2;
}
```

### 6.4 Exponential Height Fog

Simple **atmospheric fog** (fast, suitable for backgrounds).

```csharp
public float fogDensity = 0.01f;        // [0, 1]
public float fogHeightFalloff = 0.2f;   // Density decay with altitude
public Vector3 fogColor = (0.5, 0.5, 0.5);
public float fogDistanceScale = 1.0f;   // World-space scale adjustment
```

#### Density & Transmittance

```hlsl
float fogDensity(float height, float worldDistance) {
    float heightDensity = exp(-height * fogHeightFalloff);
    float distanceDensity = fogDensity * worldDistance;
    return heightDensity * distanceDensity;
}

float transmittance = exp(-fogDensity(height, distance));
float3 finalColor = mix(pixelColor, fogColor, 1.0 - transmittance);
```

---

## 7. QUALITY TIERS & SCALABILITY

HDRP supports **quality presets** per platform.

### 7.1 Quality Levels

| Level | Target GPU | Resolution | Shadows | GI | Reflections | Post-Process |
|-------|-----------|------------|---------|-----|-------------|--------------|
| **Ultra** | RTX 4080+ | 4K@60 | CSM 4× 2048 | RT GI | RT Reflections | All enabled |
| **High** | RTX 3080 | 1440@60 | CSM 4× 1024 | SSGI | SSR + Probes | All enabled |
| **Medium** | RTX 3060 | 1080@60 | CSM 2× 512 | Light Probes | Probes only | TAA, Bloom, AE |
| **Low** | GTX 1060 | 720@30 | CSM 1× 256 | Ambient + Sky | Sky only | TAA |

### 7.2 Scalability Knobs

```csharp
public enum ShadowResolution { 512, 1024, 2048, 4096 }
public enum GIApproach { SkyOnly, LightProbes, SSGI, RayTraced }
public enum ReflectionApproach { SkyOnly, Probes, SSR, RayTraced }
public enum TemporalUpsampling { None, FSR2, DLSS }
```

---

## 8. HDRP vs UE5 COMPARISON

### Direct Rendering Parity

| Feature | HDRP | UE5 | Notes |
|---------|------|-----|-------|
| **Lighting Units** | Lux/Lumens | Lux/Lumens | Identical |
| **CSM** | 4 cascades (configurable) | 4 cascades | Same algorithm |
| **Contact Shadows** | Screen-space raytrace | Screen-space raytrace | Identical |
| **Area Lights** | LTC evaluation | LTC evaluation | Identical technique |
| **SSGI** | Hi-Z raytracing | Hi-Z raytracing | Identical |
| **SSR** | Hi-Z raytracing | Hi-Z raytracing | Identical |
| **GGX BRDF** | Cook-Torrance | Cook-Torrance | Identical |
| **Sky Model** | Bruneton analytic | Bruneton analytic | Identical |
| **Fog** | Exponential + Froxels | Exponential + Froxels | Identical |

### Architecture Differences

| Aspect | HDRP | UE5 | Impact |
|--------|------|-----|--------|
| **Deferred vs Forward** | Hybrid (deferred primary) | Forward + Deferred (mobile) | HDRP scales to more lights |
| **Probe System** | APV (modern) | Static probes (older) | APV is more flexible |
| **Temporal Features** | TAA mandatory | TAA optional | HDRP requires frame history |
| **Shader Model** | Lit + CustomShader | Materials + Custom | HDRP is more opinionated |
| **RHI Abstraction** | DX12/Vulkan/Metal | DX12/Vulkan/Metal/OpenGL | Same targets (HDRP no GL) |

---

## 9. ADAPTATION ROADMAP FOR UZENGINE

### Immediate Wins (Phase 1)

- [x] **Adopt physical light units** — migrate from [0,1] arbitrary ranges to lux/lumens
- [x] **LTC area lights** — implement LTC evaluation in shader (small texture, big quality jump)
- [x] **ACES tonemap** — replace current tonemap with industry standard
- [ ] **Auto-exposure** — progressive eye adaptation (motion/night sensitivity)

### Medium-term (Phase 2–3)

- [ ] **APV light probes** — replace old light probe grid with bricks
- [ ] **Volumetric clouds** — screen-space marching (temporal accumulation)
- [ ] **Subsurface scattering** — diffusion profiles for skin/cloth
- [ ] **Contact shadow improvements** — thicker-aware raymarching
- [ ] **Planar reflections** — water, mirrors

### Long-term (Phase 4+)

- [ ] **Ray-traced GI/Reflections** — DXR backend for RTX GPUs
- [ ] **Temporal upsampling** — FSR 2 / DLSS 2 integration
- [ ] **Clear coat + Anisotropy** — advanced material features
- [ ] **Adaptive probe volumes** — production baking + streaming

---

## 10. SHADER IMPLEMENTATION TEMPLATES

### 10.1 Physical Light Evaluation

```hlsl
/// @ref HDRP 2023 "Physical Light Units" — convert lux/lumens to radiance
float3 EvaluateDirectionalLight(
    float3 lightDirection,
    float luxIntensity,
    float3 lightColor,
    float3 surfaceNormal,
    float3 viewDirection,
    float3 albedo,
    float roughness,
    float metallic)
{
    // Clamp to prevent backlight
    float cosTheta = max(0.0, dot(surfaceNormal, lightDirection));
    if (cosTheta < 0.001) return float3(0, 0, 0);

    // Radiance from lux (1 lux = 1 lumen/m² at 1m distance)
    float3 radiance = lightColor * luxIntensity;

    // Cook-Torrance BRDF
    float3 half_vec = normalize(lightDirection + viewDirection);
    float NdotH = max(0.001, dot(surfaceNormal, half_vec));
    float VdotH = max(0.001, dot(viewDirection, half_vec));
    float NdotL = cosTheta;
    float NdotV = max(0.001, dot(surfaceNormal, viewDirection));

    float alpha = roughness * roughness;
    float D = GGX_D(alpha, NdotH);
    float3 F = FresnelSchlick(lerp(float3(0.04), albedo, metallic), VdotH);
    float G = G_Smith(alpha, NdotL, NdotV);

    float3 specular = D * F * G / max(0.001, 4.0 * NdotL * NdotV);
    float3 diffuse = albedo * (1.0 - metallic) / PI;

    return (diffuse + specular) * radiance * NdotL;
}
```

### 10.2 ACES Tonemapping

```hlsl
/// @ref AMPAS 2014 "ACES Color Encoding System" — industry standard tonemapping
float3 ACESTonemapping(float3 hdrColor, float exposure) {
    hdrColor *= pow(2.0, exposure);

    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return saturate((hdrColor * (a * hdrColor + b)) / (hdrColor * (c * hdrColor + d) + e));
}
```

### 10.3 GGX Microfacet Distribution

```hlsl
/// @ref Walter 2007 "Microfacet Models for Refraction through Rough Surfaces" — GGX distribution
float GGX_D(float alpha, float NdotH) {
    float a2 = alpha * alpha;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

/// @ref Smith 1967 "Geometrical shadowing of a random rough surface" — visibility function
float G_Smith(float alpha, float NdotL, float NdotV) {
    float r = (alpha + 1.0) / 2.0;  // Remapping for Schlick-GGX
    float g1_v = NdotV / (NdotV * (1.0 - r) + r);
    float g1_l = NdotL / (NdotL * (1.0 - r) + r);
    return g1_v * g1_l;
}
```

### 10.4 Bruneton Sky LUT Sampling

```hlsl
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — efficient sky rendering
float3 SampleSkyRadiance(
    float3 rayDirection,
    float3 sunDirection,
    float cameraAltitude)
{
    // Map ray direction to LUT coordinates
    float zenithAngle = acos(dot(rayDirection, float3(0, 1, 0)));
    float azimuth = atan2(rayDirection.z, rayDirection.x);

    float sunZenithAngle = acos(dot(sunDirection, float3(0, 1, 0)));
    float sunAzimuth = atan2(sunDirection.z, sunDirection.x);
    float sunViewAngle = acos(dot(rayDirection, sunDirection));

    float2 scatterUV0 = float2(
        zenithAngle / PI,
        cameraAltitude / ATMOSPHERE_HEIGHT);

    // Scattering LUT is 4D (compressed into 2D)
    float3 scatteringColor = SampleScatteringLUT(
        scatterUV0,
        sunZenithAngle / PI,
        sunViewAngle / PI);

    return scatteringColor;
}
```

---

## 11. RECOMMENDED HDRP SETTINGS FOR PARITY WITH UE5

```
Quality Preset: High
Resolution: 1440p
Shadows:
  - Directional: CSM 4×1024, contact shadows enabled
  - Point/Spot: 512×512 PCF_5x5
Global Illumination: SSGI + Light Probes
Reflections: SSR + Probes
Post-Processing:
  - TAA: enabled, history blend 0.95
  - Bloom: threshold 1.0, intensity 0.5
  - Tonemapping: ACES
  - Auto-Exposure: progressive, EV [-6, 6]
  - Motion Blur: intensity 0.5
Material System: Lit shader, PBR defaults
Sky: Bruneton model, ground color (0.4, 0.4, 0.4)
Fog: Exponential + volumetric froxels
```

---

## References

- HDRP Manual: `https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition/`
- AMPAS ACES: `https://github.com/ampas/aces-dev`
- Bruneton Sky: https://ebruneton.github.io/precomputed_atmospheric_scattering/
- LTC Area Lights: https://www.gdcvault.com/play/1023777/The-Technology-Behind
- GGX: Walter et al., "Microfacet Models for Refraction through Rough Surfaces" (EGSR 2007)

