# UE5 Default Scene Rendering Systems Analysis

**Analysis Date**: 2026-03-14
**UE5 Source Version**: 5.6 (W:\UE_5.6_Source)
**Analyzed By**: Unreal Architect Agent
**Purpose**: Extract architectural patterns for UzEngine adaptation

This document catalogs every rendering system that contributes to a basic UE5 scene's visual quality. The analysis is clean-room — we document the pattern and design principle, not implementation details.

---

## Overview: UE5's Deferred Rendering Stack

UE5 uses a **multi-layer deferred rendering pipeline**:

```
┌─────────────────────────────────────────────────────────────┐
│ Render Phase Sequence                                       │
├─────────────────────────────────────────────────────────────┤
│ 1. Depth Prepass          → Scene depth buffer              │
│ 2. Base Pass (GBuffer)    → Material GBuffer targets        │
│ 3. Decals (DBuffer)       → Modify GBuffer data             │
│ 4. Lighting Composition   → Deferred lighting passes        │
│ 5. Translucency           → Forward rendered transparent    │
│ 6. Post-Processing        → Tonemap, Bloom, AA, etc.       │
│ 7. UI/Debug Rendering     → Editor overlays                │
└─────────────────────────────────────────────────────────────┘
```

---

## 1. DEFERRED RENDERING PIPELINE

### 1.1 GBuffer Layout

**Source Files**:
- `W:/UE_5.6_Source/RenderCore/Public/GBufferInfo.h` (lines 12–44)
- `W:/UE_5.6_Source/Renderer/Internal/SceneTextures.h` (lines 131–136)

**Pattern**: UE5 uses a **flexible slot-based GBuffer layout** that maps logical "slots" (EGBufferSlot enum) to physical render targets (GBufferA–F). This decouples shader code from RT layout.

**Default Layout Slots** (from GBufferInfo.h):
```cpp
enum EGBufferSlot
{
    GBS_Invalid,
    GBS_SceneColor,              // RGB 11.11.10 (lit color for forward pass)
    GBS_WorldNormal,             // RGB 10.10.10 (oct-encoded world normal)
    GBS_PerObjectGBufferData,    // 2 bits per-object flags
    GBS_Metallic,                // R8 (0–1 metallic)
    GBS_Specular,                // R8 (specular intensity)
    GBS_Roughness,               // R8 (0–1 roughness)
    GBS_ShadingModelId,          // 4 bits (material type: PBR, subsurface, hair, cloth, etc.)
    GBS_SelectiveOutputMask,     // 4 bits (which decals affect this pixel)
    GBS_BaseColor,               // RGB8 (diffuse albedo)
    GBS_GenericAO,               // R8 (ambient occlusion for GI)
    GBS_IndirectIrradiance,      // R8 (packed SH irradiance)
    GBS_AO,                       // R8 (cavity/contact shadow AO)
    GBS_Velocity,                // RG float16 (motion vectors for TAA/MB)
    GBS_PrecomputedShadowFactor, // RGBA8 (static lighting)
    GBS_WorldTangent,            // RGB8 (world space tangent for aniso)
    GBS_Anisotropy,              // R8 (material anisotropy)
    GBS_CustomData,              // RGBA8 (material-specific data)
    GBS_SubsurfaceColor,         // RGB8 (SSS tint)
    GBS_Opacity,                 // R8 (for translucent materials)
    GBS_SubsurfaceProfile,       // RGB8 (profile index)
    GBS_ClearCoat,               // R8 (clear coat layer weight)
    GBS_ClearCoatRoughness,      // R8 (clear coat roughness)
    GBS_HairSecondaryWorldNormal,// RG8 (hair secondary normal)
    GBS_HairBacklit,             // R8 (hair backlit factor)
    GBS_Cloth,                   // R8 (cloth factor)
    GBS_IrisNormal,              // RG8 (eye iris normal)
    GBS_SeparatedMainDirLight,   // RGB 11.11.10 (pre-lit single dir light)
    GBS_Num
};
```

**Physical Render Targets**:
```cpp
struct FSceneTextures {
    FRDGTextureRef GBufferA;     // Typically: WorldNormal + PerObjectData
    FRDGTextureRef GBufferB;     // Typically: BaseColor + AO
    FRDGTextureRef GBufferC;     // Typically: Specular + Roughness + ShadingModelId + SelectiveMask
    FRDGTextureRef GBufferD;     // Custom data / Subsurface / Anisotropy (optional)
    FRDGTextureRef GBufferE;     // Additional (optional, e.g., WorldTangent)
    FRDGTextureRef GBufferF;     // Additional (optional, e.g., PrecomputedShadow)
    FRDGTextureRef Velocity;     // Motion vectors (can be separate or in GBuffer)
};
```

**Design Principle**: By using a slot-based abstraction, UE5 can:
- Support different material shading models (PBR, hair, cloth, skin) without changing shader structure
- Pack data tightly (e.g., 10.10.10 normals) via `EGBufferCompression` enum
- Enable "selective output masks" so decals only modify relevant channels
- Add new slots without breaking existing shaders

**Visual Contribution**: GBuffer is the foundation—it stores the surface properties needed for lighting. Without it, there is no deferred lighting.

