<!-- doc-meta
title: UE5 Rendering CVars Reference — Complete Configuration System
author: UzEngine Architecture Analysis
date: 2026-04-04
status: reference
accuracy: primary-source (W:\UE_5.6_Source)
-->

# UE5 Rendering CVars Reference — Complete Configuration System

This document provides a comprehensive list of all rendering-related CVars (console variables) that UE5 5.6 exposes for runtime configuration. This is extracted from actual UE5 source code to ensure accuracy.

**Purpose**: For UzEngine to match UE5's level of configurability, every shader parameter must be runtime-configurable via CVars. This list provides the definitive reference for what parameters should exist, their defaults, ranges, and what they control.

**Key Principles**:
1. All shader parameters must be configurable at runtime (no hardcoded values)
2. Parameter names follow UE5's `r.<Category>.<Parameter>` naming convention
3. Defaults must match published references (papers, techniques)
4. Ranges and clamps are determined by the technique, not arbitrary

---

## 1. Tone Mapping & Color Grading

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Tonemapper.Sharpen` | float | -1.0 | [-1, 10] | Sharpening in tonemapper (post-tone curve). Negative = inherit from PostProcessVolume. 0 = off. Clamped to [0, 10] in shader. |
| `r.TonemapperGamma` | float | 0.0 | [0, ∞) | Output gamma mapping. 0 = default (sRGB or Rec709). Positive = fixed gamma value. |

**Implementation Notes**:
- `r.Tonemapper.Sharpen` feeds into the ACES tone curve as a final sharpening pass
- `r.TonemapperGamma` controls display-referred color space (must match output device capability)
- These are read during final tonemapping stage (`PostProcessTonemap.cpp`)

---

## 2. Bloom & HDR Post-Process

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Bloom.ScreenPercentage` | float | 100.0 | [0, ∞) | Screen percentage for FFT bloom convolution kernel resolution. Controls axis resolution of FFT. |
| `r.Bloom.CacheKernel` | int32 | 1 | [0, 1] | Whether to cache the kernel in spectral domain (0=no, 1=yes). Tradeoff: memory vs. recomputation. |
| `r.Bloom.AsyncCompute` | int32 | 1 | [0, 1] | Run FFT bloom on async compute queue (0=off, 1=on). |
| `r.Bloom.WarnKernelResolution` | int32 | 1 | [0, 2] | Emit warning if kernel resolution is unnecessarily high. 0=disabled, 1=consoles only, 2=all platforms. |
| `r.Bloom.ApplyLocalExposure` | int32 | unset | [0, 1] | Apply local exposure before bloom (eye adaptation). |
| `r.BloomQuality` | int32 | unset | [0, 4] | Quality setting for bloom. Higher = more expensive but better quality. |

**Implementation Notes**:
- Bloom is computed via FFT convolution in spectral domain
- `r.Bloom.ScreenPercentage` directly controls resolution: `resolution = viewport * (screenPercentage / 100.0)`
- Kernel is cached per-frame if `r.Bloom.CacheKernel = 1`, improving performance at memory cost
- Async compute requires a compute queue (DX12 required)

---

## 3. Ambient Occlusion (GTAO)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.GTAO.UseNormals` | int32 | 0 | [0, 1] | Use GBuffer normals (1) or depth-derived normals (0). |
| `r.GTAO.FilterWidth` | int32 | 5 | [4, 5] | Noise pattern size. 4=4x4 pattern, 5=5x5 pattern (default). Larger = more noise but faster. |
| `r.GTAO.ThicknessBlend` | float | 0.5 | [0, 1] | Heuristic bias for thin/thick objects. 0=off, >0=on (default 0.5). Larger values reduce occlusion. |
| `r.GTAO.FalloffEnd` | float | 200.0 | [0, ∞) | Distance at which occlusion completes falloff (in cm). Defines max occlusion radius. |
| `r.GTAO.FalloffStartRatio` | float | 0.5 | [0, 1] | Ratio of `r.GTAO.FalloffEnd` where falloff begins. Controls occlusion fade-in. Must be in [0, 1]. |
| `r.GTAO.NumAngles` | float | 2 | [1, 16] | Horizon search directions per pixel. Larger = better quality but slower. |
| `r.GTAO.PauseJitter` | int32 | 0 | [0, 1] | Pause jitter when temporal filter is off (1=pause, 0=continue). |
| `r.GTAO.Upsample` | int32 | 1 | [0, 1] | Upsampling filter for GTAO. 0=simple (faster), 1=depth-aware (better, default). |
| `r.GTAO.Downsample` | int32 | 2 | [1, 2] | Downsample factor for GTAO computation. 2x = 4 times fewer pixels. |
| `r.GTAO.TemporalFilter` | int32 | 1 | [0, 1] | Temporal filtering of GTAO. 1=enabled (default), 0=disabled. |
| `r.GTAO.SpatialFilter` | int32 | 1 | [0, 1] | Spatial filtering of GTAO. 1=enabled (default), 0=disabled. |

**Implementation Notes**:
- GTAO is a scalable, modern replacement for SSAO with better results
- `r.GTAO.FalloffEnd` and `r.GTAO.FalloffStartRatio` define the kernel radius function:
  - `falloffStart = falloffEnd * falloffStartRatio`
  - `falloffScale = 1.0 / (falloffEnd^2 - falloffStart^2)`
- Temporal jitter uses frame index mod 6 to select from fixed angle set: [60°, 300°, 180°, 240°, 120°, 0°]

