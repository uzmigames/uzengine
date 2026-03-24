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
| `r.GTAO.Power` | float | 2.0 | [0.1, 10] | AO power curve exponent |
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
| `r.AutoExposure.MinBrightness` | float | 0.03 | [0.001, 100] | Min avg luminance |
| `r.AutoExposure.MaxBrightness` | float | 8.0 | [0.001, 100] | Max avg luminance |
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
| `r.SkyLight.MinAmbient` | float | 0.0 | [0, 1] | Minimum ambient floor |

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