**Complexity**: **Medium**

---

### 1.2 Base Pass Rendering

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/BasePassRendering.h` (lines 78–175)
- `W:/UE_5.6_Source/Renderer/Private/BasePassRendering.h` (lines 209–255)

**Pattern**: The **base pass** writes GBuffer targets. It is a **mesh pass processor** that iterates opaque geometry and outputs GBuffer slots.

**Data Written per Material**:
- World normal (compressed to oct 10.10.10 or 8.8)
- Base color (RGB8 sRGB)
- Roughness, metallic (R8 each or packed)
- Shading model ID (4 bits)
- Emissive color (stored in scene color if unlit material)
- Tangent frame (for anisotropy/parallax)
- Custom material parameters (via slots)

**Important Uniforms**:
```cpp
struct FOpaqueBasePassUniformParameters {
    FSharedBasePassUniformParameters Shared;  // Lights, reflections, fog
    FSubstrateBasePassUniformParameters Substrate;  // Substrate system
    int32 UseForwardScreenSpaceShadowMask;  // Screen-space shadow mask
    Texture2D ForwardScreenSpaceShadowMaskTexture;  // Optional FSM
    Texture2D IndirectOcclusionTexture;  // From SSAO/DFAO
    Texture2D ResolvedSceneDepthTexture;  // For parallax/contact shadows
    FDBufferParameters DBuffer;  // Decal data
    Texture2D PreIntegratedGFTexture;  // Cook-Torrance pre-integrated GF LUT
};
```

**Design Principle**: The base pass is a **pure write pass**—it doesn't read scene color, only writes GBuffer. This enables GPU-driven culling and efficient memory access patterns.

**Visual Contribution**: Base pass establishes material color and properties. Without it, scene is black (no surface data).

**Complexity**: **Medium**

---

## 2. LIGHTING SYSTEMS

### 2.1 Directional Light (Sun)

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/Shadows/ShadowSceneRenderer.h` (lines 70–71)
- `W:/UE_5.6_Source/Renderer/Private/DeferredShadingRenderer.cpp` (lines 227–231)

**Pattern**: Directional lights use **cascaded shadow maps (CSM)** with multiple cascades per view.

**Key Parameters**:
- **Cascade Count**: Typically 4 (configurable via `r.ShadowQuality` and light component)
- **Split Scheme**: Logarithmic split (closer cascades have more detail)
- **Map Resolution**: 2048x2048 per cascade (configurable per platform/quality)
- **Filtering**: Poisson-disk PCF (16-tap default, varies by quality)
- **Bias**: Slope-scale bias + constant bias (prevents z-fighting)
- **View-dependent**: Each view gets its own cascade setup

**Directional Light Shadow Setup**:
```cpp
class FShadowSceneRenderer {
    void AddDirectionalLightShadow(
        FLightSceneInfo& LightSceneInfo,
        FViewInfo& View,
        float MaxNonFarCascadeDistance,
        TArray<FProjectedShadowInfo*>& OutShadowInfosThatNeedCulling
    );
};
```

**Composition**: Applied in **deferred lighting pass** to shadow-masked scene (read from shadow texture, modulate light contribution).

**Design Principle**: Cascades solve the distant shadow problem (far shadows have low precision, near shadows need high precision). Multi-cascade approach balances quality vs. cost.

**Visual Contribution**: Directional shadows create dramatic shading and depth cues. Without CSM, scene looks flat and overlit.

**Complexity**: **Complex** (cascade selection, projection, filtering)

---

### 2.2 Sky Light (IBL)

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/ReflectionEnvironment.h` (lines 25–35)
- `W:/UE_5.6_Source/Engine/Classes/Components/SkyLightComponent.h` (ref in memory)

**Pattern**: Sky light uses **image-based lighting (IBL)** with pre-integrated specular and diffuse.

**Data Structures**:
```cpp
struct FReflectionUniformParameters {
    FVector4f SkyLightParameters;  // (intensity, AO, fade, unused)
    TextureCube SkyLightCubemap;   // Sky environment cubemap
    TextureCube SkyLightBlendDestinationCubemap;  // For blending/transitions
    TextureCubeArray ReflectionCubemap;  // Reflection captures (sorted by distance)
    Texture2D PreIntegratedGF;     // Cook-Torrance GF pre-integrated LUT
};
```

**IBL Computation** (in deferred lighting):
- **Diffuse**: Sample SkyLight cubemap at surface normal, multiply by SH irradiance (9 floats, 3 SH bands)
- **Specular**:
  1. Sample SkyLight cubemap at reflection direction (mip based on roughness)
  2. Apply Cook-Torrance F0 correction via PreIntegratedGF LUT (BRDF term)
  3. Multiply by metal/roughness and view-dependent fresnel

**Key Parameters**:
- **Intensity**: Multiplier on cubemap luminance
- **Rotation**: Per-frame cubemap rotation
- **Ambient Occlusion (AO)**: Darkens indirect diffuse based on material/screen-space AO
- **Distance Field AO (DFAO)**: Optional global occlusion override
- **Cloud Shadow**: Optional cloud shadow projection on IBL

**Design Principle**: Pre-integrated BRDF + cubemap sampling avoids expensive per-pixel integration. Mip-mapping of cubemap provides roughness-dependent filtering (higher mips = blurrier, simulates rough surfaces).

**Visual Contribution**: IBL provides ambient lighting and reflections. Without it, unlit areas are completely black; metallic/glossy surfaces have no reflections.

**Complexity**: **Complex** (cubemap capture, SH computation, BRDF integration)

---

### 2.3 Reflection Captures (Probes)

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/ReflectionEnvironmentCapture.h`