---

## 4. Screen-Space Reflections (SSR)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.SSR.Quality` | int32 | 3 | [0, 3] | Quality of screen-space reflections. 0=off (default), 1=low (no glossy), 2=medium (no glossy), 3=high (glossy). |
| `r.SSR.Temporal` | int32 | 1 | [0, 1] | Temporal smoothing of SSR. 0=off (debug), 1=on (default). |
| `r.SSR.Stencil` | int32 | 0 | [0, 1] | Use stencil prepass for SSR (0=off default, 1=on). Optimizes by skipping unlit pixels. |
| `r.SSR.Compute` | int32 | 0 | [0, 2] | Compute shader path. 0=pixel shader (default), 1=sync compute, 2=async compute. |
| `r.SSR.HalfResSceneColor` | int32 | unset | [0, 1] | Use half-resolution scene color for SSR input. Tradeoff: speed vs. quality. |
| `r.SSR.ExperimentalDenoiser` | int32 | 0 | [0, 1] | Enable experimental denoiser for SSR. |
| `r.SSR.TiledComposite` | int32 | 0 | [0, 1] | Use tiled composite optimization. 0=off (default), 1=on. |
| `r.SSR.TiledComposite.OverrideMaxRoughness` | float | -1.0 | [-1, 1] | Override max roughness for tiled composite. -1=disabled (use material roughness). |
| `r.SSR.TiledComposite.MinSpecular` | float | 0.0 | [0, 1] | Minimum specular threshold for SSR application. |
| `r.SSR.TiledComposite.TwoSidedFoliage` | int32 | 0 | [0, 1] | Special handling for two-sided foliage reflections. |
| `r.SSR.TiledComposite.Visualize` | int32 | 0 | [0, 1] | Debug visualization of tiled composite tiles. |

**Implementation Notes**:
- SSR only reflects screen-space content (limited to what camera can see)
- Temporal filtering requires motion vectors; jitter must match TAA
- `r.SSR.Quality` gate flags (quality == 0 disables entire pass):
  - Quality 1-2: exclude glossy reflections (roughness > threshold)
  - Quality 3: include all reflections
- Max roughness typically clamped at ~0.8 (beyond this, reflections too noisy)

---

## 5. Screen-Space Global Illumination (SSGI)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.SSGI.Quality` | int32 | 4 | [1, 4] | Quality setting for ray count. 1=minimal, 4=maximal (default). Controls samples per pixel. |
| `r.SSGI.LeakFreeReprojection` | int32 | 1 | [0, 1] | Use expensive leak-free reprojection (0=simple, 1=leak-free default). Prevents lighting artifacts at occlusion edges. |
| `r.SSGI.MinimumLuminance` | float | 0.5 | [0, ∞) | Reject GI samples below this luminance threshold. Reduces noise from dark occluders. |
| `r.SSGI.RejectUncertainRays` | int32 | 1 | [0, 1] | Reject rays that went behind screen geometry (0=use, 1=reject default). |
| `r.SSGI.TerminateCertainRay` | int32 | 1 | [0, 1] | Don't fallback to other tracing if ray is certain & found nothing (0=fallback, 1=terminate default). |
| `r.SSGI.SkyDistance` | float | 10000000 | [0, ∞) | Distance to sky in kilometers. Used for ray termination. |

**Implementation Notes**:
- SSGI uses cone-traced rays from each pixel to sample diffuse GI
- Temporal blend weight typically 0.1-0.3 (conservative to avoid ghosting)
- Ray quality determines sample count per-pixel (affects per-pixel cost dramatically)
- Fallback for uncertain rays uses sky color or irradiance probe

---

## 6. Shadow Mapping

### Directional Lights (CSM)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Shadow.CSMDepthBias` | float | 10.0 | [0, ∞) | Constant depth bias for Cascaded Shadow Maps. Prevents self-shadowing artifacts. |
| `r.Shadow.CSMSlopeScaleDepthBias` | float | 3.0 | [0, ∞) | Slope-scale depth bias for CSM. Increases bias on angled surfaces. |
| `r.Shadow.CSMReceiverBias` | float | 0.9 | [0, 1] | Receiver-side bias for CSM. Offsets light backward toward surfaces. |
| `r.Shadow.CSMSplitPenumbraScale` | float | unset | [0, ∞) | Penumbra scaling for CSM transitions. Controls soft shadow falloff. |
| `r.Shadow.TransitionScale` | float | 60.0 | [0, ∞) | Fade-in region size between caster and shadow. Larger = smaller region = more self-shadow artifacts. |
| `r.Shadow.CSM.MaxCascades` | int32 | unset | [1, 4] | Maximum number of cascade levels (typically 1-4). |

### Point Lights

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Shadow.PointLightDepthBias` | float | 0.02 | [0, ∞) | Constant depth bias for point light shadows (dual paraboloid or cubemap). |
| `r.Shadow.PointLightSlopeScaleDepthBias` | float | 3.0 | [0, ∞) | Slope-scale depth bias for point lights. |

### Spot Lights

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Shadow.SpotLightDepthBias` | float | 3.0 | [0, ∞) | Constant depth bias for whole-scene spot light shadows. |
| `r.Shadow.SpotLightSlopeDepthBias` | float | 3.0 | [0, ∞) | Slope-scale depth bias for spot lights. |
| `r.Shadow.SpotLightTransitionScale` | float | 60.0 | [0, ∞) | Transition scale for spot light shadow fade-in. |
| `r.Shadow.SpotLightReceiverBias` | float | 0.5 | [0, 1] | Receiver bias for spot lights. |
| `r.Shadow.PerObjectSpotLightDepthBias` | float | 3.0 | [0, ∞) | Depth bias for per-object spot light shadows. |
| `r.Shadow.PerObjectSpotLightSlopeDepthBias` | float | 3.0 | [0, ∞) | Slope-scale bias for per-object spot shadows. |

