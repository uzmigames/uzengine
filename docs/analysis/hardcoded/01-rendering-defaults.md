# 01 — Rendering Defaults Tuned for Sample Scene

**Severity:** HIGH
**File focus:** `runtime/engine/renderer/frame/frame_constants.cpp`, `shadows/`, `gi/`, `passes/atmosphere/`

Rendering constants (exposure, fog density, shadow atlas, tonemap params) have defaults calibrated against the `sample/` scenes (primarily Sponza). Every new project inherits these numbers and ships with visually wrong output until artists manually re-tune.

## Findings

| # | Category | File:Line | Snippet | Why Bad | Fix |
|---|----------|-----------|---------|---------|-----|
| 1.1 | Exposure calibration | [frame_constants.cpp:73-75](../../../runtime/engine/renderer/frame/frame_constants.cpp#L73-L75) | `cv_Exposure = 3.0f` with comment `// matched to indoor Sponza skylight 0.35 / sun 18` | Exposure is explicitly documented as tuned for Sponza; other projects load over/underexposed | Move to `sample/project.uzproj` as `PostProcessVolume.exposure = 3.0`. Runtime default = auto-exposure or `1.0`. |
| 1.2 | Contact shadow length | [frame_constants.cpp:40-41](../../../runtime/engine/renderer/frame/frame_constants.cpp#L40-L41) | `ContactShadowLen = 0.10f` comment `// large interiors like Sponza` | Tuned for Sponza's scale; too long for outdoor, too short for small interiors | Per-PostProcessVolume override; scale-relative (0.1% of view depth) |
| 1.3 | SSGI expected depth | [ssgi_renderer.cpp:32](../../../runtime/engine/renderer/gi/ssgi_renderer.cpp#L32) | `// hardcoded 5km depth assumption that needs separate work` | SSGI GI formula assumes 5km world scale; breaks on any other scale | Pass scene AABB extent via CB; compute expected depth from it |
| 1.4 | SSGI thickness | [ssgi_renderer.cpp:35](../../../runtime/engine/renderer/gi/ssgi_renderer.cpp#L35) | `Thickness = 50.0f // Sponza columns ~60cm` | Comment reveals Sponza bias in default | Remove Sponza reference; document unit scale expectations |
| 1.5 | Skylight intensity | [frame_constants.cpp:33-35](../../../runtime/engine/renderer/frame/frame_constants.cpp#L33-L35) | `SkyLightIntensity = 1.0f; MinAmbient = 0.25f` | Defaults match Sponza's HDR skylight baseline | Per-scene override in `.uzscene` envmap metadata |
| 1.6 | Color saturation boost | [frame_constants.cpp:45](../../../runtime/engine/renderer/frame/frame_constants.cpp#L45) | `ColorSaturation = 1.1f` | Artistic boost (1.1×) imposed on every project | Per-project default in `.uzproj`; engine default = `1.0` |
| 1.7 | ACES tonemap params | [frame_constants.cpp:47-51](../../../runtime/engine/renderer/frame/frame_constants.cpp#L47-L51) | `TonemapSlope = 0.91f, Toe = 0.45f, Shoulder = 0.22f` | Only ACES provided; no Reinhard/Uncharted presets | Add tonemap curve enum + per-curve params; expose in `.uzproj` |
| 1.8 | Shadow atlas size | [shadow_renderer.cpp:19](../../../runtime/engine/renderer/shadows/shadow_renderer.cpp#L19) | `ShadowAtlasRes = 2048; LocalShadowAtlasSize = 4096` | Tuned for Sponza light density; wastes VRAM on mobile, insufficient on large scenes | Base on scene light count or expose via `.uzproj` render config |
| 1.9 | Local shadow grid | [local_light_shadow_atlas.h:8,38](../../../runtime/engine/renderer/shadows/local_light_shadow_atlas.h#L8) | `4096×4096 atlas / 4×4 grid / 1024 tiles` | Fixed grid assumes specific light budget | Parameterize grid layout and tile count |
| 1.10 | Dielectric F0 | [frame_constants.cpp:67](../../../runtime/engine/renderer/frame/frame_constants.cpp#L67) | `MaterialDielectricF0 = 0.04f` | Physics-accurate but hardcoded; stylized games need override | Per-material / per-project override |
| 1.11 | GGX min alpha | [frame_constants.cpp:68](../../../runtime/engine/renderer/frame/frame_constants.cpp#L68) | `MaterialMinGGXAlpha = 0.0064f` | Fixed anti-aliasing floor may not suit all materials | Per-material configurable |
| 1.12 | Cloud shape runtime path | [cloud_pass.cpp:358](../../../runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp#L358) | `// VFS asset from runtime/content/atmosphere/shape_detail.bin` | Cloud shape loaded from hardcoded path; engine depends on sample content | Load via VFS with `.uzproj` override; ship as engine default asset under `engine/content/` |

## Proposed Refactor

### `.uzproj` additions
```json
{
  "rendering": {
    "postProcessVolume": {
      "exposure": 3.0,
      "saturation": 1.1,
      "filmGrain": 0.0,
      "contactShadowLength": 0.10,
      "tonemap": {
        "curve": "aces",
        "slope": 0.91,
        "toe": 0.45,
        "shoulder": 0.22
      }
    },
    "skylight": {
      "intensity": 1.0,
      "minAmbient": 0.25
    },
    "shadow": {
      "atlasResolution": 2048,
      "localAtlasSize": 4096,
      "localGridSize": 4
    },
    "material": {
      "dielectricF0": 0.04,
      "minGGXAlpha": 0.0064
    }
  }
}
```

### Runtime change
`FrameConstants` reads from `ProjectConfig::Rendering()` at init; CVars become per-scene overrides only, not engine defaults.

## Scene-Specific Comments to Scrub

These don't affect runtime behavior but signal that the renderer was developed against a single scene:

- [shadow_cascade.cpp:305](../../../runtime/engine/renderer/shadows/shadow_cascade.cpp#L305) — `// (Sponza, 2026-04-10). Rotating the camera moved the sphere`
- [shadow_pcss.hlsli:87](../../../runtime/shaders/runtime/lighting/shadow_pcss.hlsli#L87) — `/// (Sponza bug 2026-04-10).`

Generalize these comments to describe the bug behavior without naming the sample scene.

## Re-audit 2026-09-08 (after the SponzaTest port, `phase0_sponza-parity-*`)

| Row | Current state | Status |
|---|---|---|
| 1.1 Exposure | `cv_Exposure` default is now **1.0** (`frame_constants.cpp:82`, applied on top of auto exposure); Sponza sets `r.Exposure 0.5` in `sample/content/scenes/sponza.uzscene`. The "Exposure 3.0 — matched to … Sponza" comment above it (`:79-81`) is stale. | Resolved in code; comment to clean |
| 1.2 Contact shadow length | `r.ContactShadow.Length = 0.10` unchanged, comment still says "large interiors like Sponza" (`:40`). | Open |
| 1.4 SSGI thickness | `r.SSGI.Thickness = 50.0` unchanged (`ssgi_renderer.cpp:35`). | Open |
| 1.5 Skylight | `r.SkyLight.Intensity = 1.0`, `r.SkyLight.MinAmbient = 0.25` unchanged; since task 03 the minimum-ambient floor is **not** applied when GI irradiance comes from the radiance cache (`deferred_lighting_ps.hlsl`, `!irradianceFromGI`). | Open, narrower |
| new | `r.IndirectAO.Exponent` default **1.0** (linear AO, reference pipeline); the pre-port Sponza calibration used 3.0 as an artistic choice (`frame_constants.cpp:85-88`). | Neutral default |
| new | `DirectionalLightComponent::lightSourceAngle` default **0.5357°** (physical sun disc; was 1.2°). Sponza sets `light_source_angle` explicitly; `RenderingParity.DirectionalLightDefaults` asserts the default. | Neutral default |
| new | The hand-tuned Sponza calibration (materials, sun, HDRI skylight) lives in `sample/reference/sponza-manual-calibration/`; shipped Sponza materials are neutral (multipliers 1.0). | Moved out of engine/content |

## Re-audit 2026-10-05 (lighting block A follow-up: defaults out of the code)

Branch `feat/sponza-gi-parity`. Every value below either moved to data (a component field, a scene
key) or to a CVar whose default reproduces the previous behaviour; the measurements are in
`docs/analysis/lighting-block-a-2026-10-05.md` (A.10).

| Item | Before | Now |
|---|---|---|
| Phantom sun | `LightFrameData` defaulted to sun direction (0.5, 0.3, 0.8) and colour (3, 2.85, 2.7): every scene without a `DirectionalLightComponent` was lit and CSM-shadowed by a light that exists in no scene file (`light_renderer.h`). | `sunColor` defaults to 0 and `hasDirectionalLight` says whether a sun exists; without one the cascade pass is skipped and `cascadeCount` is 0 (full visibility). The sky keeps a direction from its own data: `SkyAtmosphereComponent::sunDirection`, scene key `sun_direction` (Lua `uz.scene.set_atmosphere_sun_direction`). Guard: `scripts/test_no_sun_lighting.py`. |
| 1.2 Contact shadow length | `r.ContactShadow.Length = 0.10` meant "fraction of the far plane x 0.1"; the shader also carried the 1 % literal. | `r.ContactShadow.Length` is the fraction of the far plane itself, default **0.01** (the old fixed length), range [0, 0.05]; the dead `r.ContactShadow.Steps` / `r.ContactShadow.Thickness` registrations (never read; the live ones are `r.Shadow.ContactShadowSteps` / `r.Shadow.ContactShadowThickness`) were removed. |
| 1.5 Skylight | `r.SkyLight.MinAmbient = 0.25` floored every scene. | Default **0** since block A.2 (the component value). |
| Local shadow bias | Normal offset 0.5 % and the spot near plane 0.1 cm / point near plane 1.0 cm were literals in `deferred_lighting_ps.hlsl` and `frame_constants.cpp`. | `r.Shadow.Local.NormalOffset` (0.005, in `ShadowDataCB`) and `r.Shadow.Local.NearPlane` (1.0 cm for both spot and point tiles; the reference scene measures identically). |
| Sky IBL re-bake | cos(0.25 deg), 1 % and 30 passes were literals in `atmosphere_renderer.cpp`. | `r.SkyLight.RebakeAngleDeg` 0.25, `r.SkyLight.RebakeIntensityDelta` 0.01, `r.SkyLight.RebakeMinInterval` 30. |
| Auto exposure | `g_StartDistance = 1.5` (EV gap where adaptation turns linear) was a shader literal. | `r.AutoExposure.AdaptationLinearRange` 1.5, 14th root constant. |
| CSM atlas resolution | `r.Shadow.AtlasResolution` registered with 2048 and never read; the atlas used the constant 4096. | Read by `ShadowAtlas::Init`, default **4096**; `ShadowAtlas::GetResolution()` feeds the cascade setup, the cache, the inspector and the readback. |
| Local shadow atlas | `r.Shadow.LocalAtlasSize` / `r.Shadow.LocalTileSize` registered and never read; `kLocalShadow*` constants everywhere. | Read by `LocalLightShadowAtlas::Init` (tiles capped at 16 by the constant buffer); the frame uses the atlas getters. |
| Material floors | `r.Material.MinGGXAlpha` registered and never published; the shader used `max(roughness, 0.04)`; `r.Material.DielectricF0` published but the shader used the literal `0.08 * specular`. | `materialMinGGXAlpha` published (default **0.0016** = 0.04 squared, so the floor is unchanged) and `roughness = max(r, sqrt(materialMinGGXAlpha))`; `dielectricF0 = 2 * materialDielectricF0 * specular`. |
| Still in code (deliberately) | `SceneLighting` (`scene_lighting.h`) keeps the old sun defaults but nothing in the renderer reads it; `kShadowResolution` / `kLocalShadow*` remain as the CVar defaults; GI probe constants belong to the flicker programme. | - |