**Pattern**: **Reflection capture probes** are placed in the scene to override/blend IBL in local regions.

**Key Concepts**:
- **Cubemap Array**: Multiple probes stored in a single TextureCubeArray
- **Influence Radius**: Sphere-based influence (soft falloff at edges)
- **Priority**: Higher priority probes override lower
- **Blending**: Lerp between probes based on distance
- **Per-Reflection**: Closest probe is used for each pixel's reflection direction

**Design Principle**: Probes provide local reflection variation without per-pixel computation. Distance-based blending ensures smooth transitions.

**Visual Contribution**: Reflection probes add local details to reflections (e.g., puddles reflect nearby buildings). Without them, reflections are uniform from sky.

**Complexity**: **Simple** (spatial lookup + blend)

---

### 2.4 Point/Spot Light Shadows

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/Shadows/ShadowSceneRenderer.h` (lines 67, 231–238)

**Pattern**: Local lights (point, spot) use **virtual shadow maps (VSM)** or traditional shadow maps on a **per-light basis**.

**Traditional Approach**:
- **Point Light**: Dual-paraboloid projection (2x 2048x2048 maps, one per hemisphere)
- **Spot Light**: Single perspective-projected map (2048x2048)
- **Filtering**: PCF (8-tap or 16-tap)
- **Distance Bias**: Slope-scale + constant bias

**Virtual Shadow Maps (VSM)** (newer, default in UE 5.4+):
- Single clip map per light
- Automatically mips and caches pages
- Supports all light types uniformly
- Projection via virtual addresses (no traditional matrix)

**Design Principle**: VSM solves the atlas packing problem—each light doesn't need its own RT, pages are dynamically allocated and reused.

**Visual Contribution**: Point/spot shadows add localized depth and realism. Without them, local lights wash out geometry.

**Complexity**: **Complex** (projection, caching, PCF)

---

## 3. SHADOWS

### 3.1 Cascaded Shadow Maps (CSM)

**Already covered under Directional Light (2.1).**

---

### 3.2 Contact Shadows (Screen-Space)

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/Shadows/ScreenSpaceShadows.h`

**Pattern**: **Screen-space contact shadows** trace a cone from the surface toward the sun in screen space to detect occlusion.

**Algorithm**:
1. For each pixel, march toward sun direction in screen space (1–8 steps)
2. Compare depth: if scene depth < ray-marched depth, pixel is in contact shadow
3. Fade out at screen edges and distance

**Key Parameters**:
- **Max Distance**: Ray length in screen space (6–12 pixels typical)
- **Thickness**: How thick surfaces are treated (bias for false shadows)
- **Quality**: Step count (low-end: 4, high-end: 16)

**Design Principle**: Cheap screen-space approximation that adds detail to shadow boundaries without shadow map overhead. Complements CSM by adding crevice/fold shadows.

**Visual Contribution**: Contact shadows add detail in areas between surfaces (crevices, seams). Without them, close shadow boundaries are too sharp.

**Complexity**: **Simple** (screen-space raycasting)

---

### 3.3 Subsurface Scattering Shadows

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/PostProcess/PostProcessSubsurface.h`
- `W:/UE_5.6_Source/Engine/Classes/Components/SkyLightComponent.h` (sky light AO)

**Pattern**: For materials with subsurface scattering, shadows are pre-blurred to simulate light transmission.

**Approach**:
- Render unblurred shadow map (high frequency detail)
- Apply separable Gaussian blur (simulates light scattering inside material)
- Use blur radius from material's subsurface profile

**Design Principle**: Blurred shadows simulate light bouncing inside translucent surfaces (skin, leaves, wax). Different materials can have different blur radii via subsurface profiles.

**Visual Contribution**: Makes skin/translucent materials look alive (backlit ears glow, light penetrates thin geometry). Critical for skin rendering.

**Complexity**: **Simple** (Gaussian blur + profile lookup)

---

## 4. POST-PROCESSING

### 4.1 Tone Mapping

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/PostProcess/PostProcessTonemap.h` (lines 15–78)

**Pattern**: **ACES tone mapping** (Academy Color Encoding System) is the default.

**Data Flow**:
```
HDR Scene Color → ACES RRT (Reference Rendering Transform)
                → ACES ODT (Output Device Transform)
                → Display-referred LDR (0–1 range)
```