### Rect Lights

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Shadow.RectLightDepthBias` | float | 0.025 | [0, ∞) | Constant depth bias for rect light shadows. |
| `r.Shadow.RectLightSlopeScaleDepthBias` | float | 2.5 | [0, ∞) | Slope-scale depth bias for rect lights. |
| `r.Shadow.RectLightReceiverBias` | float | 0.3 | [0, 1] | Receiver bias for rect lights. |

### Directional Lights Per-Object

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Shadow.PerObjectDirectionalDepthBias` | float | 10.0 | [0, ∞) | Constant bias for per-object directional shadows. Lower = better contact, more self-shadow. |
| `r.Shadow.PerObjectDirectionalSlopeDepthBias` | float | 3.0 | [0, ∞) | Slope-scale bias for per-object directional shadows. |

### General Shadow Settings

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Shadow.ShadowMaxSlopeScaleDepthBias` | float | 1.0 | [0, ∞) | Maximum slope-scale depth bias clamped across all lights. Prevents excessive self-shadowing artifacts. |
| `r.Shadow.EnableModulatedSelfShadow` | int32 | 0 | [0, 1] | Allow modulated shadows on shadow casters (mobile only). |
| `r.Shadow.StencilOptimization` | int32 | 1 | [0, 1] | Use stencil optimization for shadow rendering. |
| `r.Shadow.LODDistanceFactor` | float | unset | [0, ∞) | Distance-based LOD factor for shadow casting. |
| `r.Shadow.LODDistanceFactor.CascadeScale` | float | unset | [0, ∞) | Cascade-specific LOD distance scaling. |
| `r.Shadow.FarShadow.LODDistanceFactor` | float | unset | [0, ∞) | LOD factor for far cascade shadows. |
| `r.Shadow.OnePassPointLight.UseCachedMDCs` | int32 | unset | [0, 1] | Cache modified draw calls for one-pass point light shadows. |
| `r.Shadow.ShouldBeginDeferredCullingAfterShadowRendering` | int32 | unset | [0, 1] | Culling optimization for deferred rendering. |
| `r.Shadow.Denoiser` | int32 | unset | [0, 2] | Shadow denoiser type. 0=off, 1=temporal, 2=advanced. |
| `r.Shadow.Denoiser.MaxBatchSize` | int32 | 4 | [1, ∞) | Max batch size for shadow denoiser passes. |
| `r.Shadow.Denoiser.ReconstructionSamples` | int32 | 8 | [1, ∞) | Number of samples for denoiser reconstruction. |
| `r.Shadow.Denoiser.PreConvolution` | int32 | 1 | [0, 1] | Pre-convolve shadows before denoising. |
| `r.Shadow.Denoiser.TemporalAccumulation` | int32 | 1 | [0, 1] | Temporal accumulation in denoiser. |
| `r.Shadow.Denoiser.HistoryConvolutionSamples` | int32 | 1 | [0, ∞) | History convolution sample count. |
| `r.Shadow.MinDirectionalLightAngleForRTHF` | float | unset | [0, 90] | Min sun angle (degrees) before ray-traced hard shadows. |

**Implementation Notes**:
- Depth bias prevents self-shadowing: `depth_bias = constant + slope * tan(angle)`
- Receiver bias offsets light space position (GPU-side bias, more expensive)
- CSM split strategy determined by cascade ratios (0.1, 0.2, 0.5 typical)
- Per-object shadows use tighter bias (objects self-shadow)
- Shadow denoiser runs temporal reconstruction on sparse contact shadows

---

## 7. Temporal Anti-Aliasing (TAA) & Temporal Upscaling (TSR)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.TemporalAAFilterSize` | float | 1.0 | [0, ∞) | Filter kernel size. 1.0=smooth, 0.0=sharp but aliased. Controls AA smoothness. |
| `r.TemporalAACatmullRom` | int32 | 0 | [0, 1] | Use Catmull-Rom filter kernel (1) vs Gaussian (0). Catmull-Rom sharper. |
| `r.TemporalAAPauseCorrect` | int32 | 1 | [0, 1] | Correct TAA during pause. 1=hold targets (default, more memory). |
| `r.TemporalAACurrentFrameWeight` | float | 0.04 | [0, 1] | Weight of current frame in history blend. Low = blur + ghost, high = jitter visible. |
| `r.TemporalAA.Quality` | int32 | 2 | [0, 3] | TAA quality. 0=no filtering, 1=input filter, 2=filter+anti-ghost (default), 3=alt filter. |
| `r.TemporalAA.HistoryScreenPercentage` | float | 100.0 | [0, ∞) | History buffer size as % of screen. <100% = faster but blurry. |
| `r.TemporalAA.Upscaler` | int32 | 1 | [0, 1] | TAA upscaler. 0=default renderer, 1=plugin-overridable (default). |
| `r.TemporalAA.R11G11B10History` | int32 | 1 | [0, 1] | History bitdepth. 0=full precision (slower), 1=R11G11B10 (default, faster). |
| `r.TemporalAA.UseMobileConfig` | int32 | 0 | [0, 1] | Use mobile TAA config (disables groupshared caching). Mobile platforms. |
| `r.TemporalAA.Mobile.UseCompute` | int32 | 1 | [0, 1] | Mobile: use compute shader (1) vs pixel shader (0). Compute saves bandwidth on tiled GPUs. |
| `r.TemporalAASamples` | int32 | unset | [1, ∞) | Number of temporal samples (jitter positions). Higher = better AA. |
| `r.TemporalAAScaleSamples` | int32 | unset | [0, 1] | Scale samples with render target size. |
| `r.TemporalAA.Debug.OverrideTemporalIndex` | int32 | -1 | [-1, ∞) | Force temporal sample index (debug). -1=normal. |

