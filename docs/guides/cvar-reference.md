<!-- doc-meta
title: CVar Reference — Complete List of Console Variables
status: active
created: 2026-03-21
updated: 2026-03-21
-->

# CVar Reference — Complete List of Console Variables

## Usage

```bash
# CLI: --cvar name=value (can be repeated)
demo.exe --cvar r.Bloom.Threshold=5.0 --cvar r.GTAO.Intensity=2.0 --cvar r.TAA=0

# C++ (runtime):
uz::CVarRegistry::Get().Set("r.Bloom.Threshold", 5.0f);
float val = uz::CVarRegistry::Get().GetFloat("r.Bloom.Threshold");
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
| `r.Bloom.Threshold` | float | -1.0 | [-1, 100] | Luminance threshold (-1=none) |
| `r.Bloom.Intensity` | float | 0.675 | [0, 10] | Bloom contribution multiplier |
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

## Auto Exposure Parameters

Registered in `auto_exposure.cpp`.

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.AutoExposure` | bool | 1 | [0, 1] | Auto exposure toggle |
| `r.AutoExposure.Bias` | float | 0.0 | [-5, 5] | Exposure EV bias |
| `r.AutoExposure.MinBrightness` | float | 0.03 | [0.001, 100] | Min avg luminance |
| `r.AutoExposure.MaxBrightness` | float | 8.0 | [0.001, 100] | Max avg luminance |

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
| `r.DynamicResolution.TargetFPS` | float | 60.0 | [15, 240] | Target FPS for dynamic res |
