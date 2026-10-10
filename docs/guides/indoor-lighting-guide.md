<!-- doc-meta
title: Indoor Lighting Guide
status: stable
owner: rendering
last-updated: 2026-10-08
-->

# Indoor Lighting Guide

This guide covers how interiors get their indirect light in UzEngine as of 2026-10-08 (branch
`feat/sponza-gi-parity`) and which settings change it. It replaces the 2026-04-06 version, which
described a layered pipeline the engine never had: a light probe volume blended over the SkyLight
SH, bounce light "composited on top" of both, `r.GI.SDF` and `r.GI.Brixelizer` switches, and
`--pixel-debug` readbacks. Each statement about the engine cites the code it was checked against.
The open questions this guide refers to (N1-N4) are recorded in
`.rulebook/tasks/phase0_indoor-lighting-system/tasks.md`.

Short names used below (paths under `runtime/`):

| Short name | File |
|---|---|
| `deferred_lighting_ps.hlsl` | `shaders/runtime/lighting/deferred_lighting_ps.hlsl` |
| `frame_constants.cpp` | `engine/renderer/frame/frame_constants.cpp` |
| `frame_gi_passes.cpp` | `engine/renderer/frame/frame_gi_passes.cpp` |
| `scene_renderer_init.cpp` | `engine/renderer/scene/scene_renderer_init.cpp` |
| `radiance_cache.cpp` | `engine/renderer/gi/radiance_cache.cpp` |
| `ddgi_system.cpp` | `engine/renderer/gi/ddgi_system.cpp` |
| `ssgi_world_gather.hlsli` | `shaders/runtime/ssgi/ssgi_world_gather.hlsli` |
| `frame_atmosphere_passes.cpp` | `engine/renderer/frame/frame_atmosphere_passes.cpp` |
| `env_mis_temporal_cs.hlsl` | `shaders/runtime/lighting/env_mis_temporal_cs.hlsl` |
| `rt_manager.cpp` | `engine/renderer/raytracing/rt_manager.cpp` |
| `sky_light_component.h` | `engine/ecs/environment/sky_light_component.h` |
| `light_probe_volume_component.h` | `engine/ecs/environment/light_probe_volume_component.h` |
| `irradiance_probe_grid.cpp` | `engine/renderer/gi/irradiance_probe_grid.cpp` |

## 1. Where the indirect light comes from

### 1.1 Diffuse: one irradiance source per pixel

The deferred lighting pass takes the indirect diffuse irradiance of a pixel from exactly one
source, chosen by priority. A higher source replaces the lower ones. Nothing is added or lerped
(`deferred_lighting_ps.hlsl:739-754`).

| Priority | Source | Answers a pixel when | Code |
|---|---|---|---|
| 1 | DDGI: probe atlases written by DXR rays, read with a Chebyshev visibility test | `r.RayTracing 1` and `r.RayTracing.GI 1`, with the radiance cache running; the TLAS holds instances; the writer has completed one update; and at least one of the 8 surrounding probes survives the visibility test (weight sum above 1e-6) | `deferred_lighting_ps.hlsl:759-880`; binding `frame_constants.cpp:885-904`; writer `frame_gi_passes.cpp:329-376` |
| 2 | Radiance cache: 16³ L2 SH probes traced through the global distance field, centred on the camera | `r.RadianceCache 1` and the cache has produced a result. From then on it answers **every** pixel that DDGI did not, even with zero ("zero is a valid answer": an unlit enclosure gets no bounce light) | `deferred_lighting_ps.hlsl:882-970`; `frame_constants.cpp:796-819` |
| 3 | Light probe volume | Never: the volume is inert (section 4) | `deferred_lighting_ps.hlsl:973-1028`; `frame_constants.cpp:967-972` |
| 4 | Sky SH fallback: one global L2 SH of the sky, with no occlusion | No source above answered: every pixel when `r.RadianceCache` is 0, and before the cache's first result | `deferred_lighting_ps.hlsl:1030-1060` |

Only on the SH fallback is `minOcclusion` also a hard floor on the irradiance
(`deferred_lighting_ps.hlsl:1066-1068`), and only there does the sky light colour tint the result
(`skyTintDiffuse`, `:714`). Irradiance from a GI source is used untinted, because its writers
applied the sky light to the sky they sampled (`:1069-1072`).