**Implementation Notes**:
- Jitter is Halton sequence or permuted grid (reproducible per-frame)
- History weight formula: `output = history * (1 - weight) + current * weight`
- R11G11B10 saves bandwidth but loses precision in blues (acceptable for LDR)
- TAA with motion vectors: pixel motion determines reprojection
- Mobile config disables LDS (local data share) caching for bandwidth reduction

---

## 8. Sky & Atmosphere

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.SkyAtmosphere` | int32 | 1 | [0, 1] | Enable sky atmosphere rendering (Bruneton technique). |
| `r.SkyAtmosphere.SampleCountMin` | float | 2.0 | [1, 32] | Min ray samples for sky atmosphere computation. Low = fast. |
| `r.SkyAtmosphere.SampleCountMax` | float | 32.0 | [2, 32] | Max ray samples for sky atmosphere computation. High = quality. |
| `r.SkyAtmosphere.DistanceToSampleCountMax` | float | 150.0 | [0, ∞) | Distance (km) where sample count ramps to max. |
| `r.SkyAtmosphere.SampleLightShadowmap` | int32 | 1 | [0, 1] | Sample directional light shadow during atmosphere computation. |
| `r.SkyAtmosphere.FastSkyLUT` | int32 | 1 | [0, 1] | Use fast precomputed sky LUT (cheaper than per-frame computation). |
| `r.SkyAtmosphere.FastSkyLUT.SampleCountMin` | float | 4.0 | [1, 32] | Min samples for fast sky LUT precomputation. |
| `r.SkyAtmosphere.FastSkyLUT.SampleCountMax` | float | 32.0 | [4, 32] | Max samples for fast sky LUT. |
| `r.SkyAtmosphere.FastSkyLUT.DistanceToSampleCountMax` | float | 150.0 | [0, ∞) | Distance for LUT sample ramp. |
| `r.SkyAtmosphere.FastSkyLUT.Width` | int32 | 192 | [64, 512] | Sky LUT width (pixels). Higher = better detail. |
| `r.SkyAtmosphere.FastSkyLUT.Height` | int32 | 104 | [32, 256] | Sky LUT height (pixels). |
| `r.SkyAtmosphere.AerialPerspective.DepthTest` | int32 | 1 | [0, 1] | Depth test for aerial perspective application. |
| `r.SkyAtmosphere.AerialPerspectiveLUT.DepthResolution` | float | 16.0 | [1, 64] | Depth discretization for aerial perspective LUT. |
| `r.SkyAtmosphere.AerialPerspectiveLUT.Depth` | float | 96.0 | [10, 200] | Depth range (km) for aerial perspective LUT. |
| `r.SkyAtmosphere.AerialPerspectiveLUT.SampleCountMaxPerSlice` | float | 2.0 | [1, 32] | Samples per depth slice in aerial perspective. |
| `r.SkyAtmosphere.AerialPerspectiveLUT.Width` | int32 | 32 | [16, 128] | Aerial perspective LUT width. |
| `r.SkyAtmosphere.AerialPerspectiveLUT.FastApplyOnOpaque` | int32 | 1 | [0, 1] | Fast-apply aerial perspective on opaque (no per-pixel depth). |
| `r.SkyAtmosphere.TransmittanceLUT` | int32 | 1 | [0, 1] | Use precomputed transmittance LUT. |
| `r.SkyAtmosphere.TransmittanceLUT.SampleCount` | float | 10.0 | [1, 64] | Ray samples for transmittance LUT. |
| `r.SkyAtmosphere.TransmittanceLUT.UseSmallFormat` | int32 | 0 | [0, 1] | Use smaller precision for transmittance (faster, less quality). |
| `r.SkyAtmosphere.TransmittanceLUT.Width` | int32 | 256 | [128, 512] | Transmittance LUT width. |
| `r.SkyAtmosphere.TransmittanceLUT.Height` | int32 | 64 | [32, 256] | Transmittance LUT height. |
| `r.SkyAtmosphere.MultiScatteringLUT.SampleCount` | float | 15.0 | [1, 64] | Samples for multi-scattering LUT. Higher = more bounces. |
| `r.SkyAtmosphere.MultiScatteringLUT.HighQuality` | float | 0.0 | [0, 1] | High quality multi-scattering (more expensive). |
| `r.SkyAtmosphere.MultiScatteringLUT.Width` | int32 | 32 | [16, 128] | Multi-scattering LUT width. |
| `r.SkyAtmosphere.MultiScatteringLUT.Height` | int32 | 32 | [16, 128] | Multi-scattering LUT height. |
| `r.SkyAtmosphere.DistantSkyLightLUT` | int32 | 1 | [0, 1] | Use distant sky light LUT. |
| `r.SkyAtmosphere.DistantSkyLightLUT.Altitude` | float | 6.0 | [0, 100] | Observer altitude (km) for distant sky light LUT. |
| `r.SkyAtmosphere.LUT32` | int32 | 0 | [0, 1] | Use 32-bit precision for LUTs (slower but more accurate). |
| `r.SkyAtmosphere.EditorNotifications` | int32 | 1 | [0, 1] | Show editor warnings about expensive sky computations. |
| `r.SkyAtmosphereAsyncCompute` | int32 | 0 | [0, 1] | Run sky atmosphere on async compute queue. |

**Implementation Notes**:
- Sky atmosphere implements Bruneton's atmospheric light transport (2010)
- Transmittance LUT: precomputed transmission through atmosphere (V-shaped table)
- Aerial perspective: fog color from atmosphere, applied to scene pixels
- Multi-scattering models atmospheric bounces (cheap approximation)
- LUT resolutions are critical: too small = banding, too large = cache misses
- Sample counts control integration quality (raymarch steps)

---

## 9. Volumetric Clouds

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.VolumetricCloud` | int32 | 1 | [0, 1] | Enable volumetric cloud rendering. |
| `r.VolumetricCloud.DistanceToSampleMaxCount` | float | 15.0 | [1, 100] | Distance (km) where sample count ramps to max. |
| `r.VolumetricCloud.SampleMinCount` | int32 | 2 | [1, 256] | Minimum ray march steps per pixel. |
| `r.VolumetricCloud.SampleClampCount` | int32 | 2147483647 | [1, ∞) | Maximum ray march steps (hardware limit clamp). |
| `r.VolumetricCloud.ViewRaySampleMaxCount` | int32 | 768 | [1, 1024] | Max samples for main view ray tracing. Controls quality/cost. |
| `r.VolumetricCloud.ReflectionRaySampleMaxCount` | int32 | 80 | [1, 256] | Max samples for reflection ray tracing (cheaper than view rays). |
| `r.VolumetricCloud.StepSizeOnZeroConservativeDensity` | int32 | 1 | [0, 1] | Skip steps where density is zero. Optimization. |
| `r.VolumetricCloud.HighQualityAerialPerspective` | int32 | 0 | [0, 1] | Per-pixel aerial perspective pass (expensive). |
| `r.VolumetricCloud.DisableCompute` | int32 | 0 | [0, 1] | Force pixel shader path (debug). |
| `r.VolumetricCloud.ApplyFogOnAllPixel` | int32 | 0 | [0, 1] | Apply fog to all pixels (not just opaque). |
| `r.VolumetricCloud.ApplyFogLate` | int32 | 0 | [0, 1] | Apply fog after cloud composition (reduces latency artifacts). |
| `r.VolumetricCloud.EnableAerialPerspectiveSampling` | int32 | 1 | [0, 1] | Sample aerial perspective for volumetric clouds. |
| `r.VolumetricCloud.EnableDistantSkyLightSampling` | int32 | 1 | [0, 1] | Sample distant sky light for cloud indirect lighting. |
| `r.VolumetricCloud.SoftBlendingDistanceOnTranslucent` | float | 0.5 | [0, ∞) | Soft blend distance for cloud on translucent pixels. |