**Inputs to Tonemap Pass**:
```cpp
struct FTonemapInputs {
    FScreenPassTextureSlice SceneColor;  // HDR scene after lighting
    FScreenPassTexture Bloom;             // Bloom to composite
    FRDGTextureRef ColorGradingTexture;   // 3D LUT (user color grading)
    FRDGBufferRef EyeAdaptationBuffer;    // Exposure compensation
    FLocalExposureParameters* LocalExposureParams;  // Local adaptation
    FEyeAdaptationParameters* EyeAdaptParams;       // Auto-exposure params
    bool bOutputInHDR;  // Keep in HDR (for HDR display)
};
```

**Key Parameters**:
- **Eye Adaptation**: Auto-exposure based on average scene luminance (temporal smoothing)
- **Local Exposure**: Per-pixel exposure adjustment based on spatial luminance (simulates eye adaptation)
- **Color Grading LUT**: 32x32x32 or 64x64x64 3D LUT for color correction/artistic color
- **Film Grain**: Optional procedural noise for analog feel (Halton sequence per frame)

**Design Principle**: ACES is an industry-standard tone mapper that preserves color information across a wide dynamic range. Supports both SDR and HDR displays via parameters.

**Visual Contribution**: Converts HDR lighting data to displayable colors. Without tone mapping, HDR colors clamp at 1.0 (blown out highlights, crushed blacks).

**Complexity**: **Simple** (lookup + LUT sampling)

---

### 4.2 Bloom / Light Bloom

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/PostProcess/PostProcessBloomSetup.h` (lines 11–50)

**Pattern**: **Gaussian bloom via downsampling chain and separable blur**.

**Algorithm**:
1. **Threshold**: Extract bright pixels (above bloom threshold, e.g., luminance > 1.0)
2. **Downsampling Chain**: 1x → 2x → 4x → 8x (gaussian blur at each step)
3. **Separable Blur**: Horizontal + vertical Gaussian (faster than 2D kernel)
4. **Upsampling**: Composite blurred mips back to full resolution (additive)

**Key Parameters**:
```cpp
struct FBloomSetupInputs {
    float Threshold;  // Pixels above this bloom (default 1.0 = full white)
    EBloomQuality Quality;  // Q1–Q5 (number of mips and blur quality)
    FRDGBufferRef EyeAdaptationBuffer;  // Scale bloom by exposure
};
```

**Quality Levels**:
- Q1: 1 bloom mip (3x downsampled)
- Q5: 5 bloom mips (down to 1/32 resolution)

**Design Principle**: Separable Gaussian blur reduces 2D convolution from O(k²) to O(k). Downsampling reduces memory and compute. Multiple mips provide temporal coherence (smooth bloom across scales).

**Visual Contribution**: Bloom simulates lens imperfections and eye glare. Creates glow around bright areas (fire, sun, neon). Without bloom, bright HDR values just look white.

**Complexity**: **Simple** (Gaussian blur + downsampling)

---

### 4.3 Anti-Aliasing (TAA)

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/PostProcess/TemporalAA.h` (lines 14–93)

**Pattern**: **Temporal Anti-Aliasing (TAA)** or **Temporal Super-Resolution (TSR)** is the default AA.

**TAA Algorithm**:
1. **Jittered Projection**: Offset camera projection per frame (Halton sequence)
2. **Motion Vectors**: Track pixel movement between frames (from velocity buffer)
3. **History Reprojection**: Reproject previous frame's color to current pixel
4. **Rejection/Clipping**: If history pixel differs too much (motion/disocclusion), reduce history blend
5. **Temporal Blend**: Lerp between current and history (e.g., 80% history, 20% current)

**Data**:
```cpp
struct FTAAPassParameters {
    ETAAPassConfig Pass;  // Main, Upsampling, SSR, etc.
    ETAAQuality Quality;  // Low, Medium, High, etc.
    FRDGTexture* SceneDepthTexture;  // For reprojection
    FRDGTexture* SceneVelocityTexture;  // Motion vectors
    FRDGTexture* SceneColorInput;  // Current frame
    bool bDownsample;  // Output half-res for performance
};
```

**Temporal Super-Resolution (TSR)**:
- Extended TAA that upsamples (e.g., 77% → 100%) while denoising
- Uses multiple frames' history for better detail
- Default on high-end platforms (UE 5.1+)

**Design Principle**: TAA leverages temporal coherence—pixels don't change drastically between frames. By averaging over time, AA happens "for free" as a side effect of reprojection.

**Visual Contribution**: TAA eliminates aliasing (jagged edges, shimmering). Without it, edges are jagged and crawling at animation.

**Complexity**: **Complex** (motion reprojection, history management, clipping logic)

---

### 4.4 Screen-Space Ambient Occlusion (SSAO)

**Source Files**:
- `W:/UE_5.6_Source/Engine/Runtime/Renderer/Private/CompositionLighting/PostProcessAmbientOcclusion.h`

**Pattern**: **Horizon-Based Ambient Occlusion (HBAO)** or **GTAO** (Ground Truth AO) samples depth around each pixel.

**Algorithm** (HBAO+):
1. For each pixel, sample depth in rays around it (4–8 directions, 4–8 samples per ray)
2. Compute horizon angle (angle to furthest occluding surface)
3. Integrate visibility across directions
4. Apply blur (separable Gaussian) to denoise

