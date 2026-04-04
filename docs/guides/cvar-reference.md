<!-- doc-meta
title: CVar Reference — Complete List of Console Variables
status: active
created: 2026-03-21
updated: 2026-03-24
-->

# CVar Reference — Complete List of Console Variables

## Usage

```bash
# CLI: --cvar name=value (can be repeated)
demo.exe --cvar r.Bloom.Threshold=5.0 --cvar r.GTAO.Intensity=2.0 --cvar r.TAA=0

# C++ (runtime):
uz::CVarRegistry::Get().Set("r.Bloom.Threshold", 5.0f);
float val = uz::CVarRegistry::Get().GetFloat("r.Bloom.Threshold");

# Lua (runtime):
uz.cvar.set("r.Bloom.Threshold", 5.0)
local val = uz.cvar.get("r.Bloom.Threshold")
uz.cvar.reset("r.Bloom.Threshold")           -- reset to default
uz.cvar.reset_all()                           -- reset all CVars
local info = uz.cvar.info("r.Bloom.Threshold") -- {name, description, value, default, min, max}
local all  = uz.cvar.list("r.Bloom")           -- list CVars by prefix
```

## Adding New CVars

Each subsystem registers its own CVars using `UZ_CVAR_*` macros at file scope in `.cpp` files:

```cpp
// In bloom_pass.cpp (file scope, after includes)
#include "cvar.h"
static auto* cv_BloomThreshold = UZ_CVAR_FLOAT("r.Bloom.Threshold", -1.f,
    "Bloom luminance threshold", -1.f, 100.f);

// In the pass Execute():
settings.threshold = cv_BloomThreshold->value;
```

**Never** declare CVars in `.h` files (causes duplicate registration across TUs).

---

## Render Pass Toggles

Registered in `scene_lifecycle.cpp`. Control pass enable/disable via `ApplyCVars()`.

| CVar | Type | Default | Description |
|------|------|---------|-------------|
| `r.TAA` | bool | 1 | Temporal anti-aliasing |
| `r.SSR` | bool | 1 | Screen-space reflections |
| `r.SSGI` | bool | 1 | Screen-space global illumination |
| `r.GTAO` | bool | 1 | Ground truth ambient occlusion |
| `r.Bloom` | bool | 1 | Bloom post-process |
| `r.MotionBlur` | bool | 1 | Motion blur |
| `r.DOF` | bool | 0 | Depth of field |
| `r.CAS` | bool | 1 | Contrast adaptive sharpening |
| `r.LensFlare` | bool | 0 | Lens flare |
| `r.Fog` | bool | 1 | Height fog |
| `r.Clouds` | bool | 1 | Volumetric clouds |
| `r.ContactShadows` | bool | 1 | Contact shadows |
| `r.RadianceCache` | bool | 0 | SDF radiance cache GI |
| `r.DepthPrepass` | bool | 1 | Depth prepass before GBuffer |
| `r.AerialPerspective` | bool | 1 | Aerial perspective |

## Bloom Parameters

Registered in `bloom_pass.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Bloom.Threshold` | float | 0.8 | [-1, 100] | Luminance threshold (-1=none) |
| `r.Bloom.Intensity` | float | 0.6 | [0, 10] | Bloom contribution multiplier |
| `r.Bloom.Radius` | float | 1.0 | [0.1, 4] | Upsample filter radius scale |
| `r.Bloom.SizeScale` | float | 4.0 | [0.1, 64] | Global bloom size scale |

## GTAO Parameters

Registered in `gtao_pass.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.GTAO.Radius` | float | 200.0 | [1, 10000] | AO search radius (cm) |
| `r.GTAO.Intensity` | float | 1.0 | [0, 5] | AO strength multiplier |
| `r.GTAO.Power` | float | 1.0 | [0.1, 10] | AO power curve exponent |
| `r.GTAO.ThicknessBlend` | float | 0.5 | [0, 1] | Thickness heuristic (0=thin, 1=thick) |
| `r.GTAO.NumAngles` | int | 2 | [1, 16] | Direction search count |
| `r.GTAO.NumTaps` | int | 8 | [4, 16] | Steps per direction |
| `r.GTAO.MaxScreenRadius` | float | 256.0 | [64, 512] | Max screen-space search radius (px) |
| `r.GTAO.FadeStart` | float | 50000 | [100, 1e6] | Distance fade start (cm) |
| `r.GTAO.FadeRange` | float | 50000 | [100, 1e6] | Distance fade range (cm) |