### Cloud Shadows (Shadow Map)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.VolumetricCloud.ShadowMap` | int32 | 1 | [0, 1] | Enable cloud shadow map rendering. |
| `r.VolumetricCloud.ShadowMap.Debug` | int32 | 0 | [0, 1] | Debug visualization of shadow map. |
| `r.VolumetricCloud.ShadowMap.SnapLength` | float | 20.0 | [0, ∞) | World space snapping (cm) for shadow map stability. |
| `r.VolumetricCloud.ShadowMap.SnapToPixelGrid` | int32 | 1 | [0, 1] | Snap shadow map to pixel grid (prevents shimmer). |
| `r.VolumetricCloud.ShadowMap.RaySampleMaxCount` | float | 128.0 | [1, 256] | Max samples for shadow ray march. |
| `r.VolumetricCloud.ShadowMap.LightDistanceOverride` | float | 0.0 | [0, ∞) | Override light distance (0=use actual, >0=fixed). |
| `r.VolumetricCloud.ShadowMap.RaySampleHorizonMultiplier` | float | 2.0 | [0.5, 4] | Horizon search multiplier (affects shadow softness). |
| `r.VolumetricCloud.ShadowMap.MaxResolution` | int32 | 2048 | [512, 4096] | Shadow map texture resolution. Higher = memory + quality. |
| `r.VolumetricCloud.ShadowMap.SpatialFiltering` | int32 | 1 | [0, 1] | Spatial filtering of shadow map. |
| `r.VolumetricCloud.ShadowMap.TemporalFiltering.NewFrameWeight` | float | 1.0 | [0, 1] | New frame blend weight for temporal filtering. 1=only new, 0=only history. |
| `r.VolumetricCloud.ShadowMap.TemporalFiltering.LightRotationCutHistory` | float | 10.0 | [0, 360] | Light rotation angle (degrees) that invalidates shadow history. |

### Cloud Sky AO (Occlusion)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.VolumetricCloud.SkyAO` | int32 | 1 | [0, 1] | Enable sky ambient occlusion from clouds. |
| `r.VolumetricCloud.SkyAO.Debug` | int32 | 0 | [0, 1] | Debug visualization of sky AO. |
| `r.VolumetricCloud.SkyAO.SnapLength` | float | 20.0 | [0, ∞) | Snapping for sky AO stability. |
| `r.VolumetricCloud.SkyAO.MaxResolution` | int32 | 2048 | [512, 4096] | Sky AO texture resolution. |
| `r.VolumetricCloud.SkyAO.TraceSampleCount` | int32 | 10 | [1, 32] | Ray samples for sky AO trace. |
| `r.VolumetricCloud.SkyAO.Filtering` | int32 | 1 | [0, 1] | Spatial filtering of sky AO. |

