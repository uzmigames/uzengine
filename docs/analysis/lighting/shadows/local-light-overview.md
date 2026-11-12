<!-- doc-meta
title: UzEngine Local Light Shadow System
category: analysis
engine: uzengine
theme: rendering
created: 2026-03-30
updated: 2026-11-11
status: active
author: docs-operator
-->

# UzEngine Local Light Shadow System

How point and spot lights cast shadows in UzEngine (rulebook task `phase3a-3.30.0-point-spot-light-shadows`,
PCSS from `phase2_omni-pcss-soft-shadows`). This is the implementation as committed on `feat/phase1-lighting`;
every symbol, file and number below is the shipped one. UE5 background lives in `point-spot-overview.md`,
`local-light-atlas-allocation.md` and `point-light-data-flow.md`.

## 1. Pipeline in one paragraph

Each frame `ExtractLightingFromECS()` (`runtime/engine/renderer/lighting/light_renderer.cpp`) collects every
`PointLightComponent` / `SpotLightComponent` with `castShadows`, ranks them by importance and hands the winners
to the scene renderer as `LightFrameData::spotShadows[]` / `pointShadows[]`. `frame_constants.cpp` allocates
atlas tiles for them (one per spot, six per point light) and writes one `LocalLightShadowData` entry per tile
into `ShadowDataCB::localShadows[]` (view-projection, atlas rect, bias, light position, range).
`frame_shadows.cpp` renders the `LocalShadowDepth` render-graph pass: every opaque draw of the frame into every
tile, storing the linear distance to the light. `deferred_lighting_ps.hlsl` samples the tile (hard PCF, or the
PCSS soft path when enabled) and multiplies the light's attenuation by the visibility. Nothing persists across
frames: the atlas is reset and re-rendered every frame.

## 2. Atlas (`runtime/engine/renderer/shadows/local_light_shadow_atlas.h/.cpp`)

One `D32Float` depth texture, `LocalLightShadowAtlas`, created by `Init(device)` and sized by two CVars read at
that point (registered in `shadow_renderer.cpp`):

| CVar | default | range | meaning |
|---|---|---|---|
| `r.Shadow.LocalAtlasSize` | 4096 | 1024..8192 | atlas width = height (texels) |
| `r.Shadow.LocalTileSize` | 1024 | 256..2048 | tile width = height (texels) |

`columns = atlasSize / tileSize`, `maxTiles = min(columns^2, kMaxLocalLightShadows)`. With the defaults that is a
4x4 grid of 1024x1024 tiles = 16 tiles; a smaller tile size does **not** raise the tile count past 16 because the
constant buffer holds 16 entries (section 5). `kLocalShadowAtlasSize`, `kLocalShadowTileSize`,
`kLocalShadowTileCols`, `kMaxLocalShadowTiles` are the compile-time defaults.

Allocation is `Reset()` once per frame, then sequential `AllocateTile(ShadowTileInfo&)`: tile `i` is column
`i % columns`, row `i / columns`; `ShadowTileInfo` carries the pixel viewport (`viewportX/Y/Size`) for the depth
pass and the UV rect (`uvOffsetX/Y`, `uvScale = tileSize / atlasSize`) for the sampler. `AllocateTile` returns
false when the atlas is full; `GetAllocatedTileCount()` is the running count.

## 3. Selection, importance and budget (`light_renderer.cpp`)

Every point light with `castShadows` becomes a `PointLightShadowInfo` candidate (position, range, bias,
`lightIndex` into `LightingCB::pointLights[]`); every spot light with `castShadows` a `SpotLightShadowInfo`
(position, direction, clamped outer half-angle, range, bias, `lightIndex`). Candidates are sorted by

```
importance = |color * intensity| * range^2 / max(1, |lightPos - cameraPos|^2)
```

(`ComputeShadowImportance`, descending). Spots are assigned first: `spotShadowCount = min(candidates, 16)`,
`spotLights[i].shadowIndex = 0..spotShadowCount-1`. Point lights get the remaining entries in blocks of six:
`maxPoint = (16 - spotShadowCount) / 6`, `pointLights[i].shadowIndex = spotShadowCount + 6 * rank`. A light that
does not fit keeps `shadowIndex = 0xFFFFFFFF` and is lit without a shadow. Consequences with the 16-entry CB:

- no spots: at most **2** shadowed point lights (12 entries);
- 1..4 spots: 2 point lights; 5..10 spots: 1; 11..16 spots: none;
- point lights never displace a spot light, whatever their importance.

The spot cone angle used for the shadow frustum is the same clamp the cone attenuation uses:
`outer = clamp(outerAngleDeg, inner + 0.001 rad, 89 deg + 0.001 rad)`.

## 4. Projections (`frame_constants.cpp`, after the CSM cascades)

Both use the engine `Camera` (`SetPosition`, `LookAt`, `SetHorizontalFOV(fov, aspect 1, near, far)`) with
`near = r.Shadow.Local.NearPlane` (default 1.0 cm, range 0.01..50) and `far = range`.