## Auto Exposure Parameters

Registered in `auto_exposure.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.AutoExposure` | bool | 1 | [0, 1] | Auto exposure toggle |
| `r.AutoExposure.Bias` | float | 0.0 | [-5, 5] | Exposure EV bias |
| `r.AutoExposure.MinBrightness` | float | 0.1 | [0.001, 100] | Min avg luminance |
| `r.AutoExposure.MaxBrightness` | float | 4.0 | [0.001, 100] | Max avg luminance |
| `r.AutoExposure.MidGrayTarget` | float | 0.18 | [0.1, 0.5] | 18% gray card target |
| `r.AutoExposure.LogLuminanceMin` | float | -10.0 | [-20, 0] | Histogram log2 luminance min |
| `r.AutoExposure.LogLuminanceMax` | float | 20.0 | [0, 40] | Histogram log2 luminance max |
| `r.AutoExposure.SpeedUp` | float | 3.0 | [0.1, 20] | Bright adaptation speed (EV/s) |
| `r.AutoExposure.SpeedDown` | float | 1.0 | [0.1, 20] | Dark adaptation speed (EV/s) |

## SSGI Parameters

Registered in `ssgi_renderer.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.SSGI.Quality` | int | 2 | [1, 4] | Quality: 1=4rays, 2=8, 3=16, 4=32 |
| `r.SSGI.Intensity` | float | 1.0 | [0, 10] | Indirect diffuse intensity |

## Lens Flare Parameters

Registered in `lens_flare_pass.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.LensFlare.Intensity` | float | 1.0 | [0, 10] | Overall flare intensity |
| `r.LensFlare.GhostIntensity` | float | 0.5 | [0, 5] | Ghost contribution |
| `r.LensFlare.HaloRadius` | float | 0.6 | [0, 2] | Halo ring radius (UV) |
| `r.LensFlare.ChromaticShift` | float | 0.02 | [0, 0.1] | Chromatic aberration offset |

## Sky/IBL Parameters

Registered in `frame_setup.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.SkyLight.Intensity` | float | 1.0 | [0, 20] | IBL ambient multiplier |
| `r.SkyLight.MinAmbient` | float | 0.25 | [0, 1] | Minimum ambient floor |

## Tonemap Parameters

Registered in `frame_setup.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Tonemap.Slope` | float | 0.88 | [0, 2] | ACES film curve slope |
| `r.Tonemap.Toe` | float | 0.55 | [0, 1] | ACES film curve toe |
| `r.Tonemap.Shoulder` | float | 0.26 | [0, 1] | ACES film curve shoulder |
| `r.Tonemap.BlackClip` | float | 0.0 | [0, 1] | ACES black clip |
| `r.Tonemap.WhiteClip` | float | 0.04 | [0, 1] | ACES white clip |

## Film/Post Effects

Registered in `frame_setup.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.FilmGrain` | float | 0.0 | [0, 1] | Film grain intensity |
| `r.Vignette` | float | 0.4 | [0, 2] | Vignette intensity |
| `r.ChromaticAberration` | float | 0.0 | [0, 1] | Chromatic aberration |
| `r.Exposure` | float | 1.0 | [0.01, 100] | Manual exposure multiplier |
| `r.MipBias` | float | 0.0 | [-4, 4] | Global mipmap LOD bias |

## Shadow Parameters