### Cloud Ray Tracing

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.VolumetricCloud.Shadow.ViewRaySampleMaxCount` | int32 | 80 | [1, 256] | Shadow view ray sample count. |
| `r.VolumetricCloud.Shadow.ReflectionRaySampleMaxCount` | int32 | 24 | [1, 128] | Shadow reflection sample count. |
| `r.VolumetricCloud.Shadow.SampleAtmosphericLightShadowmap` | int32 | 1 | [0, 1] | Sample light shadowmap during cloud shadow tracing. |

**Implementation Notes**:
- Cloud rendering uses ray marching through volumetric density field
- Sample counts scale with distance: `samples = lerp(minCount, maxCount, clamp(distance / distanceToMax, 0, 1))`
- Shadow map uses cascading resolution (higher near camera)
- Sky AO computed separately from shadow map (different spatial distribution)
- Temporal filtering requires light direction tracking (invalidates on large rotations)

---

## 10. Volumetric Fog

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Fog` | int32 | 1 | [0, 1] | Enable fog rendering. |
| `r.FogUseDepthBounds` | bool | true | [0, 1] | Depth bounds optimization for fog fullscreen pass. Skips far pixels. |
| `r.VolumetricFog.UpsampleJitterMultiplier` | float | 0.0 | [0, ∞) | Random jitter multiplier for volumetric fog sampling (hides pixelization). |
| `r.VolumetricFog.LightFunction` | int32 | unset | [0, 1] | Apply light functions to volumetric fog (expensive). |
| `r.VolumetricFog.UsesLightFunctionAtlas` | int32 | unset | [0, 1] | Use light function atlas for volumetric fog. |
| `r.Water.SingleLayer.UnderwaterFogWhenCameraIsAboveWater` | bool | false | [0, 1] | Render height fog behind water when camera above. Reduces artifacts but adds new ones. |
| `r.Ortho.FogHeightAdjustment` | bool | true | [0, 1] | Use orthographic camera height for fog cutoff. |

**Implementation Notes**:
- Fog is computed per-pixel at final pass (fullscreen quad)
- Exponential height fog: `fog = saturate(exp(-density * (height - cameraHeight) / falloff))`
- Depth bounds test: `if (depth > fog_distance) skip_pixel` (optimization)
- Volumetric fog uses 3D texture (froxel grid) updated per-frame

---

## 11. Refraction & Distortion

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Refraction.Blur.TemporalAA` | int32 | unset | [0, 1] | Apply TAA to refraction blur pass. |

**Implementation Notes**:
- Refraction uses screen-space sampling with material normal perturbation
- Blur can be expensive (bilateral gaussian with depth weighting)

---

## 12. Ambient Lighting & IBL

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.SkySpecularOcclusionStrength` | float | unset | [0, 1] | Strength of sky specular occlusion. Dims specular reflections in occluded areas. |
| `r.SkylightCapture.LODDistanceScale` | float | unset | [0, ∞) | LOD bias for skylight capture distance calculations. |
| `r.SkyLight.RealTimeReflectionCapture.TimeSlice` | int32 | 1 | [1, ∞) | Frames per skylight update. 1=every frame, >1=less frequent. |
| `r.SkyLight.RealTimeReflectionCapture.TimeSlice.SkyCloudCubeFacePerFrame` | int32 | 2 | [1, 6] | Cubemap faces to render per frame. 2=60fps at 60hz. |
| `r.SkyLight.RealTimeReflectionCapture.ShadowFromOpaque` | int32 | 0 | [0, 1] | Include opaque geometry shadows in skylight capture. |
| `r.SkyLight.RealTimeReflectionCapture.DepthBuffer` | int32 | 1 | [0, 1] | Use depth buffer during skylight capture (enables masking). |
| `r.SkyLight.RealTimeReflectionCapture.AlwaysClearColorBuffer` | int32 | 0 | [0, 1] | Always clear target before skylight capture. |
| `r.SkyLight.RealTimeReflectionCapture.VolumetricCloudResolutionDivider` | int32 | 2 | [1, 4] | Volumetric cloud resolution reduction for skylight. |
| `r.SkyLight.RealTimeReflectionCapture.ResolutionOverride` | int32 | 0 | [0, 2048] | Override skylight cubemap resolution (0=auto). |

**Implementation Notes**:
- Skylight uses 6-face cubemap capture of scene radiance
- Time slicing spreads cost across frames (temporal coherence required)
- Real-time updates needed for dynamic lighting (moving sun, etc.)

---