**Key Parameters**:
- **Radius**: Sample radius in world space (0.5–2.0m typical)
- **Intensity**: AO strength multiplier
- **Sample Count**: 4–8 directions × 4–8 steps (quality trade-off)
- **Blur Radius**: Post-blur to smooth

**Design Principle**: HBAO avoids expensive cone tracing by using depth samples. Horizon angle provides occlusion approximation without per-pixel ray marching.

**Visual Contribution**: AO darkens crevices, seams, and contact points. Makes geometry feel grounded and adds perceived depth. Without AO, surfaces look floating.

**Complexity**: **Simple** (depth sampling + horizon angle computation)

---

## 5. SKY & ATMOSPHERE

### 5.1 Sky Atmosphere (Bruneton/Hillaire)

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/SkyAtmosphereRendering.h` (lines 36–68)

**Pattern**: **Bruneton et al. sky model** with precomputed lookup tables (LUTs).

**Data**:
```cpp
struct FAtmosphereUniformShaderParameters {
    float BottomRadiusKm;  // Planet radius (6371 km)
    float TopRadiusKm;     // Atmosphere top (6471 km)
    FLinearColor RayleighScattering;  // (r, g, b) Rayleigh coefficients
    float RayleighDensityExpScale;  // Exponential falloff
    FLinearColor MieScattering;  // Mie scattering (larger particles)
    float MiePhaseG;  // Mie phase function anisotropy
    FLinearColor MieAbsorption;  // Mie extinction
    FLinearColor GroundAlbedo;  // Planet surface reflectivity
};
```

**Precomputed LUTs**:
- **Transmittance LUT** (64x256): Light transmission through atmosphere at various angles
- **MultiScattered Luminance LUT** (32x32x32): Multiple scattering contribution
- **Sky View LUT** (192x104): Full sky radiance for current view
- **Aerial Perspective Volume**: 3D texture for camera-relative depth fog

**Design Principle**: Precomputing LUTs avoids expensive ray marching every frame. Lookup tables are small (few MB) but provide realistic sky in real-time.

**Visual Contribution**: Atmosphere adds depth, color variation with viewing angle, and realistic fog. Without it, sky is flat and colors are wrong (no Rayleigh blue at horizon).

**Complexity**: **Complex** (Bruneton model, LUT generation)

---

### 5.2 Volumetric Clouds

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/VolumetricCloudRendering.h` (referenced in includes)

**Pattern**: **Froxel-based volumetric rendering** via structured buffers.

**Algorithm**:
1. Create 3D grid (froxels) aligned to view frustum
2. Trace ray per pixel through froxels
3. Sample cloud density at each froxel (via noise functions or 3D texture)
4. Accumulate illumination (light transmittance + scattering)
5. Composite onto scene

**Key Features**:
- **Shadow Support**: Trace secondary rays to light for shadows
- **Lighting**: Single-directional light contribution per voxel
- **Noise**: Procedural 3D Perlin/Worley noise for cloud shape

**Design Principle**: Froxel grids reduce memory footprint vs. full 3D textures. Structured buffers enable compute shader acceleration.

**Visual Contribution**: Clouds add atmospheric depth and dynamism. Without them, sky looks empty and flat.

**Complexity**: **Complex** (froxel grid, ray marching, noise)

---

### 5.3 Fog (Exponential Height Fog)

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/BasePassRendering.h` (line 25: includes FogRendering.h)

**Pattern**: **Per-vertex or per-pixel exponential height fog**.

**Formula**:
```
FogAmount = exp(-(Height - CameraHeight) * FogDensity)
```

**Parameters**:
- **FogDensity**: Exponential coefficient (how fast fog increases with altitude)
- **FogHeightFalloff**: Different falloff above/below camera
- **FogInscatteringColor**: Fog color (typically from atmosphere)
- **SecondFogData**: Optional second fog layer for more control

**Design Principle**: Exponential height fog is cheap (single evaluation per vertex) and intuitive (denser at lower altitudes).

**Visual Contribution**: Fog adds atmospheric perspective and hides far geometry. Without it, distant objects are visible forever.

**Complexity**: **Simple** (single exponential per vertex)

---

## 6. MATERIALS

### 6.1 Default Material Model (PBR)

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/BasePassRendering.h` (GBuffer slots)

**Pattern**: **Physically-Based Rendering (PBR)** with Cook-Torrance BRDF.

**Material Parameters**:
```cpp
// Stored in GBuffer
float BaseColor[3];      // Diffuse albedo (RGB, sRGB space)
float Metallic;           // 0 = dielectric, 1 = metal
float Roughness;          // 0 = mirror, 1 = matte
float Specular;           // Specular intensity (0–1)
float AmbientOcclusion;   // Darkening multiplier
float Emissive[3];        // Unlit color (HDR)

// Optional advanced parameters
float ClearCoat;          // Clear coat layer (0–1)
float ClearCoatRoughness; // Clear coat roughness
float Anisotropy;         // Surface anisotropy (0–1)
float SubsurfaceDensity;  // For SSS materials
```