- **Spot**: one tile, `LookAt(position + direction, up)` with `up = +Z` unless `|direction . Z| > 0.99`, then
  `up = +Y`; horizontal FOV = `2 * outer`, so the cone boundary is exactly the tile edge.
- **Point**: six tiles in the order **+X, -X, +Y, -Y, +Z, -Z**, each a 90-degree frustum from the light
  position; up vectors `+Z` for the four horizontal faces, `-Y` for +Z and `+Y` for -Z (Z-up world). The same
  order is hard-coded in the shader's face selection (section 6) and in the unit test
  `runtime/tests/engine/renderer/test_local_light_shadows.cpp` (every direction on the sphere projects inside the
  face selected for it; seams are covered by both neighbours).

Each tile's `LocalLightShadowData` entry: `viewProj`, `atlasOffsetX/Y`, `atlasScale`, `bias` (cm, from the
light's `shadowBias`), `lightPosX/Y/Z`, `radius` (range). `ShadowDataCB::localShadowCount` is the number of
entries written (spots + 6 per point light), `localShadowAtlasIndex` the atlas SRV,
`localShadowNormalOffset = r.Shadow.Local.NormalOffset` (default 0.005, range 0..0.05).

## 5. Constant buffer layout

`ShadowDataCB` (`constant_buffers.h`, 2304 bytes, bound at `b4`): the 768-byte CSM section, then
`LocalLightShadowData localShadows[16]` at offset 768 (16 x 96 = 1536 bytes). One entry is six `float4`:

| float4 | content |
|---|---|
| 0..3 | `viewProj` rows |
| 4 | `atlasOffsetX`, `atlasOffsetY`, `atlasScale`, `bias` (cm) |
| 5 | light position xyz, `radius` |

The shader (`shadow.hlsli`) declares the array as `float4 localShadowRaw[96]` and indexes entry `e` at
`localShadowRaw[6e .. 6e+5]`. The scalar fields it reads: `localShadowAtlasIndex` (696), `localShadowCount`
(700), `localShadowNormalOffset` (704), `localSoftPCSS` (752), `localPcssSourceRadius` (756).

The per-light side lives in `LightingCB`: `PointLightData` (64 bytes) carries `shadowIndex` at offset 48 and
`softShadowRadius` at 52; `SpotLightData` (80 bytes) carries them at 64 and 68 (`light_types.hlsli` mirrors both).
`npm run test:layouts` and `test_deferred_pipeline.cpp` pin these offsets; `test_local_light_shadows.cpp` pins the
`LocalLightShadowData` / `ShadowDataCB` ones.

## 6. Depth pass and sampling

**`LocalShadowDepth`** (`frame_shadows.cpp`): the atlas is cleared to 1.0, then for every tile a viewport +
scissor of the tile, the `GetShadowDepthLinear` pipeline (`shadow_depth_vs.hlsl` with `UZ_SHADOW_LINEAR=1`
forwarding the world position + `shadow_depth_linear_ps.hlsl`), a per-tile `ShadowCB` (view-projection, light
position, range) and every opaque command of the frame's draw list (translucent, additive and mesh-decal
materials are skipped). The tile stores `saturate(distance(fragment, light) / range)` in `SV_Depth`, tested with
`Less`: a linear value is independent of the projection's Z convention (the `Camera` projections are reversed-Z
while the shadow PSO and comparison sampler follow the CSM's standard-Z, see `shadow-depth-z-convention.md`)
and keeps its precision over the whole range. Cost scales with tiles x draws: the demo scene below renders
13 tiles x 6 meshes = 78 depth draws.

**Hard path** (`SampleLocalShadow`, `deferred_lighting_ps.hlsl`): push the receiver along its geometric normal by
`localShadowNormalOffset * distance` (against acne on grazing surfaces), project it with the tile's `viewProj`,
return lit when it falls outside the tile (`w <= 0` or `|ndc| > 1`), else compare
`(distance(light, receiver) - bias) / range` against the tile with the hardware LESS comparison sampler
(`SampleCmpLevelZero`, one bilinear 2x2 tap).

**Point lights** (`ShadePointLight`): the face is the major axis of the light-to-receiver direction
(`a.x >= a.y && a.x >= a.z` -> +X/-X, else `a.y >= a.z` -> +Y/-Y, else +Z/-Z), entry `shadowIndex + face`.

**Soft path** (`SampleLocalShadowSoft`, Fernando 2005 PCSS): enabled when `r.Shadows.SoftPCSS` (master) and
`r.Shadows.SoftPCSS.Local` (default ON) are set and the light's emitter radius is positive. The radius per light
is `LocalPcssRadius(softShadowRadius, sourceRadius)`: the light's `softShadowRadius` when non-zero (negative =
hard shadow for that light), else its `sourceRadius` when positive (point lights), else
`r.Shadows.SoftPCSS.Local.SourceRadius` (5 cm). A 16-tap blocker search over the clamped filter support
(`r.Shadow.PCSSMaxPenumbra`, 16 texels), penumbra = `(d_receiver - d_blocker) * radius / d_blocker`
(`PcssLocalPenumbraRadius`), then 16 Poisson comparison taps with that radius; taps are traced as rays and
re-projected per tap (`LocalShadowTapUV`), so a point-light kernel crosses cube-face seams into the neighbouring
tile instead of repeating a tile edge, and each tap is clamped half a texel inside its tile.