## 13. Ray Tracing Features

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.RayTracing` | int32 | unset | [0, 1] | Enable ray tracing features (DXR). |
| `r.RayTracing.TextureLod` | int32 | unset | [0, 1] | Use texture LOD during ray tracing (saves bandwidth). |
| `r.RayTracing.ForceAllRayTracingEffects` | int32 | unset | [0, 1] | Force ray tracing for all RT-capable effects. |
| `r.RayTracing.AllowInline` | int32 | unset | [0, 1] | Allow inline ray tracing (RT 1.1). |
| `r.RayTracing.AllowPipeline` | int32 | unset | [0, 1] | Allow pipeline ray tracing (traditional DXR). |
| `r.RayTracing.AsyncBuild` | int32 | unset | [0, 1] | Build TLAS asynchronously. |

**Implementation Notes**:
- Ray tracing requires DX12 with DXR support or equivalent (Vulkan KHR_raytracing)
- Inline RTM 1.1 is lower latency, pipeline is more flexible
- TLAS (top-level) rebuild must be deferred until GPU finish (GPU sync point)

---

## 14. LOD & Culling

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.LODFadeTime` | float | unset | [0, ∞) | Duration (seconds) of LOD transition fade. |
| `r.CullInstances` | int32 | unset | [0, 1] | Enable instance culling. |
| `r.Shadow.OcclusionCullCascadedShadowMaps` | int32 | unset | [0, 1] | Use occlusion culling for CSM shadow rendering. |
| `r.HZBOcclusion` | int32 | unset | [0, 1] | Enable hierarchical Z-buffer occlusion culling. |

**Implementation Notes**:
- LOD transitions smooth visual pop (prevents harsh grid changes)
- Occlusion culling skips rendering occluded objects (rasterized HZB pyramid)
- CSM culling reduces shadow caster count per cascade

---

## 15. Hair Strands

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.HairStrands.VelocityThreshold` | float | 3.0 | [0, ∞) | Pixel velocity threshold (px) for forced responsive AA on hair. |
| `r.HairStrands.WriteGBufferData` | int32 | 1 | [0, 2] | Write hair data to GBuffer. 0=no, 1=dummy, 2=full. |
| `r.HairStrands.HoldoutMode` | int32 | unset | [0, 2] | Holdout rendering mode (0=normal, 1=holdout, 2=additive). |
| `r.HairStrands.Shadow.CullPerObjectShadowCaster` | int32 | 1 | [0, 1] | CPU cull per-object shadow casters for hair. |
| `r.HairStrands.DeepShadow.Resolution` | int32 | 2048 | [512, 4096] | Deep shadow map resolution for hair. |
| `r.HairStrands.DeepShadow.MinResolution` | int32 | 64 | [32, 256] | Minimum resolution for deep shadow atlas tiles. |
| `r.HairStrands.DeepShadow.DebugDOMIndex` | int32 | unset | [0, ∞) | Debug visualization index for deep opacity map. |
| `r.HairStrands.DeepShadow.DebugDOMScale` | float | unset | [0, ∞) | Scale factor for DOM debug visualization. |
| `r.HairStrands.DeepShadow.InjectVoxelDepth` | int32 | unset | [0, 1] | Inject voxel content into deep shadow (experimental). |

### Hair Lighting

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.HairStrands.ScatterSceneLighting` | int32 | unset | [0, 1] | Scatter scene color lighting into hair (short hair). |
| `r.HairStrands.SkyLighting` | int32 | unset | [0, 1] | Enable sky lighting on hair. |
| `r.HairStrands.SkyAO` | int32 | unset | [0, 1] | Enable sky AO on hair. |
| `r.HairStrands.SkyLighting.ConeAngle` | float | unset | [0, 180] | Cone angle (degrees) for sky lighting trace. |
| `r.HairStrands.SkyLighting.SampleCount` | int32 | 16 | [1, 64] | Samples for sky lighting integration. |
| `r.HairStrands.SkyAO.SampleCount` | int32 | 16 | [1, 64] | Samples for sky AO integration. |
| `r.HairStrands.SkyLighting.TransmissionDensityScale` | float | unset | [0, ∞) | Density scale for transmitted sky light. |
| `r.HairStrands.SkyLighting.UseViewHairCount` | int32 | unset | [0, 1] | Use view hair count texture for background transmission. |
| `r.HairStrands.SkyAO.DistanceThreshold` | float | unset | [0, ∞) | Max distance (cm) for occlusion search. |
| `r.HairStrands.SkyLighting.DistanceThreshold` | float | unset | [0, ∞) | Max distance for sky lighting occlusion search. |
| `r.HairStrands.SkyLighting.IntegrationType` | int32 | unset | [0, 1] | Integration type. 0=adhoc, 1=uniform. |
| `r.HairStrands.SkyLighting.DebugSample` | int32 | unset | [0, 1] | Debug visualization of sample points. |
| `r.HairStrands.SkyLighting.ScreenTraceOcclusion` | int32 | unset | [0, 1] | Screen-space ray trace for occlusion. |

### Hair LUT

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.HairStrands.HairLUT.IncidentAngleCount` | int32 | unset | [2, 32] | LUT slices for incident angle axis. |
| `r.HairStrands.HairLUT.RoughnessCount` | int32 | unset | [2, 32] | LUT slices for roughness axis. |
| `r.HairStrands.HairLUT.AbsorptionCount` | int32 | unset | [2, 32] | LUT slices for absorption (base color) axis. |
| `r.HairStrands.HairLUT.SampleCountScale` | float | unset | [0.1, 10] | Multiplier for LUT sample count. |

**Implementation Notes**:
- Hair uses specialized BSDF (R, TT, TRT paths)
- Deep shadow map stores opacity at multiple distances (multisampling)
- Hair count texture: opacity per-pixel (for soft hair interaction)
- LUT precomputation makes real-time evaluation feasible

---

## 16. Nanite (Virtualized Geometry)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Nanite.ViewMeshLODBiasEnable` | int32 | unset | [0, 1] | Enable LOD bias for Nanite view meshes. |
| `r.Nanite.ViewMeshLODBiasOffset` | float | unset | [0, 10] | LOD bias offset for view mesh selection. Positive = coarser LODs. |
| `r.Nanite.ViewMeshLODBiasMin` | float | unset | [0, 10] | Minimum LOD bias (coarsest selectable LOD). |