Registered in `frame_setup.cpp` / `scene_lifecycle.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.ContactShadow.Length` | float | 0.02 | [0, 0.2] | Contact shadow ray length (screen %) |
| `r.ContactShadow.Steps` | int | 12 | [4, 24] | Contact shadow ray march steps |
| `r.ContactShadow.Thickness` | float | 0.01 | [0.0001, 0.1] | Contact shadow geometry thickness |
| `r.Shadow.AtlasResolution` | int | 2048 | [512, 8192] | Main shadow atlas resolution |
| `r.Shadow.LocalAtlasSize` | int | 4096 | [1024, 8192] | Local light shadow atlas resolution |
| `r.Shadow.LocalTileSize` | int | 1024 | [256, 2048] | Per-light shadow tile size |
| `r.Shadow.PCSS.SearchSamples` | int | 16 | [8, 32] | PCSS blocker search sample count |
| `r.Shadow.PCSS.Samples` | int | 32 | [16, 64] | PCSS PCF filter sample count |
| `r.Shadow.PCSS.MaxSharpness` | float | 4.0 | [1, 8] | PCSS post-PCF sharpening limit |
| `r.ShadowDebug` | int | 0 | [0, 8] | Shadow debug grid mode |

## GI Parameters

Registered in `frame_setup.cpp` / `scene_lifecycle.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.GI.Intensity` | float | 1.0 | [0, 10] | GI intensity multiplier |
| `r.GI.RadianceCache.GridExtent` | float | 5000.0 | [100, 50000] | Probe grid half-extent (cm) |

## Motion Blur Parameters

Registered in `frame_lighting.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.MotionBlur.Amount` | float | 0.5 | [0, 5] | Motion blur intensity |
| `r.MotionBlur.Max` | float | 5.0 | [0, 50] | Max blur radius (% viewport) |

## CAS Parameters

Registered in `frame_lighting.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.CAS.Sharpness` | float | 0.15 | [0, 1] | Sharpening strength |

## LOD Parameters

Registered in `frame_lighting.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.LOD.Bias` | float | 1.0 | [0.1, 10] | Global LOD bias multiplier |

## DOF Parameters

Registered in `scene_lifecycle.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.DOF.FocusDistance` | float | 0.0 | [0, 100000] | Focus distance (cm) |
| `r.DOF.Aperture` | float | 4.0 | [0.5, 32] | F-stop number |
| `r.DOF.FocalRegion` | float | 0.0 | [0, 10000] | In-focus zone width (cm) |
| `r.DOF.NearTransition` | float | 300.0 | [0, 10000] | Near transition ramp (cm) |
| `r.DOF.FarTransition` | float | 500.0 | [0, 50000] | Far transition ramp (cm) |

## Resolution & Upscaling

Registered in `scene_lifecycle.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.ScreenPercentage` | float | 100.0 | [10, 200] | Render scale percentage |
| `r.AntiAliasingMethod` | int | 2 | [0, 5] | AA method: 0=None, 1=TAA, 2=TSR |
| `r.DynamicResolution.Enabled` | bool | 0 | [0, 1] | GPU-bound adaptive resolution |

## FSR 1.0 Parameters

Registered in `fsr_pass.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.FSR` | bool | 0 | [0, 1] | FSR 1.0 spatial upscaling toggle |
| `r.FSR.Sharpness` | float | 0.2 | [0, 2] | RCAS sharpness (0=max sharp, 2=soft) |

## SSR Parameters

Registered in `frame_lighting.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.SSR.Intensity` | float | 1.0 | [0, 2] | SSR intensity multiplier |
| `r.SSR.MaxRoughness` | float | 0.8 | [0, 1] | Max roughness for SSR |
| `r.SSR.NumSteps` | int | 16 | [8, 32] | SSR ray march step count |
| `r.SSR.StartMipLevel` | float | 1.0 | [0, 3] | SSR initial HZB mip level |
| `r.SSR.SlopeToleranceScale` | float | 4.0 | [1, 16] | SSR slope tolerance scale |

## TAA Parameters

Registered in `frame_setup.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.TAA.CurrentFrameWeight` | float | 0.04 | [0.01, 0.2] | Current frame blend weight (lower=more ghosting, higher=more flicker) |
| `r.TAA.JitterSequenceLength` | int | 8 | [1, 16] | Halton jitter sequence length |