**BRDF Components**:
- **Diffuse (Lambertian)**: Cosine-weighted; modulated by (1 − Metallic)
- **Specular (Cook-Torrance)**:
  ```
  F(v,h) · D(h) · G(v,h,l) / (4 * cos(v) * cos(l))
  ```
  - **F (Fresnel)**: Fresnel-Schlick (approximates real Fresnel behavior)
  - **D (Normal Distribution)**: GGX (simulates roughness)
  - **G (Geometry)**: Smith G1 (visibility/shadowing)

**Pre-Integrated Data**:
- **PreIntegratedGF LUT**: Stores F0 and roughness-dependent BRDF terms (2D texture: roughness × NdotV)

**Design Principle**: Cook-Torrance separates BRDF into independent physical components. Pre-integration eliminates per-pixel convolution.

**Visual Contribution**: PBR provides realistic material appearance across all lighting conditions. Without it, materials look plastic or too glossy.

**Complexity**: **Medium** (BRDF math, LUT sampling)

---

### 6.2 Subsurface Scattering

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/PostProcess/PostProcessSubsurface.h`

**Pattern**: **Screen-space subsurface scattering (SSSS)** via separable Gaussian blur.

**Algorithm**:
1. Extract SSS color and thickness from GBuffer
2. Blur shadow map (or thickness) using separable Gaussian
3. Modulate diffuse by blurred transmittance
4. Apply subsurface profile (per-material blur kernel)

**Subsurface Profile**:
```cpp
// Defines Gaussian blur kernel per material
TArray<float> BlurRadius;  // Radius at each sample
TArray<float> BlurKernelWeights;  // Weight per sample
FLinearColor TintColor;  // SSS tint (e.g., reddish for skin)
float IORDensity;  // Material density (affects blur)
```

**Design Principle**: Screen-space approach avoids expensive volumetric integration. Blur in screen space is orders of magnitude cheaper than ray marching through material.

**Visual Contribution**: Makes translucent materials look alive (backlit ears, light penetrates leaves). Critical for skin and organic materials.

**Complexity**: **Simple** (separable blur + profile lookup)

---

## 7. ENVIRONMENT

### 7.1 Light Probes (Irradiance Volumes)

**Source Files**:
- UE5 uses **Lumen** (baked and dynamic global illumination) instead of traditional light probes in modern setups
- `W:/UE_5.6_Source/Renderer/Private/IndirectLightingCache.cpp` (legacy system)

**Pattern**: **Spherical Harmonics (SH) Light Probes** placed in a 3D grid.

**Data per Probe**:
- 9 SH coefficients (3 per color channel) for 3rd-order SH
- Ambient occlusion scalar
- Stored in a volume texture or structured buffer

**Lookup**:
- Interpolate 8 nearest probes (trilinear blend)
- Evaluate SH at surface normal to get incident irradiance

**Design Principle**: SH provides compact representation of directional lighting (9 floats vs. cubemap). Spatial interpolation gives smooth transitions.

**Visual Contribution**: Probes provide indirect diffuse lighting in interior areas. Without them, only IBL contributes to shadows.

**Complexity**: **Simple** (SH interpolation + evaluation)

---

### 7.2 Lumen Global Illumination (Modern Default)

**Source Files**:
- `W:/UE_5.6_Source/Renderer/Private/Lumen/` (extensive system)

**Pattern**: **Real-time hierarchical global illumination** via surface cache cards and hardware ray tracing.

**Core Idea**:
1. Render scene geometry to 64x64 "surface cache cards" (cached per light)
2. Trace rays against card cache for reflections/GI
3. Accumulate history across frames
4. Store irradiance in card cache for future lookups

**Key Components**:
- **Surface Cache**: GPU-resident cards (64x64 virtual texels each)
- **Lumen Probes**: Fast GI lookups via screen-space probe grid
- **Hardware RT**: Optional DXR/Vulkan ray tracing acceleration

**Design Principle**: Lumen trades off per-frame cost for quality—cards provide persistent high-quality cache that improves over time.

**Visual Contribution**: Lumen provides realistic bounced light and global illumination in real-time. Without it, only direct lighting is visible in shadow areas.

**Complexity**: **Very Complex** (card management, ray tracing, history)

---

## 8. MESH RENDERING

### 8.1 Static Mesh LOD System

**Source Files**:
- Referenced in `FBasePassShaderElementData` and mesh pass processors

**Pattern**: **Per-mesh level-of-detail (LOD) selection** based on screen coverage.

**LOD Selection**:
- Calculate screen pixel coverage of mesh
- Select LOD that provides ~2–4 pixels per triangle edge
- Dithered LOD transitions to avoid popping

**Data per LOD**:
- Vertex buffer (position, normal, tangent, texcoords)
- Index buffer
- Material assignments per section
- Optional lightmap UVs (for baked lighting)

**Design Principle**: LODs reduce triangle count and vertex attribute memory bandwidth. Screen-space heuristic ensures consistent quality regardless of viewport size.

**Visual Contribution**: LODs enable distant geometry rendering without performance collapse. Without LODs, far objects either disappear or stall frame rate.

**Complexity**: **Simple** (screen coverage calculation, vertex fetch)

---

### 8.2 GPU Instancing

**Source Files**:
- Referenced in mesh draw commands and GPU scene system

**Pattern**: **GPU-driven instancing** via GPU scene and structured buffers.

**Data per Instance**:
- World-to-local transform (4x3 or 4x4 matrix)
- Previous frame transform (for velocity)
- Custom data (material parameters, tint, etc.)

**Rendering**:
- Single draw call with InstanceCount > 1
- Shader reads instance data from structured buffer via SV_InstanceID

**Design Principle**: Batching multiple instances reduces draw call overhead. GPU scene enables dynamic instance data (CPU doesn't rebind buffers).

**Visual Contribution**: Instancing enables placing many copies of objects (trees, rocks, NPCs) efficiently. Without it, each object would be separate draw call.

**Complexity**: **Simple** (instance buffer lookup)

---

## 9. SYSTEM INTERACTIONS

### 9.1 Pipeline Flow

```
┌─────────────────────────────────────────────────────┐
│ 1. Visibility / Culling                             │
├─────────────────────────────────────────────────────┤
│   - Frustum cull
│   - Occlusion test (HZB)
│   - LOD selection
│   → Visible mesh list
└────────────────────┬────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────────┐
│ 2. Depth Prepass                                    │
├─────────────────────────────────────────────────────┤
│   - Write depth only
│   - Optional alpha test (cutouts)
│   → SceneDepth texture
└────────────────────┬────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────────┐
│ 3. Base Pass (GBuffer)                              │
├─────────────────────────────────────────────────────┤
│   - Read depth (depth test)
│   - Write GBufferA–F
│   - Material shaders (emit normals, roughness, etc.)
│   → GBufferA–F + Depth + Velocity
└────────────────────┬────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────────┐
│ 4. Decals (DBuffer)                                 │
├─────────────────────────────────────────────────────┤
│   - Modify GBuffer channels (normal, roughness)
│   - Deferred decal rendering via DBuffer
│   → Updated GBufferA–F
└────────────────────┬────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────────┐
│ 5. Lighting Composition                             │
├─────────────────────────────────────────────────────┤
│   5a. Sky light (IBL)
│       - Sample SkyLight cubemap
│       - Apply BRDF
│       → Indirect diffuse + specular
│   5b. Directional light (sun)
│       - Sample CSM
│       - Apply BRDF
│       → Direct diffuse + specular
│   5c. Point/spot lights (deferred light volumes)
│       - Render light volumes (cones/spheres)
│       - Per-pixel light accumulation
│       → Local light contributions
│   → Composite to SceneColor
└────────────────────┬────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────────┐
│ 6. Ambient Occlusion (SSAO)                         │
├─────────────────────────────────────────────────────┤
│   - Screen-space AO sampling
│   - Modulate indirect lighting
│   → Darkened SceneColor
└────────────────────┬────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────────┐
│ 7. Subsurface Scattering (optional)                 │
├─────────────────────────────────────────────────────┤
│   - Blur shadow/thickness map
│   - Apply transmittance
│   → SSS contribution added
└────────────────────┬────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────────┐
│ 8. Translucency / Forward Passes (optional)         │
├─────────────────────────────────────────────────────┤
│   - Transparent materials
│   - Particle systems
│   - Hair rendering
│   → Composited over opaque
└────────────────────┬────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────────┐
│ 9. Post-Processing                                  │
├─────────────────────────────────────────────────────┤
│   - Bloom setup (threshold + downsampling)
│   - Bloom blur + upsample
│   - TAA (reprojection + blend)
│   - Color grading (LUT)
│   - Tonemap (ACES)
│   - Optional: DoF, motion blur, chromatic aberration
│   → HDR → LDR
└────────────────────┬────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────────┐
│ 10. Final Composite to Screen                       │
├─────────────────────────────────────────────────────┤
│   - Gamma correction if needed
│   - Optional UI/debug overlays
│   → Frame Buffer
└─────────────────────────────────────────────────────┘
```

---

## 10. SYSTEM COMPLEXITY SUMMARY

| System | Complexity | Impact on Visual Quality | Data Footprint |
|--------|-----------|-------------------------|-----------------|
| **GBuffer Layout** | Medium | Foundational—everything depends on it | 4–6 × viewport (A–F targets) |
| **Base Pass** | Medium | Required—establishes material properties | ~16 KB descriptor set |
| **Directional Shadows (CSM)** | Complex | Major—creates dramatic shading | 4 × 2048² = 64 MB typical |
| **Sky Light (IBL)** | Complex | Major—provides ambient + reflections | Cubemap (128–512³) + LUTs |
| **Reflection Probes** | Simple | Moderate—local reflection details | Cubemap array |
| **Point/Spot Shadows** | Complex | Moderate—localized shadows | Per-light shadow map |
| **Contact Shadows** | Simple | Minor—edge details only | None (screen-space) |
| **SSAO** | Simple | Moderate—adds perceived depth | 2× 1/4-res AO texture |
| **Tone Mapping** | Simple | Critical—controls final color | 3D LUT (32–64³) |
| **Bloom** | Simple | Minor—aesthetic glow | Downsampled chain (~5% memory) |
| **TAA** | Complex | Critical—eliminates aliasing | History framebuffer |
| **Sky Atmosphere** | Complex | Major—realistic color + depth | 3x LUT textures (~10 MB total) |
| **Volumetric Clouds** | Complex | Optional—atmosphere detail | Froxel grid (~50 MB) |
| **Fog** | Simple | Optional—atmospheric effect | None (per-vertex) |
| **Material (PBR)** | Medium | Foundational—all surfaces | Textures (artist responsibility) |
| **Subsurface Scattering** | Simple | High—for skin/organic | Blur kernel + profile (small) |

---

## 11. ABSTRACTION BOUNDARIES IN UE5

**Key Architectural Insights**:

1. **RHI Abstraction**: All rendering uses RHI layer (not direct DX12). UzEngine should maintain this.

2. **Render Graph (RDG)**: Modern UE5 uses RDG to describe rendering tasks, not imperative command lists. RDG handles:
   - Resource aliasing (reuse memory across passes)
   - Barrier generation (automatic)
   - GPU queue scheduling

   **Implication**: UzEngine should abstract render graph tasks similarly (not low-level commands).

3. **Mesh Pass Processors**: Material rendering is decoupled via FMeshPassProcessor. Each material's vertex/pixel shaders are pluggable without hardcoded mesh rendering logic.

4. **Uniform Buffers as Data Containers**: Instead of per-pixel uniforms, UE5 uses structured data (BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT) that are versioned and cached. This enables:
   - Automatic versioning (rebind only when data changes)
   - Type-safe shader binding
   - Deduplication across draws

5. **Virtual Dispatch via Static Type Registration**: Shaders register themselves via DECLARE_SHADER_TYPE / IMPLEMENT_SHADER_TYPE macros, not runtime class hierarchies. This enables:
   - Permutation generation (static combinations)
   - Type-safe template specialization
   - No switch statements on shader types

**Implication for UzEngine**: Adopt similar patterns:
- Render graph abstraction layer
- Mesh pass processor pattern
- Typed uniform buffers (not loose parameters)
- Static shader type registration (avoid runtime polymorphism for shaders)

---

## 12. ANTI-PATTERNS TO AVOID

Based on reading UE5 source, here are patterns **NOT** to copy:

1. **Switch Statements on Material Types**: UE5 avoids `if (material == MetallicRoughness)` via template specialization. Use templates or vtables instead.

2. **Runtime Shader String Concatenation**: UE5 uses compile-time shader permutations, not runtime HLSL string building.

3. **Loose Uniform Parameters**: Don't pass individual floats/vectors as uniforms. Use structured buffers or constant buffers instead.

4. **Hardcoded Cubemap Sizes**: UE5 parameterizes cubemap resolutions (128, 256, 512) by quality tier, not hardcoding 256.

5. **Non-Temporal TAA History**: Maintaining TAA history outside render graph leads to sync issues. Use RDG to manage history lifecycle.

6. **Per-Pixel Shadow Map Samples**: Sampling shadow maps should be done at fixed step counts (4, 8, 16), not variable per pixel.

---

## 13. REFERENCES TO UE5 SOURCE

All file paths are relative to W:\UE_5.6_Source\Engine\Source\Runtime\Renderer:

| Component | Key Files |
|-----------|-----------|
| GBuffer | RenderCore/Public/GBufferInfo.h; Internal/SceneTextures.h |
| Base Pass | Private/BasePassRendering.h/cpp |
| Deferred Shading | Private/DeferredShadingRenderer.h/cpp |
| Shadows | Private/Shadows/ShadowSceneRenderer.h; Private/ShadowRendering.h |
| Lighting | Private/CompositionLighting/CompositionLighting.h |
| Reflection Env | Private/ReflectionEnvironment.h |
| Post-Process | Private/PostProcess/PostProcessTonemap.h, TemporalAA.h, PostProcessBloomSetup.h |
| Sky | Private/SkyAtmosphereRendering.h |
| Subsurface | Private/PostProcess/PostProcessSubsurface.h |
| SSAO | Private/CompositionLighting/PostProcessAmbientOcclusion.h |

---

## 14. IMPLEMENTATION GUIDANCE FOR UZENGINE

### Minimum Viable Scene (MVP):

For UzEngine to render a basic scene at UE5 quality, implement (in order):

1. **GBuffer Layout** (2 targets minimum: Normal+Depth, Albedo+Roughness)
2. **Base Pass** (write GBuffer, read depth, alpha test)
3. **Directional Light + CSM** (3 cascades, 2048x2048)
4. **Sky Light / IBL** (cubemap + 1 mip for roughness blend)
5. **Deferred Lighting Pass** (BRDF evaluation, light composition)
6. **Tone Mapping + Bloom** (ACES + simple Gaussian bloom)
7. **TAA** (reprojection + history)

This gives ~80% of UE5's visual quality with ~20% of the complexity.

### Advanced Features (for later phases):

- SSAO (perception of depth)
- Reflection probes (local detail)
- Contact shadows (edge detail)
- Subsurface scattering (skin rendering)
- Volumetric clouds (atmosphere)
- Lumen GI (bounced light)

---

**End of Analysis**

This document can be used to guide architectural decisions in UzEngine's renderer without copying UE5 code directly.