**Implementation Notes**:
- Nanite uses hardware-accelerated geometry streaming
- LOD bias determines target triangle count per pixel
- Offset applies additional bias (tuning for perf)

---

## 17. Virtual Shadow Maps

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Shadow.Virtual.OnePassProjection.SkipScreenShadowMask` | int32 | unset | [0, 1] | Skip screen shadow mask in one-pass projection. |
| `r.Shadow.Virtual.TextureResolution` | int32 | unset | [1024, 16384] | Virtual shadow map page resolution. |
| `r.Shadow.Virtual.CacheResolution` | int32 | unset | [1024, 8192] | Virtual shadow map cache resolution. |
| `r.Shadow.Virtual.MaxPhysicalPages` | int32 | unset | [1, 16384] | Maximum physical pages in cache. |

**Implementation Notes**:
- Virtual shadow maps cache pages on demand (streaming)
- One-pass projection: caster and receiver in single pass
- Page resolution trade-off: smaller = more pages needed, larger = less quality

---

## 18. Async Compute & Performance

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.EnableAsyncComputeTranslucencyLightingVolumeClear` | int32 | unset | [0, 1] | Use async compute for translucency volume clear. |
| `r.CaptureNextRenderFrame` | int32 | unset | [0, 1] | Capture next frame for debugging. |
| `r.StallInitViews` | float | unset | [0, 100] | Stall duration (ms) for init views (debug). |

**Implementation Notes**:
- Async compute queue independent of graphics queue
- Volume clears can run async before main lighting passes
- Frame capture used for PIX/RenderDoc debugging

---

## 19. Translucency & Refraction

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.TranslucencyVelocity` | int32 | unset | [0, 1] | Compute motion vectors for translucent surfaces. |
| `r.ForceBlackVelocityBuffer` | int32 | unset | [0, 1] | Force zero velocity (debug). |
| `r.Refraction.Blur.TemporalAA` | int32 | unset | [0, 1] | Apply TAA to refraction blur. |

---

## 20. Eye Adaptation (Auto Exposure)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.EyeAdaptation.Method` | int32 | unset | [0, 3] | Auto exposure method. 0=off, 1=basic, 2=histogram, 3=multi-threaded. |
| `r.EyeAdaptation.MetersToWhitePoint` | float | unset | [0.001, 10] | EV scaling for white point metering. |
| `r.EyeAdaptation.UsePreexposure` | int32 | unset | [0, 1] | Preexpose to target exposure. |

**Implementation Notes**:
- Auto exposure adapts to scene luminance over time
- Histogram method: per-frame luminance distribution
- White point metering: calibrates for different lighting conditions

---

## Summary & Best Practices for UzEngine

### Required Implementation Checklist

Every shader parameter that affects visual output should have a corresponding CVar:

1. **Tone Mapping**: sharpness, gamma
2. **Bloom**: threshold, intensity, quality, temporal filtering
3. **AO**: radius, bias, intensity, sample count, temporal/spatial filtering
4. **Shadows**: depth bias, slope bias, receiver bias, transition scale, sample count
5. **TAA**: filter size, catmull-rom, frame weight, history size
6. **SSR**: quality, temporal, stencil, max roughness
7. **SSG I**: quality, ray count, minimum luminance
8. **Atmosphere**: sample counts, LUT resolutions, transmittance, multi-scattering
9. **Volumetric Fog**: density, falloff, height absorption
10. **IBL**: cube map resolution, irradiance sample count
11. **Hair**: velocity threshold, shadow resolution, sky lighting sample count
12. **Clouds**: ray march sample count, shadow map resolution, density scale

### Naming Convention

```
r.<Subsystem>.<Feature>.<Parameter>
r.<Subsystem>.<Parameter>
r.<Feature>.<Parameter>
```

Examples:
- `r.Shadow.CSMDepthBias` — directional light CSM depth bias
- `r.GTAO.FalloffEnd` — GTAO kernel radius
- `r.VolumetricCloud.ViewRaySampleMaxCount` — main view ray count
- `r.SkyAtmosphere.SampleCountMin` — atmosphere minimum samples

### Default Value Selection

**MANDATORY**: Every CVar default must come from:
1. Published academic paper (SIGGRAPH, GDC, etc.)
2. Industry standard (Bruneton atmosphere = 32 samples)
3. UE5 source (this reference document)
4. Never invented or "tuned to look good"

### Type Selection

- `int32`: Count, quality level, boolean flag
- `float`: Continuous parameter (bias, radius, weight)
- `bool`: Enable/disable feature

### Range Selection

- **Min bound**: Physical minimum (e.g., 1 sample, not 0)
- **Max bound**: Hardware or quality limit (e.g., 256 samples = cache miss territory)
- **Open-ended ranges** (e.g., `[0, ∞)`) for unbounded parameters

---

## Implementation References

All CVars extracted from:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\*.cpp`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\*.cpp`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\Shadows\*.cpp`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\VirtualShadowMaps\*.cpp`

Each CVar is registered using `TAutoConsoleVariable<T>` or `FAutoConsoleVariableRef`, ensuring runtime configurability without recompilation.