## IBL Parameters

Registered in `ibl_baker.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.IBL.BRDFLutResolution` | int | 512 | [128, 1024] | BRDF integration LUT resolution |
| `r.IBL.PrefilteredSize` | int | 256 | [64, 512] | Prefiltered specular cubemap base size |
| `r.IBL.PrefilteredMipLevels` | int | 6 | [1, 10] | Prefiltered specular mip count |

## Atmosphere Parameters

Registered in `sky_cubemap_capture_pass.cpp`, `transmittance_lut_pass.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Atmosphere.SkyCubemapResolution` | int | 64 | [32, 256] | Sky cubemap face resolution |
| `r.Atmosphere.TransmittanceLutWidth` | int | 256 | [64, 512] | Transmittance LUT width |
| `r.Atmosphere.TransmittanceLutHeight` | int | 64 | [32, 256] | Transmittance LUT height |

## PBR / Material Parameters

Registered in `frame_setup.cpp`, `frame_postprocess.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.PBR.MinRoughness` | float | 0.045 | [0.01, 0.2] | Minimum roughness clamp |
| `r.Material.POM.HeightRatio` | float | 0.05 | [0.01, 0.2] | POM perceived height ratio |
| `r.Material.POM.ShadowSteps` | int | 6 | [4, 16] | POM self-shadow ray steps |
| `r.Material.POM.RefinementSteps` | int | 1 | [1, 3] | POM binary search refinement |

## Tonemap Color Parameters

Registered in `frame_postprocess.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Tonemap.ChromaAbScale` | float | 0.007 | [0, 0.02] | Chromatic aberration intensity scale |
| `r.FXAA.EdgeThreshold` | float | 0.166 | [0.063, 0.333] | FXAA edge detection threshold |
| `r.ColorSaturation` | float | 1.1 | [0, 3] | Color saturation multiplier |
| `r.ColorContrast` | float | 1.0 | [0.1, 3] | Color contrast multiplier |

## Motion Blur & DOF Additional

Registered in `frame_postprocess.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.MotionBlur.MaxRadius` | float | 5.0 | [0.1, 20] | Max motion blur radius (% viewport) |
| `r.MotionBlur.TargetFPS` | int | 30 | [24, 120] | Target FPS for velocity scaling |
| `r.DOF.RingCount` | int | 5 | [3, 8] | DOF bokeh concentric ring count |
| `r.DOF.CoCSkipThreshold` | float | 0.5 | [0.1, 3] | DOF CoC radius skip threshold (px) |

## Scene Parameters

Registered in `scene_assets.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Scene.MaxTextureUploadsPerFrame` | int | 8 | [1, 32] | Max async texture uploads per frame |

## System Parameters

Registered in `application.cpp`, `job_system.cpp`, `vfs.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `sys.MaxDeltaTime` | float | 0.25 | [0.016, 1.0] | Max delta time clamp (spiral-of-death prevention) |
| `sys.FixedDeltaTime` | float | 0.01667 | [0.002, 0.1] | Fixed timestep for physics |
| `rhi.FramesInFlight` | int | 3 | [1, 5] | GPU frames in flight (init-time only) |
| `rhi.VSync` | bool | 0 | [0, 1] | Vertical sync |
| `job.MaxWorkerCount` | int | 0 | [0, 64] | Max job worker threads (0=auto) |
| `asset.PakCacheMB` | int | 256 | [64, 2048] | PakVFS LRU cache size in MB |
| `camera.MoveSpeed` | float | 1000 | [100, 10000] | FPS camera movement speed (cm/s) |
| `r.DynamicResolution.TargetFPS` | float | 60.0 | [15, 240] | Target FPS for dynamic res |

## Mesh Import

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Mesh.NormalImportMode` | int | 1 | [0, 2] | 0=Import (keep source), 1=Recompute (smooth), 2=Auto (recompute if missing) |
| `r.Mesh.SmoothingAngle` | float | 60.0 | [0, 180] | Hard edge angle threshold (degrees) |
| `r.Mesh.TangentMode` | int | 0 | [0, 2] | 0=MikkTSpace, 1=Lengyel (fallback), 2=Import (skip) |
| `r.Mesh.PackedNormals` | bool | false | — | Export meshes with int8x4 packed normals/tangents (32B vs 48B vertex) |