## 7. Scene keys (`scene_loader.cpp`)

Point and spot `light` components: `castShadows` (bool, default false), `shadowBias` (cm, default 0.5),
`softShadowRadius` (cm, default 0 = inherit, < 0 = hard), `range`, `intensity`, `color`; spots add `innerAngle`
/ `outerAngle` (degrees) and point lights `sourceRadius`. The demo scene
`sample/content/scenes/local_shadows_test.uzscene` (project `sample/projects/local_shadows_test.uzproject`) has
two shadowed point lights (red, blue) and one shadowed spot over a checker floor with three pillars: the log
reports `LocalShadow: 1 spot + 2 point light shadows allocated (13 tiles)` and `LocalShadowDepth: 13 tiles, 78
total draw calls`.

## 8. Validation (RTX 5080, 2026-11-11)

Evidence lane `E:/UzmiEngine/_evidence/lighting/point-spot-shadows-finish/` (report.md, the fixture generator and
the analytic-truth analysis). Three single-light fixtures without sun or sky, frame 41, whole-frame linear-HDR
dumps; visibility = direct light divided by the unshadowed capture; every pixel's world point is intersected with
the exact occluder boxes to get the ground truth:

- closed room, point light at its centre, one occluder per cube face, four cameras: every face receives its
  shadow (umbra pixels on all six faces); interior umbra visibility max 0.0000 (no leaks), interior lit p0.1 = 1.0
  (no acne) on the hard path; measured edges within p99 1.7 cm of the analytic edges (bias + normal offset).
- spot, bar across the cone: both shadow edges within 0.6 cm of the analytic +-16.67 cm at all 16 stations
  across the cone (no peter-panning); PCSS 10-90 % penumbra 9.2-10.1 cm at every station against an analytic
  8.7 cm (the PCF adds about one texel); hard path 1.7-2.6 cm.
- pillars/wall: the stripes' edges within p99 1.4 cm of analytic; no leaks, no acne.

**Known artifact (seam line)**: a one-sided band of `localShadowNormalOffset x distance` (about 1.4-2.3 cm here)
along every cube-face seam reads lit inside a point-light shadow, on both the hard and the PCSS path, because
`ShadePointLight` selected the face from the raw pixel position while both samplers project the normal-offset
receiver: a receiver within that offset of a seam projects outside the face chosen for it and the samplers'
"outside the tile = lit" early-out fires. 9-19 % of the analytic-umbra pixels within +-3 cm of a seam are lit in
the room captures; `r.Shadow.Local.NormalOffset 0` removes the line (and the acne protection). The fix is to
select the face from the same offset receiver (`seam-fix.patch` in the evidence lane).

## 9. Limits against the proposal

- Acceptance criterion 4 of the proposal ("max 4 shadowed point lights") is not met by design: 16 entries / 6
  faces = 2 point lights per frame (section 3). Raising it means growing `kMaxLocalLightShadows` (and
  `ShadowDataCB`, 96 bytes per entry) or a cheaper point-light layout (dual-paraboloid, 2 tiles).
- Resolution is fixed per tile; there is no screen-coverage-driven resolution or shadow caching.
- Only opaque geometry casts; no contact shadows for local lights.

## 10. Files

| file | role |
|---|---|
| `runtime/engine/renderer/shadows/local_light_shadow_atlas.h/.cpp` | atlas texture, tile allocation |
| `runtime/engine/renderer/lighting/light_renderer.h/.cpp` | candidates, importance, budget, `shadowIndex` |
| `runtime/engine/renderer/frame/frame_constants.cpp` | spot / cube-face projections, `localShadows[]` entries |
| `runtime/engine/renderer/frame/frame_shadows.cpp` | `LocalShadowDepth` pass |
| `runtime/engine/renderer/core/params/constant_buffers.h`, `shadow_data_params.h` | `LocalLightShadowData`, `ShadowDataCB`, `PointLightData` / `SpotLightData` |
| `runtime/shaders/runtime/lighting/shadow_depth_linear_ps.hlsl` | linear-distance tile |
| `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`, `shadow.hlsli`, `light_types.hlsli` | sampling, PCSS, CB mirror |
| `runtime/engine/ecs/lighting/light_component.h`, `runtime/engine/ecs/loaders/scene_loader.cpp` | components, scene keys |
| `runtime/tests/engine/renderer/test_local_light_shadows.cpp` | layout, atlas, extraction, cube faces, spot tile |

## Changelog

- 2026-11-11: rewritten against the shipped implementation (linear-distance tiles, PCSS, `softShadowRadius`,
  CVars, budget arithmetic, validation, the seam artifact). The 2026-03-30 text described a planned design
  (`AllocateSpotTile` / `AllocatePointTiles`, orthographic faces, hardware-PCF-only) that was never the code.
- 2026-03-30: initial version.