**The final gather replaces the chosen source.** With `r.GI.FinalGather 1`, the SDF final gather's
per-pixel irradiance replaces whatever the priority chain chose, in every pixel
(`deferred_lighting_ps.hlsl:1148-1155`). The gather needs the radiance cache running and a global
distance field with data (`frame_constants.cpp:826-838`), and the shared gather slot prepared
(`:848-861`). Its rays read the radiance-cache probes where they hit (`ssgi_world_gather.hlsli:13`;
`docs/engine/12-rendering/33-sdf-gi-final-gather.md:110-124`). With the gather on, therefore, no
pixel reads the DDGI result (`docs/analysis/lighting-block-b-06-ddgi-dedup.md:104-105`).

The diffuse term is `(1 - metallic) x E x baseColor x (1 - dielectric scatter)`
(`deferred_lighting_ps.hlsl:1246-1247`). It is then multiplied by `ao ^ r.IndirectAO.Exponent` and
by `skyTintDiffuse` (`:1337-1338`). The legacy screen-space SSGI (`r.SSGI`) is added on top, but only
while the gather is off (`:1277-1289`). Brixelizer GI is dormant: its composite (`:1306-1324`) is
never taken (`docs/specs/rendering/gi-system.md`, Tier 4).

Sponza's project turns on the radiance cache, the final gather and the SDF reflections
(`sample/projects/sponza.uzproject:12-14`), so once the cache has produced its first result every
Sponza pixel takes its diffuse irradiance from the gather. The recalibration's region dumps show
this at Camera A, where every pixel carries path bits 0x3, gather and reflections
(`docs/analysis/lighting-sponza-recalibration.md:50`).

### 1.2 Specular