## Render Quality Tuning Log (phase2a-2.50.0)

Changes made during the render quality consensus fixes task, with rationale.

| CVar | Before | After | Rationale |
|------|--------|-------|-----------|
| `r.GTAO.Power` | 0.3 | 1.0 | Jimenez 2016 GTAO reference: AmbientOcclusionPower = 2.0 * 0.5 = 1.0. Previous 0.3 was too weak (pow(0.5, 0.3) = 0.81 vs correct pow(0.5, 1.0) = 0.5). |
| `r.SSR.MaxRoughness` | 0.4 | 0.8 | UE5 default r.SSR.MaxRoughness = 0.8. Previous 0.4 excluded wood, stone, rough metal from reflections. |
| `r.AutoExposure.MinBrightness` | 0.3 | 0.1 | Previous 2x dynamic range (0.3-0.6) caused rapid oscillation when bright metallic specular shifted avg luminance. Widened to 40x range. |
| `r.AutoExposure.MaxBrightness` | 0.6 | 4.0 | Paired with MinBrightness change — 40x dynamic range prevents exposure hunting on high-contrast scenes. |
| `r.AutoExposure.SpeedUp` | 1.5 | 3.0 | UE5 reference adaptation speed. Faster bright adaptation prevents visible exposure hunting. |
| `r.AutoExposure.SpeedDown` | 0.7 | 1.0 | UE5 reference adaptation speed. Paired with SpeedUp for balanced adaptation. |
| `r.SkyLight.MinAmbient` | 0.15 | 0.25 | Shadow regions were near-black after GTAO power increase. Floor of 0.25 provides visible ambient fill. |

## Shader CVar Audit (phase2_shader-cvar-audit) — 47 CVars

Replaced all hardcoded shader constants with runtime-configurable CVars.
Every CVar default matches the previously hardcoded value — zero visual regression.

### SSGI (10 CVars via root constants)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.SSGI.RayCount` | int | 8 | [1, 32] | Rays per pixel |
| `r.SSGI.MaxMarchSteps` | int | 32 | [8, 128] | Max linear march steps per ray |
| `r.SSGI.StepSize` | float | 0.02 | [0.005, 0.1] | UV-space step size per step |
| `r.SSGI.Thickness` | float | 0.5 | [0.01, 2.0] | Depth comparison thickness (cm) |
| `r.SSGI.TemporalAlpha` | float | 0.05 | [0.01, 0.5] | Temporal blend (0=full history, 1=no history) |
| `r.SSGI.SpatialSigmaDepth` | float | 1.0 | [0.1, 10] | Spatial denoise depth edge-stopping |
| `r.SSGI.SpatialSigmaNormal` | float | 128.0 | [8, 512] | Spatial denoise normal edge-stopping |
| `r.SSGI.SpatialSigmaLuminance` | float | 4.0 | [0.5, 16] | Spatial denoise luminance edge-stopping |
| `r.SSGI.UpsampleDepthSigma` | float | 0.5 | [0.1, 2.0] | Bilateral upsample depth weight |
| `r.SSGI.UpsampleNormalSigma` | float | 32.0 | [4, 128] | Bilateral upsample normal cosine power |

### Shadow (6 CVars via PerFrameCB)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Shadow.PCFPenumbraRadius` | float | 2.0 | [0.5, 8.0] | PCF texel radius |
| `r.Shadow.PCSSMinPenumbra` | float | 0.5 | [0.1, 4.0] | PCSS minimum penumbra clamp |
| `r.Shadow.PCSSMaxPenumbra` | float | 16.0 | [4.0, 64.0] | PCSS maximum penumbra clamp |
| `r.Shadow.ESMExponentConstant` | float | 80.0 | [10, 200] | ESM exponential warp constant |
| `r.Shadow.ContactShadowSteps` | int | 16 | [4, 32] | Contact shadow ray march steps |
| `r.Shadow.ContactShadowThickness` | float | 0.005 | [0.001, 0.05] | Contact shadow depth thickness |

### TAA (5 CVars via TaaCB)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.TAA.VarianceClipGamma` | float | 1.25 | [0.5, 3.0] | Variance clip (wider=ghosting, tighter=flicker) |
| `r.TAA.VelocityThreshold` | float | 40.0 | [5, 100] | Velocity lerp threshold (pixels) |
| `r.TAA.AntiFlickerMax` | float | 0.3 | [0, 1] | Max anti-flicker contribution |
| `r.TAA.SpatialWeightMultiplier` | float | 2.29 | [1, 5] | Gaussian spatial weight exponent |
| `r.TAA.ExposureScale` | float | 1.0 | [0.1, 10] | HDR exposure scale for TAA weighting |

### SVGF Denoiser (4 CVars via root constants)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.SVGF.SigmaDepth` | float | 1.0 | [0.1, 10] | Edge-stopping depth sigma |
| `r.SVGF.SigmaNormal` | float | 128.0 | [8, 512] | Edge-stopping normal sigma |
| `r.SVGF.SigmaLuminance` | float | 4.0 | [0.5, 16] | Edge-stopping luminance sigma |
| `r.SVGF.MomentsSigmaNormal` | float | 32.0 | [4, 128] | Moments filter normal sigma |

### Bloom / Exposure (6 CVars via PerFrameCB)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Bloom.Knee` | float | 0.5 | [0, 1] | Soft threshold knee multiplier |
| `r.Exposure.KeyValue` | float | 0.18 | [0.01, 1] | Reinhard mid-tone key value |
| `r.Exposure.SpeedUp` | float | 3.0 | [0.1, 20] | Auto-exposure adaptation speed up |
| `r.Exposure.SpeedDown` | float | 1.0 | [0.1, 20] | Auto-exposure adaptation speed down |
| `r.Exposure.LowPercent` | float | 10.0 | [1, 50] | Histogram low percentile |
| `r.Exposure.HighPercent` | float | 90.0 | [50, 99] | Histogram high percentile |

### Tonemap (1 CVar via root constant)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.TonemapOperator` | int | 0 | [0, 4] | 0=Hill ACES, 1=Narkowicz, 2=Reinhard, 3=Hable, 4=Linear |

### Cloud FBM (8 CVars via CloudCB expansion)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Cloud.NoiseXYScale` | float | 0.12 | [0.01, 1] | Noise XY coordinate scale |
| `r.Cloud.NoiseZScale` | float | 3.0 | [0.5, 10] | Noise Z coordinate scale |
| `r.Cloud.ShapeOctaves` | int | 4 | [1, 6] | FBM octave count |
| `r.Cloud.FBMDecay` | float | 0.5 | [0.3, 0.8] | Amplitude decay per octave |
| `r.Cloud.FBMLacunarity` | float | 2.0 | [1.5, 4] | Frequency multiplier per octave |
| `r.Cloud.DetailErosion` | float | 0.35 | [0, 1] | Detail noise erosion strength |
| `r.Cloud.DensityScale` | float | 25.0 | [1, 100] | Density normalization scale |
| `r.Cloud.AerialPerspectiveDensity` | float | 0.04 | [0, 0.2] | Aerial perspective fog density |

### Material (2 CVars via PerFrameCB)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.Material.DielectricF0` | float | 0.04 | [0.01, 0.08] | Dielectric Fresnel F0 |
| `r.Material.MinGGXAlpha` | float | 0.0064 | [0.001, 0.01] | Min GGX alpha (prevents aliasing) |

### SSR (2 CVars — fixed deprecated CVarRegistry)

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.SSR.MaxRoughness` | float | 0.35 | [0.05, 1] | Max roughness cutoff for SSR |
| `r.SSR.Intensity` | float | 1.0 | [0, 5] | SSR reflection intensity |