- **Prefiltered sky cubemap (split-sum).** The radiance comes from the prefiltered cubemap
  (`deferred_lighting_ps.hlsl:1100-1106`). The result is multiplied by
  `skyTintSpecular = skyLightColor x skyLightIntensity` (`:715`, `:1339`). The split-sum also
  carries a multi-scatter share, `cosineIrradiance x multiScatter` (`:1218-1219`), which uses the
  diffuse irradiance chosen in 1.1 (or the gather's).
- **`r.GI.Reflections 1`.** SDF-traced reflections replace the prefiltered radiance
  (`deferred_lighting_ps.hlsl:1156-1165`). They need the same radiance cache and distance field as
  the gather (`frame_constants.cpp:826-836`). Their sky hits are scaled by
  `skyMultiplier = skyTintSpecular` (`deferred_lighting_ps.hlsl:1146`), and the outer tint is then
  set to 1 (`:1164`).
- **`r.SSR`.** Screen-space reflections are lerped in by confidence before the BRDF (`:1173-1179`).
- **`r.EnvMIS`** (default 1, `frame_constants.cpp:82-83`). On smooth metals, the
  environment-MIS estimate replaces the split-sum specular in proportion to its coverage
  (`deferred_lighting_ps.hlsl:1347-1366`). The pass applies `skyLightColor x skyLightIntensity`
  itself (`runtime/shaders/runtime/lighting/env_mis_temporal_cs.hlsl:230-232`). It is inactive
  while SDF reflections run (`runtime/engine/renderer/frame/frame_lighting.cpp:159`).

## 2. Which SkyLight setting reaches which path

The renderer reads the first enabled `SkyLightComponent` (`frame_constants.cpp:325-407`).

| Setting (CVar) | How it is applied | Reaches | Does not reach |
|---|---|---|---|
| `intensity` (`r.SkyLight.Intensity`) | Multiplied with the CVar into `skyLightIntensity` (`frame_constants.cpp:418`) | The prefiltered specular and the split-sum (`skyTintSpecular`, `deferred_lighting_ps.hlsl:715`, `:1339`); env-MIS (`env_mis_temporal_cs.hlsl:231`); the sky of the GI probe writers (`radiance_cache.cpp:839-841`, `ddgi_system.cpp:253-255`); the sky of the final gather (`ssgi_world_gather.hlsli:22`) and of the SDF reflections (`deferred_lighting_ps.hlsl:1146`); height fog and cloud ambient (raw sky SH L0 x intensity, `frame_atmosphere_passes.cpp:123-125`, `:198-203`, `:239-242`); the translucent IBL diffuse (`runtime/shaders/runtime/gbuffer/translucent_ps.hlsl:271-276`) | **The SH-fallback diffuse** (`skyTintDiffuse = skyColor`, `deferred_lighting_ps.hlsl:714`, since `6543d8cf`, 2026-04-08), and the irradiance-cubemap fallback that replaces the SH when `IsSHZero` reads it as empty (`:1040-1044`; the test looks only at the red channel's L1 terms, `runtime/shaders/runtime/lighting/ibl.hlsli:58-61`) |
| `lightColor` | Tint | Everything `intensity` reaches except fog and clouds, plus the SH-fallback diffuse and its cubemap fallback (`deferred_lighting_ps.hlsl:714`) | Height fog and cloud ambient (`frame_atmosphere_passes.cpp:123-125` take the intensity only) |
| `indirectLightingIntensity` (`r.SkyLight.IndirectIntensity`) | Multiplied with the CVar (`frame_constants.cpp:421`; the comment at `:420` says "overrides") into `shScale`, which scales all 7 packed sky-SH coefficients (`:428-436`) | The SH-fallback diffuse (`deferred_lighting_ps.hlsl:1037`); on fallback pixels, the split-sum's multi-scatter share (`:1218-1219`); the seed of the inert light probe volume | DDGI, the radiance cache, the final gather, SDF reflections, fog and clouds (which read the unscaled SH, `frame_atmosphere_passes.cpp:123-125`), the cubemap fallback (which a value of 0 switches on, see below), translucency |
| `lowerHemisphereIsBlack`, `lowerHemisphereColor` | `false` adds a hemisphere light of radiance `lowerHemisphereColor` below the horizon to the packed SH: `+L/2` on the constant term and `-L/2` on the up term of the cube frame, both times `shScale` (`frame_constants.cpp:438-454`; correct only since `c14215ed`, 2026-10-08) | The SH-fallback diffuse; on fallback pixels, the split-sum's multi-scatter share; the seed of the inert volume | Every GI path, fog, clouds, the prefiltered and SDF-traced specular radiance |
| `minOcclusion` (`r.SkyLight.MinAmbient`) | Raised to the CVar when the CVar is larger (`frame_constants.cpp:336`, `:419`) | The AO floor `ao = max(ao, floor)` for every source (`deferred_lighting_ps.hlsl:512`), which feeds the diffuse AO and the specular occlusion; on the SH fallback, also a floor on the irradiance (`:1066-1068`) | None |

What this means in practice:

- **GI on** (the radiance cache has produced its first result): of the settings in this table,
  only `intensity`, `lightColor` and `minOcclusion` change the image. `indirectLightingIntensity`
  and the lower hemisphere have no effect, because no GI path reads the SH they modify. Open
  decision **N2** is whether they should.
- **GI off:** `intensity` does not change the diffuse ambient at all. Only
  `indirectLightingIntensity` and the colour scale it. Open decision **N1** is whether the fallback
  should take the intensity, as it did before `6543d8cf` and as the sky-lit paths outside the
  fallback do. Evidence: the lights reference authors a sky light of 0.05 "so the frame is lit by
  the local lights alone" (`scripts/lights_reference.py:5-6`, `:107`; GI off, `:53`), yet block B.7
  measured a floor ambient of 0.632 there (`docs/analysis/lighting-block-b-07-clustered-lights.md:69`).
- **`indirectLightingIntensity` 0 does not turn the GI-off ambient off.** It zeroes every packed SH
  coefficient, `IsSHZero` then reads the SH as empty, and the fallback samples the irradiance
  cubemap instead (when one is bound), scaled by neither intensity
  (`deferred_lighting_ps.hlsl:1040-1044`, `runtime/shaders/runtime/lighting/ibl.hlsli:58-61`).
  `r.SkyLight.IndirectIntensity 0` does the same, and so does any value small enough to bring the
  red channel's L1 terms below 1e-6.
- **The flag works the opposite way to its name and comment.** `lowerHemisphereIsBlack = true`
  (the default) leaves the captured sky SH unchanged: the lower hemisphere is not blackened and the
  colour is ignored. `false` adds the colour on top of the captured sky. The component header
  (`runtime/engine/ecs/environment/sky_light_component.h:98-106`) gives the UE meaning (true =
  lower hemisphere replaced by the colour). Only `frame_constants.cpp:446-454` reads the flag.
- **`intensity` does not scale "both diffuse and specular".** The header says it does
  (`sky_light_component.h:62-65`), but the SH-fallback diffuse is the exception.
- **One term reads both settings.** On SH-fallback pixels, the split-sum's multi-scatter share
  takes the SH scaled by `indirectLightingIntensity` and is then multiplied by `skyTintSpecular`,
  which carries `intensity` (`deferred_lighting_ps.hlsl:1218-1219`, `:1339`). With the radiance
  cache on and `r.GI.Reflections 0`, the same share multiplies the GI irradiance, whose sky part
  already carries the intensity, by `skyTintSpecular` again.
- **Full GTAO occlusion falls back to the mesh AO.** A GTAO value at or below 0.01 is discarded
  and the pixel keeps the G-buffer AO, usually 1 (`deferred_lighting_ps.hlsl:506`). A fully
  occluded GTAO texel therefore reads as unoccluded instead of reaching the `minOcclusion` floor.

**Sponza.** The three Sponza scenes (`sponza`, `sponza-fog` and `sponza-gi-camera-c.qa`) author
`intensity` and `indirect_lighting_intensity` at √3 (1.7320508), `min_occlusion` at 0.01, and a
lower hemisphere of (0.12, 0.11, 0.10) with `lower_hemisphere_is_black: false`
(`sample/content/scenes/sponza.uzscene:176-191`); the QA scene `sponza-gi-runtime-enable` keeps both
intensities at 1.0. Both intensities have been √3 since `8e3470f8`; the reasons are in
`docs/analysis/lighting-sponza-recalibration.md`. With the project's GI on, only `intensity`, the
colour and `min_occlusion` act there.

## 3. Switches

| CVar | Default | Sponza | Effect | Code |
|---|---|---|---|---|
| `r.RadianceCache` | 0 | 1 (project) | Master switch of the SDF GI: mesh SDF bakes, global distance field and radiance cache. The final gather, the SDF reflections and the DDGI writer all need it | `radiance_cache.cpp:30`; `scene_renderer_init.cpp:739-746`, `:1090-1092`; `frame_gi_passes.cpp:149-161` |
| `r.GI.FinalGather`, `.Samples` | 0, 8 | 1 | Per-pixel hemisphere gather through the distance field; replaces the probe irradiance | `frame_constants.cpp:84-85`, `:837-838`, `:853-861` |
| `r.GI.Reflections`, `.Samples`, `.Steps`, `.Distance` | 0, 4, 96, 5000 | 1 | SDF reflections replace the prefiltered cubemap | `frame_constants.cpp:81`, `:92-96`, `:836` |
| `r.GI.Intensity` | 1 | default | Scales the DDGI, radiance-cache and gather irradiance | `frame_constants.cpp:131`, `:815`, `:883`; `ssgi_world_gather.hlsli:29` |
| `r.GI.RadianceCache.GridExtent` | 5000 cm | 2000 | Half-extent of the 16³ camera-centred grid. Probe spacing is 2 x extent / 15 (about 267 cm on Sponza) | `radiance_cache.cpp:31`, `:116` |
| `r.GI.RadianceCache.RaysPerProbe`, `.ProbesPerFrame` | 32, 1024 | default | Trace budget: each of the 4096 probes is retraced every 4 frames | `radiance_cache.cpp:56`, `:58` |
| `r.RayTracing` and `r.RayTracing.GI` | 0 and 0 | off | DDGI. Both must be on (`IsGIEnabled`), in addition to `r.RadianceCache` | `runtime/engine/renderer/raytracing/rt_manager.cpp:14`, `:39`, `:111-112`; `frame_gi_passes.cpp:336-345`; `frame_constants.cpp:890-904` |
| `r.GI.DDGI.IndirectTerm`, `.RayBuffers`, `.DirectionalSky`, `.Diagnostic` | 1, 1, 1, 0 | default | Second bounce in the DDGI probes; ray buffers so each probe ray is traced once; escaping rays sample the sky cubemap; log the probe radiance once | `ddgi_system.cpp:137-151` |
| `r.GTAO`, `.Radius`, `.Intensity`, `.Power` | 0, 200, 1, 1 | 1 (scene) | Screen-space AO, merged as `min(meshAO, gtao)` | `runtime/engine/renderer/passes/postprocess/gtao_pass.cpp:25-28`, `:675-678`; `frame_constants.cpp:769-773`; `deferred_lighting_ps.hlsl:499-507` |
| `r.IndirectAO.Exponent` | 1 | default | Exponent on `ao` for the diffuse indirect | `frame_constants.cpp:137`; `deferred_lighting_ps.hlsl:1337` |
| `r.ContactShadow.Length` | 0.01 | default | Sun contact-shadow length as a fraction of the far plane. **0 disables: this is the switch** | `frame_constants.cpp:60`, `:649`; `deferred_lighting_ps.hlsl:595-614` |
| `r.ContactShadows` | 1 | default | Registered, but has no effect on the image: it only seeds a pass-inspector entry that nothing reads | `runtime/engine/renderer/frame/frame_lighting.cpp:39`; `scene_renderer_init.cpp:1103`; `docs/analysis/lighting-block-b-04b-contact-shadows.md:76` |
| `r.SkyLight.Intensity`, `.IndirectIntensity`, `.MinAmbient` | 1, 1, 0 | default | See section 2 | `frame_constants.cpp:36`, `:54`, `:50-53` |
| `r.SkyLight.RebakeAngleDeg`, `.RebakeIntensityDelta`, `.RebakeMinInterval` | 0.25, 0.01, 30 | default | When the sky IBL (cubemap and SH) is re-baked: sun direction change in degrees, relative sun colour change, minimum sky passes between bakes | `runtime/engine/renderer/atmosphere/atmosphere_renderer.cpp:39-41` |
| `r.EnvMIS` | 1 | default | Environment-MIS specular on smooth metals | `frame_constants.cpp:82-83` |
| `r.SSGI` | 0 | off | Legacy screen-space GI; added only while the gather is off | `runtime/engine/renderer/gi/ssgi_renderer.cpp:24` |

The distance-field settings are `r.GI.MeshSdf.Resolution` (default 32) and `r.GI.MeshSdf.TwoSided`
(default 0; `runtime/engine/renderer/scene/scene_mesh_assets.cpp:36-39`), and `r.GI.GDF.Resolution`
(default 64, `runtime/engine/renderer/gi/global_distance_field.cpp:70-71`). Each needs a scene
reload; Sponza sets 128, 1 and 256. They are described in
`docs/engine/12-rendering/33-sdf-gi-final-gather.md:137-154`, whose table gives 128 as the
`r.GI.GDF.Resolution` default; the code registers 64.

The 2026-04-06 guide named switches that do not exist or do nothing:

- **`r.GI.SDF`** is not registered. The switch is `r.RadianceCache`. Only the diagnostic
  `r.GI.SDF.SampleFrameOffset` carries that prefix.
- **`r.GI.Brixelizer`** is not registered either. Brixelizer GI is dormant, and its `r.BrixelizerGI*`
  CVars are registered by no current build (`docs/guides/brixelizer-gi-guide.md`).
- **`r.GI.Intensity`** does not gate any light probe volume.
- **`--pixel-debug`** is dead (section 6).

The DDGI atlases and ray buffers are allocated at start-up whether DXR GI is on or not, about
15 MiB (2 MiB irradiance atlas, 5.1 MiB depth atlas, 2 x 4 MiB ray buffers;
`scene_renderer_init.cpp:748-761`, `ddgi_system.cpp:47-118`). The comment at
`scene_renderer_init.cpp:751` says "~256 KB". This is open decision N4.

## 4. The light probe volume is inert

`LightProbeVolumeComponent` (`runtime/engine/ecs/environment/light_probe_volume_component.h`) and
`IrradianceProbeGrid` (`runtime/engine/renderer/gi/irradiance_probe_grid.{h,cpp}`) exist, and the
component loads from scene JSON under `light_probe_volume`
(`runtime/engine/ecs/environment/light_probe_volume_component.cpp:15-52`). The renderer allocates a
grid for the first enabled volume and fills its CB fields (`frame_constants.cpp:906-977`). The
volume still contributes nothing:

- `lpvIntensity` has been hard-coded to 0 since `29c83ba5`, the commit that wired the volume in
  (`frame_constants.cpp:967-972`). The shader samples the volume only when it is above 0
  (`deferred_lighting_ps.hlsl:975-978`).
- With a non-zero intensity it would still be source 3 of the priority chain. While the radiance
  cache runs, no pixel would reach it.
- The probes hold no spatial variation. On a rebuild, every probe is seeded with the same per-frame
  sky SH (with `shScale` and the lower hemisphere applied), converted to the world frame:
  `FillFromSkySH(SHCubemapToWorld(...))` (`frame_constants.cpp:936-947`). The seed is refreshed
  only when the volume is loaded or created (`needsRebuild`), not when the sky is re-baked.
  `IrradianceProbeGrid::CaptureFromSkyCubemap` (`irradiance_probe_grid.cpp:193-216`) is never
  called.
- Only the first enabled volume is used (`frame_constants.cpp:975`). `autoFitScene` and
  `autoFitPadding` are read only by the serializer; no renderer code implements them, contrary to
  `light_probe_volume_component.h:31-33`. The header also says each volume gets its own grid keyed
  by entity (`:7-9`, `:25-27`), but there is a single grid. The default extents are Y-up
  (`gridDimensionsY = 4`, Y from 0 to 400 cm, `:39-49`), while the engine is Z-up.
- No scene uses a volume: none of the 90 `.uzscene` files in the repository has
  `light_probe_volume`. Sponza's `InteriorProbeVolume`, added in `29c83ba5`, was removed in
  `eb839ed9` the same night.

Do not add a volume to light an interior. Use the radiance cache and the final gather (section 5).
Whether to keep the inert code, documented as it is here, or remove it is open decision N3.

## 5. Setting up an interior

**With GI (recommended).**

1. Put `r.RadianceCache 1` and `r.GI.FinalGather 1` in the project's `cvars`, like
   `sample/projects/sponza.uzproject:11-24`. Add `r.GI.Reflections 1` if reflections should be
   occluded by the scene.
2. Size `r.GI.RadianceCache.GridExtent` to the interior. The grid has 16 probes per axis around the
   camera, so the spacing is 2 x extent / 15.
3. For content made of open shells, use `r.GI.MeshSdf.TwoSided 1`
   (`docs/engine/12-rendering/33-sdf-gi-final-gather.md:32-48`).

The bounce comes from the sun and from the sky seen through openings. SkyLight `intensity` scales
the sky part, and `r.GI.Intensity` scales the whole GI irradiance. AO comes from `r.GTAO 1`, with
`minOcclusion` as the floor. The shader's comment documents 0.005-0.02 for that floor
(`deferred_lighting_ps.hlsl:1062-1065`); Sponza uses 0.01.

Expect convergence to take time. Each probe is retraced every 4 frames and blended at 0.12, about
33 frames per e-fold, and a freshly loaded scene keeps converging for about 200 frames
(`docs/analysis/lighting-block-b-02-gdf-incremental.md:112-117`, `:152-154`). A capture at frame 60
is not converged.

**Without GI.** The SH fallback is one set of coefficients for the whole frame, with no occlusion
(`deferred_lighting_ps.hlsl:1047-1059`). An interior is lit like the outside and is darkened only
by AO. `indirectLightingIntensity` scales that ambient; `intensity` does not (N1). `minOcclusion` is
both the AO floor and an irradiance floor. A light probe volume does not help (section 4).

**With DXR.** `r.RayTracing 1` plus `r.RayTracing.GI 1`, together with `r.RadianceCache 1`, add the
DDGI probes. They sit on the radiance cache's grid, with the same origin and spacing
(`frame_constants.cpp:893-895`), and take priority 1 for the diffuse. With `r.GI.FinalGather 1` the
gather replaces them in every pixel, so turn the gather off to see DDGI. The DDGI level has not been
compared with a ground truth (`docs/analysis/lighting-block-b-06-ddgi-dedup.md:94-96`). If
`r.RayTracing.GI 1` is set without `r.RayTracing 1` (with `r.RadianceCache 1`), the log warns once
and the lighting stays on the radiance cache (`frame_gi_passes.cpp:340-345`). Without
`r.RadianceCache 1` the writer's pass is not registered and nothing is logged (`:149-161`).

## 6. Diagnostics

- **Region dump.** This is the instrument that shows which path lit a pixel:

  ```text
  uzengine.exe --project <p> --region-dump <dir> --regions "name:x,y,w,h" --region-fields components --region-first F --region-last L
  ```

  The format is described in `docs/guides/build-and-deployment.md:247-259`; read the files with
  `scripts/flicker/regiondump.py`. The `components` preset (`runtime/tools/uzengine.cpp:543`)
  records `direct_light`, `diffuse_indirect` and `specular_indirect`. The alpha of
  `emissive_and_mis` holds the path bits: 1 SDF reflections, 2 final gather, 4 legacy SSGI,
  16 env-MIS, 128 SSR (`scripts/flicker/regiondump.py:148-152`). `rc_support` and `ddgi_support`
  are zero when that probe query did not run (`:97-98`). A pixel with the gather bit (2) clear and
  both supports zero was lit by the SH fallback.
- **Run manifest.** `--run-manifest <path>` records every CVar that differs from its default
  (`docs/guides/build-and-deployment.md:324-328`).
- **Log lines.**
  - At start-up, "SceneRenderer: SDF GI enabled (GDF + RadianceCache ready)" or
    "... r.RadianceCache=0 — disabled" (`scene_renderer_init.cpp:742-746`).
  - With `--log-level debug`, "SDF GI: frame=N, active=..., probesReady=..." every 120 frames
    (`frame_gi_passes.cpp:156-160`).
  - The one-time DDGI skip warning (`frame_gi_passes.cpp:340-345`).
  - "SkyLight SH L0: ... scale=..." once after frame 100, showing `shScale`
    (`frame_constants.cpp:456-470`).
- **`--diag` and view modes.** Neither isolates the indirect terms; see the instruments list in
  `docs/engine/12-rendering/33-sdf-gi-final-gather.md:173-184`.
- **`--pixel-debug`.** Dead: `runtime/shaders/runtime/debug/pixel_debug_cs.hlsl` is a one-line stub.

Common symptoms:

| Symptom | Likely cause |
|---|---|
| The interior is lit flat, as if outdoors | The pixel is on the SH fallback: `r.RadianceCache` is off, or the cache has not produced a result yet (check the start-up log line and the region dump's supports) |
| `indirect_lighting_intensity` or the lower hemisphere changes nothing | GI is on: no GI path reads them (N2) |
| SkyLight `intensity` changes reflections and GI but not the ambient of a GI-off scene | The SH-fallback diffuse ignores it (N1) |
| `r.RayTracing.GI 1` changes nothing | The gather is on and replaces DDGI; or `r.RayTracing` is off (warning in the log); or `r.RadianceCache` is off; or the device has no DXR (`rt_manager.cpp:47-50`, `:91-93`) |
| Contact shadows cannot be turned off with `r.ContactShadows 0` | Use `r.ContactShadow.Length 0` |
| Black corners with GI on | The radiance cache and the gather adopt zero where no light reaches (neither falls back to the sky); `minOcclusion` floors only the AO there, not the irradiance |

## Related

- `docs/engine/12-rendering/33-sdf-gi-final-gather.md`: distance fields, radiance cache, final
  gather and reflections.
- `docs/specs/rendering/gi-system.md` and `docs/specs/rendering/ibl-pipeline.md`.
- `docs/analysis/lighting-block-b-2026-10.md`: the October lighting blocks, including timings
  (B-01), contact shadows (B-04b), DDGI (B-06) and clustered lights (B-07).
- `docs/analysis/lighting-sponza-recalibration.md`: Sponza's SkyLight values.
- `.rulebook/tasks/phase0_indoor-lighting-system/tasks.md`: the task, its corrections and the open
  decisions N1-N4.
