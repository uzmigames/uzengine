# Lumen temporal-stability mechanisms (blueprint for UzEngine's radiance cache + GI)

Source studied: UE5 at `E:/UnrealGITHUB` (read-only). Shaders under
`Engine/Shaders/Private/Lumen/` (`.usf`/`.ush`), C++ under
`Engine/Source/Runtime/Renderer/Private/Lumen/`. Every mechanism below cites the
file and function it was read from; every constant is the default in this snapshot.

Target of the comparison: UzEngine `runtime/engine/renderer/gi/radiance_cache.{h,cpp}`
and `.gi-shaders/gi/radiance_cache_trace_cs.hlsl` — a 16^3 = 4096 probe grid centered
on the camera, 32 full-sphere rays per probe per update, L2 SH, EMA blend
(`kTemporalBlendAlpha` = 0.12 in the header today, 0.05 historically), 1024 probes per
frame round-robin (`kProbesPerFrame`), and a separate SDF final gather that reads the SH.

---

## 0. The answer in one page

Lumen's GI does not flicker for reasons that have very little to do with the blend
factor. Its steady-state temporal alpha is 1/11 ≈ 0.09 — the same ballpark as
UzEngine's 0.12. The stability comes from *what* is averaged, *where* it is averaged,
and *when* the average is thrown away:

| # | Mechanism | Lumen | UzEngine today |
|---|-----------|-------|----------------|
| 1 | World probes are **not temporally accumulated at all**. A probe is re-traced completely (256–4096 fixed, un-jittered directions) and the result **replaces** the old one. Static scene ⇒ re-trace gives the identical image ⇒ nothing to flicker. | `TraceFromProbesCS`: "No temporal accumulation", `JITTER_TRACE_DIRECTION 0` | 32 random rays/update, EMA'd forever; never converges, drifts per probe |
| 2 | Probes are **world-locked and persistent**: clipmap origin snapped to the cell grid, probe identity re-mapped by world position when the window scrolls, unused probes kept 8 frames. | `FClipmapGeometry::SnapToGrid`, `UpdateCacheForUsedProbesCS`, `NumFramesToKeepCachedProbes=8` | Camera-centered grid; re-center ⇒ probes reclassify |
| 3 | **Few probes per frame, each done properly**: priority histogram by staleness, budget 100 probes × cost units; new probes always traced (downsampled if over budget). | `GetPriorityBucketIndex`, `SelectMaxPriorityBucketCS`, `AllocateProbeTracesCS` | 1024 probes/frame, each with a partial noisy estimate |
| 4 | World cache is **far-field only**: probe rays start at `TMin = cell·√3`; receivers must trace ≥ 2·cell·√3 themselves before interpolating. Near-field occlusion/contact comes from per-pixel screen probes, never from the grid. | `UpdateRadianceCacheState` (ProbeTMin), `GetRadianceCacheCoverage` (MinTraceDistanceBeforeInterpolation), `LumenScreenProbeTracing.usf` | Grid SH is the whole GI, including near field ⇒ coarse-grid shadows crawl with the grid |
| 5 | **Visibility-aware interpolation**: sphere-parallax reprojection, per-texel hit depth atlas, neighbor-to-neighbor occlusion test in the probe spatial filter. | `SampleRadianceCacheProbeWithParallaxCorrection`, `FilterProbeRadianceWithGatherCS` | Plain trilinear SH |
| 6 | **Screen-space temporal filter with reprojection + disocclusion rejection + variable history length (0..10 frames) + "lighting is moving" detection**, applied to the per-pixel integrated result. This is the piece that actually removes residual noise. | `ScreenProbeTemporalReprojectionCS`, `StochasticLightingTileClassification.usf` | None (final gather is re-noised every frame) |
| 7 | **Spatial pre-filter on probe radiance** before integration: 3 passes, edge-aware (depth + hit-direction angle), widened when temporal history is short. | `ScreenProbeFilterGatherTracesCS` | None |
| 8 | Ray jitter sequence length (8) is **matched to the history length** (10) so the accumulated set is close to a full stratified set, not a random walk. | `MaxRayDirections=8`, `GetProbeTexelCenter` | Hammersley set size randomized per frame; union over frames is random |
| 9 | Firefly clamp on every ray before averaging. | `MaxRayIntensity=10` | None |
| 10 | What is left for TSR: only sub-pixel blue-noise dither (quantization, stochastic 1-of-4 probe pick, ±16 px interpolation jitter). TSR has zero Lumen-specific code. | `QuantizeForFloatRenderTarget`, `StochasticInterpolation=1`, `FullResolutionJitterWidth=1` | — |

The crawl in UzEngine is the sum of (1) a non-converging EMA whose error is a slowly
wandering per-probe offset, (2) a 15 Hz round-robin step on top of it, (3) interpolation
between independently wandering probes, (4) a grid 6× coarser than Lumen's finest
clipmap being used for the near field. Lumen avoids each of these structurally, not by
tuning alpha.

---

## 1. Architecture: two tiers, and which tier does what

```
                       per frame
  GBuffer ───► Screen probes (1 per 16x16 px, jittered)    ◄── "final gather"
                 │  64 importance-sampled rays each
                 │  screen trace → mesh SDF → global SDF
                 │  ...up to MinTraceDistanceBeforeInterpolation
                 ▼
          World Radiance Cache (clipmapped, sparse, persistent) ◄── "far field"
                 │  32x32 octahedral radiance + hit depth per probe
                 │  re-traced only when stale (budget 100/frame)
                 ▼
  Screen probe radiance (8x8 octahedral per probe)
     → CompositeTraces (firefly clamp, dither)        LumenScreenProbeFiltering.usf
     → CalculateMoving
     → [TemporallyAccumulateTraceRadiance: OFF by default]
     → FilterRadianceWithGather ×3 (spatial, edge-aware)
     → ConvertToIrradiance (6x6 oct or SH3), FixupBorders, mips
     → ScreenProbeIntegrateCS (per pixel, plane-weighted 4-probe interp)  LumenScreenProbeGather.usf
     → ScreenProbeTemporalReprojectionCS (per pixel, reprojected history)  LumenScreenProbeGatherTemporal.usf
     → FDiffuseIndirectCompositePS (× DiffuseColor, add to SceneColor)     IndirectLightRendering.cpp
     → TSR / TAA (generic; no Lumen inputs)
```

Pass order, world cache (`LumenRadianceCache.cpp::UpdateRadianceCaches`, RDG event
names in order): `ClearProbeFreeList` (non-persistent only) → `UpdateCacheForUsedProbes`
→ `ClearRadianceCacheUpdateResources` → `AllocateUsedProbes` →
`SelectMaxPriorityBucket` → `AllocateProbeTraces` → `SetupProbeIndirectArgsCS` →
`ComputeProbeWorldOffsets` (irradiance configs only) → `ClearProbePDFs` →
`ScatterScreenProbeBRDFToRadianceProbes` → `GenerateUniformProbeTraceTiles` /
`GenerateAdaptiveProbeTraceTiles` → `SetupTraceFromProbesCS` → `SortTraceTiles` (off) →
`TraceFromProbes` → `FilterProbeRadiance` → `CalculateProbeIrradiance` +
`PrepareProbeOcclusion` (irradiance configs only) → `FixupBordersAndGenerateMips`.

Pass order, screen probes (`LumenScreenProbeGather.cpp` + `LumenScreenProbeFiltering.cpp`):
`UniformPlacement` → `AdaptivePlacementMark/Spawn` → GatherScreenData (BRDF PDF +
disocclusion flag) → `MarkRadianceProbes(ScreenProbes)` → world cache update →
tracing → `CompositeTraces` → `CalculateMoving` → `TemporallyAccumulateRadiance`
(off) → `FilterRadianceWithGather` ×3 → `ScreenProbeConvertToIrradiance` → `FixupBorders`
→ `GenerateMip` → `TileClassificationBuildLists` → `ScreenProbeIntegrate` →
`TemporalReprojection`.

---

## 2. World Radiance Cache

### 2.1 Placement: world-locked clipmaps

Config for the diffuse GI path (`LumenScreenProbeGather.cpp::SetupRadianceCacheInputs`):

| CVar | Default | Meaning |
|------|---------|---------|
| `r.Lumen.ScreenProbeGather.RadianceCache` | 1 | persistent world cache on |
| `...RadianceCache.NumClipmaps` | 4 | |
| `...RadianceCache.GridResolution` | 48 | probes per axis per clipmap |
| `...RadianceCache.ClipmapWorldExtent` | 2500 cm | half-extent of clipmap 0 |
| `...RadianceCache.ClipmapDistributionBase` | 2.0 | each clipmap 2× the previous |
| `...RadianceCache.ProbeResolution` | 32 | octahedral map 32×32 = 1024 directions |
| `...RadianceCache.NumMipmaps` | 1 | |
| `...RadianceCache.ProbeAtlasResolutionInProbes` | 128 | 128×128 = 16384 probe slots |
| `...RadianceCache.NumProbesToTraceBudget` | 100 | × `LumenFinalGatherLightingUpdateSpeed` [0.5, 4], ×10 in editor |
| `...RadianceCache.NumFramesToKeepCachedProbes` | 8 | keep-alive for unused probes |
| `...RadianceCache.ReprojectionRadiusScale` | 1.5 | sphere parallax radius = 1.5 × TMin |

Derived: clipmap 0 cell = 5000/48 = **104 cm**; clipmaps 1..3 = 208, 417, 833 cm;
coverage ±25/50/100/200 m. Compare UzEngine: 16 probes over ±50 m ⇒ **~667 cm**
spacing, i.e. between Lumen's clipmap 2 and 3 — the resolution Lumen reserves for
geometry 50–100 m away is UzEngine's resolution everywhere.

Origin snapping (`LumenRadianceCache.cpp::FClipmapGeometry::SnapToGrid`, used by
`UpdateRadianceCacheState`): the clipmap origin is `floor(ViewOrigin / CellSize) * CellSize`
per level, so the probe lattice is fixed in world space and camera motion only slides a
window by whole cells. `ProbeTMin = |(CellSize,CellSize,CellSize)| × ProbeTMinScale(1.0)
= CellSize·√3` for non-irradiance configs (same function).

Clipmap selection for a receiver (`LumenRadianceCacheInterpolation.ush::GetRadianceProbeClipmap`):
first clipmap whose `EdgeFade` (per-axis `saturate((coord − 0.5) × InvClipmapFadeSize)`,
`InvClipmapFadeSize = 1.0`) exceeds the caller's dither. The screen-probe tracer passes
dither = 0 (`LumenScreenProbeTracing.usf`: `GetRadianceCacheCoverage(..., 0)`), so the
clipmap choice is a deterministic function of world position — no per-frame flipping at
clipmap seams.

### 2.2 Sparse allocation driven by receivers

Nothing is traced "because it is in the grid". A frame starts by clearing the
indirection texture (`ClearProbeIndirectionCS`), then receivers *mark* the 8 corner
probes of the cell they will interpolate from with `USED_PROBE_INDEX`
(`LumenRadianceCacheMarkCommon.ush::MarkPositionUsedInIndirectionTexture`; for GI the
marker is the `MarkRadianceProbes(ScreenProbes)` pass, one mark per screen probe). Only
marked cells get a probe slot in the 128×128 atlas (`AllocateUsedProbesCS` →
`AllocateProbeIndex`, free-list first, then bump allocator).

Per probe, the atlas stores: 32×32 radiance (`RadianceProbeAtlasTexture`, lighting data
format), 32×32 hit depth `R16_UINT` with front-face/two-sided/hit bits
(`EncodeProbeDepth` in `LumenRadianceCacheInterpolation.ush`), a final bordered copy for
bilinear sampling (`FinalRadianceAtlas`, `FinalProbeResolution = 32 + 2 × (1 << MaxMip)`),
`ProbeLastUsedFrame`, `ProbeLastTracedFrame`, `ProbeWorldOffset`, `AllocatedProbeData`
(coord, clipmap, priority bucket, trace cost; `LumenRadianceCacheUpdate.ush`).

### 2.3 Persistence across frames and camera motion

`LumenRadianceCacheUpdate.usf::UpdateCacheForUsedProbesCS` (persistent path):
for every probe allocated last frame, recompute its world position from last frame's
clipmap corner + coord (`GetLastFrameProbeTranslatedWorldPosition`, includes the
`PreViewTranslation` delta), convert to this frame's probe coord in the same clipmap,
and keep it if

```
ProbeUsedMarker == USED_PROBE_INDEX
|| FrameNumber - LastUsedFrameNumber < NumFramesToKeepCachedProbes   // 8
```

Kept probes write themselves back into the indirection texture (so the marker is
replaced by the real index and `AllocateUsedProbesCS` treats them as "re-trace existing
probe" candidates); anything else goes to the free list. The C++ comment explains the
8-frame keep-alive: screen probes are jittered every frame, so the set of marked cells
changes every frame; without keep-alive the cache would invalidate constantly.
(A `NumFramesToKeepCachedProbesHeuristic` that drops to 2 frames when free slots <
budget is computed but, in this snapshot, not used in the comparison.)

Full invalidation happens only when (`UpdateRadianceCaches`):
`bResizedHistoryState` (config change), `GRadianceCacheForceFullUpdate`, or
`bPropagateGlobalLightingChange`. The latter (`LumenSceneRendering.cpp::UpdateGlobalLightingState`)
fires when the directional-light or sky-light max color changes by a factor > 4 or
< 0.25. On those frames `NumProbesToTraceBudget = UINT32_MAX` and the cache is
non-persistent — everything marked is re-traced once.

### 2.4 Update scheduling: who gets re-traced

Cost model (`LumenRadianceCacheUpdate.ush`): `PROBE_TRACE_COST_DOWNSAMPLED = 1`,
`NORMAL = 4`, `SUPERSAMPLED = 16`; budget = `NumProbesToTraceBudget × 4` cost units
per frame (`GetProbeTraceCostBudget`), i.e. 400 units ⇒ 100 full probes, or 400
downsampled, or 25 supersampled. A probe's cost comes from camera distance
(`GetProbeTraceCost`: beyond `r.Lumen.RadianceCache.DownsampleDistanceFromCamera` =
4000 cm ⇒ cost 1; inside `SupersampleDistanceFromCamera` (= −1, disabled) ⇒ 16; else 4).

Priority (`LumenRadianceCacheUpdate.usf::GetPriorityBucketIndex`, 16 buckets):

```
if LastTracedFrame == NEVER_TRACED:        bucket 0            // new probes: most urgent
else:
   F = max(LastUsedFrame - LastTracedFrame, 1)                // how long it has been used while stale
   importance = F / (ClipmapIndex + 1)                        // far clipmaps age slower
   bucket = 15 - clamp(log2(importance), 0, 14)               // log2 histogram, 1 = "most stale"
```

`AllocateUsedProbesCS` adds each probe's cost to `RWPriorityHistogram[bucket]`.
`SelectMaxPriorityBucketCS` walks buckets 0→15 summing cost until the budget is hit,
records `MaxUpdateBucket` and how much cost may still be taken from it.
`AllocateProbeTracesCS` then: traces every probe in buckets < max; traces probes in the
max bucket until `MaxTracesFromMaxUpdateBucket` is exhausted (atomic); and for bucket 0
never skips — it force-downsamples (`bForceDownsample`) once bucket 0 alone exceeds the
budget, so a new probe always has *some* valid data the frame it is created. Traced probes
get `ProbeLastTracedFrame = FrameNumber`.

Consequence: a probe used continuously is re-traced roughly every (budget / live probes)
frames, nearer clipmaps first; a probe not used is never traced. In a typical view
thousands of probes are live and ~100 change per frame. The other ~97% are byte-for-byte
the same as last frame.

### 2.5 Tracing and writing a probe: deterministic, no blend

`LumenRadianceCache.usf::TraceFromProbesCS` traces 8×8 tiles of an octahedral map at
`TraceResolution = (32/2) << Level`:

- Level 0 = 16×16 (256 rays), level 1 = 32×32 (1024), level 2 = 64×64 (4096).
- Direction = `EquiAreaSphericalMapping((texel + 0.5) / TraceResolution)`. The jitter
  path is compiled out: `#define JITTER_TRACE_DIRECTION 0` with the comment
  *"No temporal accumulation, so just reads as dirty lighting"*. Same texel ⇒ same
  direction every time the probe is traced.
- `TraceInput.bDitheredTransparency = CalculateIrradiance > 0 ? 0 : 1` with the comment
  *"We want stable lighting when using radiance cache probes directly for lighting"*.
- Cone half-angle `acos(1 − 1/TraceResolution²)`; min distance `GetRadianceProbeTMin`
  (cell·√3), max `MaxTraceDistance`; traces the Lumen scene (cards/voxels) and applies
  sky.
- Output per texel: radiance × `CachedLightingPreExposure`, and `EncodeProbeDepth(hit
  distance, bHit = transparency < 0.5, ...)`. Lower levels are upsampled by replication
  into the 32×32 slot; level 2 is box-downsampled (depth = min, flags = OR).

Which texels get which level (`GenerateAdaptiveProbeTraceTilesCS`): the screen probes that
marked this world probe scatter their diffuse-BRDF SH into a per-probe PDF
(`ScatterScreenProbeBRDFToRadianceProbesCS`, 9 coefficients + weight, atomics). A level-0
tile is refined to level 1 if `PDF(tile center) > 0`, and to level 2 (when enabled) if
`PDF > r.Lumen.RadianceCache.SupersampleTileBRDFThreshold` = 0.1. Directions no receiver
can see (e.g. into the floor for a floor probe) stay at 16×16; useful directions get the
full 32×32. Two-sided foliage receivers contribute PDF 0 to keep foliage probes cheap.

The traced texels are written into the probe's atlas slot (or a temp atlas when
filtering) and **replace** the old content. There is no history read anywhere in the
world-cache update. Stability of the cache therefore equals stability of a deterministic
trace of the scene: for a static scene the re-trace is identical, and for a changed scene
the probe jumps to the new value in one update.

### 2.6 Spatial filter between neighbouring probes

`LumenRadianceCache.usf::FilterProbeRadianceWithGatherCS` (`r.Lumen.RadianceCache.SpatialFilterProbes` = 1),
run only for the probes traced this frame, per texel, over the 6 face neighbours in the
same clipmap (if allocated):

1. `OcclusionWeight = neighbour texel is front-face or two-sided ? 1 : 0`.
2. Two-way visibility test using the depth atlases: from this probe towards
   `NeighbourPos + 2·cell × dir`, and from the neighbour towards `ThisPos + 2·cell × dir`;
   if either probe's stored hit distance in that direction is shorter than the test
   distance, `OcclusionWeight = 0` (thin wall between probes ⇒ no sharing).
3. Neighbour hit distance clamped to our own (comment: preserves contact shadows).
4. `AngleWeight = 1 − saturate(angle(neighbourHit − thisPos, dir) /
   SpatialFilterMaxRadianceHitAngle)`. The CVar `r.Lumen.RadianceCache.SpatialFilterMaxRadianceHitAngle`
   = 0.2 is documented "in degrees" but passed raw (`LumenRadianceCache.cpp:2693`) and
   compared against `acosFast` radians ⇒ effectively 0.2 rad ≈ 11.5°.
5. Weighted average with self weight 1, written with blue-noise dithered quantization
   (`QuantizeForFloatRenderTarget`).

Net effect: only radiance whose hit point is far enough that neighbouring probes see
"the same thing" is averaged; near hits and anything behind a wall are left alone.

### 2.7 Output and sampling

`FixupBordersAndGenerateMipsCS` copies the (filtered) 32×32 into the bordered final
atlas (`OctahedralMapWrapBorder`) and builds mips if requested (`NumMipmaps` = 1 ⇒ none
for GI). Sampling (`LumenRadianceCacheInterpolation.ush::SampleRadianceCacheInterpolated`):
mip from cone angle; 2×2×2 corner probes; either full trilinear or, with
`RADIANCE_CACHE_STOCHASTIC_INTERPOLATION`, one probe chosen by the trilinear weights and
a random scalar. Each probe sample goes through `SampleRadianceCacheProbeWithParallaxCorrection`:

```
R = ReprojectionRadiusScale(1.5) × ProbeTMin
T = second hit of ray(P, dir) with sphere(ProbePos, R)
dir' = (P + dir·T) − ProbePos                 // direction from the probe to that point
CorrectionFactor = T² / (R · dot(dir', dir))  // cancels the grid-pattern attenuation
radiance = FinalAtlas.SampleLevel(oct(dir'), mip) × CorrectionFactor
hitDistance = DepthAtlas[oct(dir')]
```

The returned hit distance is what the screen-probe spatial filter and integration use
for the ray, so a cached ray still carries a plausible depth.

### 2.8 Leak prevention / "probe occlusion" summary for the GI path

For the diffuse GI configuration (`CalculateIrradiance = 0`), the mechanisms that apply are:

1. **Near field excluded from the cache.** Probe rays begin at `TMin = cell·√3`
   (clipmap 0: 180 cm). Screen-probe rays must trace at least
   `MinTraceDistanceBeforeInterpolation = TMin + cell·√3 = 2√3·cell` (clipmap 0: 361 cm)
   before the cache may answer (`LumenRadianceCacheInterpolation.ush::GetRadianceCacheCoverage`;
   applied in `LumenScreenProbeTracing.usf`: `TraceDistance = min(TraceDistance,
   Coverage.MinTraceDistanceBeforeInterpolation)`, then `SampleRadianceCacheAndApply` only
   if the ray is still transparent). Occlusion inside the cell comes from the receiver's
   own rays.
2. **Sphere parallax + per-direction depth** (2.7) instead of treating the probe as a
   point at infinity.
3. **Inter-probe occlusion test in the spatial filter** (2.6), so a probe never inherits
   radiance from a neighbour behind a thin wall.
4. **Deterministic clipmap pick and world-locked lattice** (2.1), so the set of probes a
   receiver reads does not change unless the receiver moves a full cell.

Mechanisms that exist in the code base but run **only** for irradiance configurations
(`CalculateIrradiance` — irradiance-field gather / translucency volume), gated in
`LumenRadianceCache.cpp` at lines 2322 and 2726:

- `ComputeProbeWorldOffsetsCS` — relocate a probe by up to 0.25 cell, either toward
  the GBuffer surface (`r.Lumen.RadianceCache.IrradianceProbeOffsetMode` = 1: averages
  "ideal" offsets `0.2·N + 0.8·V` scaled by 0.75·cell·0.25 over 16 screen samples) or
  by searching 64 SDF samples for the point farthest from any surface (mode 2). The
  offset is persisted in `ProbeWorldOffset` and reused until the probe is re-traced.
- `PrepareProbeOcclusionCS` — per direction `(depth, depth²)` for Chebyshev-style
  visibility, backface counting, and a `ProbeValid` flag (SWRT: invalid when inside
  the global SDF by more than half a voxel). `r.Lumen.RadianceCache.OcclusionKernelCosineExponent` = 128.

These are the DDGI-style "probe occlusion" pieces; the screen-probe GI path does not need
them because it never uses the grid for near-field visibility.

### 2.9 Radiance-cache CVars (all defaults, `LumenRadianceCache.cpp`)

`r.Lumen.RadianceCache.Update` 1 · `ForceFullUpdate` 0 · `SpatialFilterProbes` 1 ·
`SpatialFilterMaxRadianceHitAngle` 0.2 (raw radians, see 2.6) · `SortTraceTiles` 0 ·
`IrradianceProbeOffsetMode` 1 · `OcclusionKernelCosineExponent` 128 ·
`ForceUniformTraceTileLevel` −1 · `SupersampleTileBRDFThreshold` 0.1 ·
`SupersampleDistanceFromCamera` −1 · `DownsampleDistanceFromCamera` 4000 ·
`TemporaryAtlasDownsampleFactor` 6 · `ShowBlackRadianceCacheLighting` 0.

---

## 3. Screen Probe Gather: the final gather and its denoiser

### 3.1 Probe placement and per-frame jitter

One uniform probe per `r.Lumen.ScreenProbeGather.DownsampleFactor` = 16 px tile
(1080p ⇒ 120×68 = 8160), placed at tile origin + `GetScreenTileJitter(StateFrameIndexMod8)
= Hammersley16(i, 8) × 16` (`LumenScreenProbeCommon.ush`), i.e. an 8-frame cycle of
sub-tile offsets. Each probe records depth (sign = invalid), octahedral normal, world
speed (`WriteDownsampledProbeMaterial`; speed from `GetHistoryScreenPositionIncludingTAAJitter`).
Adaptive probes (`NumAdaptiveProbes` = 8 per uniform probe, `AdaptiveProbeAllocationFraction`
= 0.5) are spawned where the uniform corners fail the plane-distance test for a pixel
(`CalculateUpsampleInterpolationWeights`).

### 3.2 Tracing: 64 rays, importance sampled, jittered on an 8-frame cycle

`TracingOctahedronResolution` = 8 ⇒ 64 rays/probe (`GatherOctahedronResolutionScale` = 1).
Ray directions use `GetProbeTexelCenter(tile) = BlueNoiseVec2(tile,
ScreenProbeRayDirectionFrameIndex)` (`LumenScreenProbeTracingCommon.ush`) with
`ScreenProbeRayDirectionFrameIndex = StateFrameIndex % r.Lumen.ScreenProbeGather.Temporal.MaxRayDirections (8)`
(`LumenScreenProbeGather.cpp:2281`). The CVar help says it explicitly: *"Number of possible
random directions per pixel. Should be tweaked based on MaxFramesAccumulated."* — the
jitter sequence length is chosen so that the ~10-frame history covers the whole set.

Per ray (`LumenScreenProbeTracing.usf`): screen-space trace → mesh SDF → global SDF,
capped at `MinTraceDistanceBeforeInterpolation` when the cache covers the origin; a miss
within that distance samples the world cache (`SampleRadianceCacheAndApply`) and takes its
hit distance. Each ray stores `EncodeProbeRayDistance(hit, bHit, bMoving, bReachedRadianceCache)`.
`bMoving = IsTraceMoving(...)` = `|probeSpeed − |hitVelocity|| / max(depth, 100) >
r.Lumen.ScreenProbeGather.Temporal.RelativeSpeedDifferenceToConsiderLightingMoving (0.005)`
(`LumenScreenProbeTracingCommon.ush`) — "the light I see from here is attached to
something moving relative to me".

### 3.3 Composite, clamp, and the per-probe "moving" scalar

`ScreenProbeCompositeTracesWithScatterCS` (`LumenScreenProbeFiltering.usf`): every ray's
radiance is clamped so `max(r,g,b) ≤ r.Lumen.ScreenProbeGather.MaxRayIntensity (10)` (with
pre-exposure) *before* averaging — the firefly clamp. Rays are scattered into the 8×8
gather map with blue-noise dithering to the neighbouring texel (`DITHER_TRACE_DIRECTIONS 1`)
to hide the octahedral grid; per texel the min hit distance and the `bMoving` fraction are
kept. `ScreenProbeCalculateMovingCS` averages the texel moving fractions into
`ScreenProbeMoving[probe]`.

### 3.4 Temporal filter on probes: present but off

`ScreenProbeTemporallyAccumulateTraceRadianceCS` reprojects each probe to last frame's
probe grid, takes the 2×2 history probes whose world position lies within a plane distance
(`exp2(−10000·(Δ/depth)²) > 0.1`) and lerps with `r.Lumen.ScreenProbeGather.TemporalFilterProbes.HistoryWeight`
(0.5). It is disabled by default (`r.Lumen.ScreenProbeGather.TemporalFilterProbes` = 0).
Lumen chose to accumulate *after* integration, per pixel, instead.

### 3.5 Spatial filter on probe radiance (before integration)

`ScreenProbeFilterGatherTracesCS`, `r.Lumen.ScreenProbeGather.SpatialFilterNumPasses` = 3,
`SpatialFilterHalfKernelSize` = 1, per texel (same octahedral direction) over the 4-neighbour
cross of uniform probes:

```
PositionWeight = exp2(-SpatialFilterPositionWeightScale(1000) · (|dn - d| / d)²)   // neighbour probe depth vs ours
NeighbourHit   = min(neighbourHitDistance, ourHitDistance)                          // contact-shadow preservation
AngleWeight    = 1 - saturate(angle(neighbourHitPoint - ourPos, dir) / SpatialFilterMaxRadianceHitAngle(10° → rad))
Weight         = PositionWeight × AngleWeight
```

Widening rules: if `ScreenProbeMoving > 0.01` ⇒ `bStrongFilter` (8 more taps at radius 2
and the diagonals) and `bRelaxAngleWeight` (angle weight = 1). If the probe is flagged
disoccluded ⇒ `bRelaxAngleWeight`. The disocclusion flag (`LumenScreenProbeGatherScreenData.usf`,
`SCREEN_PROBE_DISOCCLUSION`) is computed from *last frame's* per-pixel
`FastUpdateMode_NumFramesAccumulated` history: a pixel is "disoccluded" if its reprojected
`NumFramesAccumulated < r.Lumen.ScreenProbeGather.SpatialFilter.DisocclusionMaxFrames (4)`;
the probe is flagged if ≥ `SpatialFilter.DisocclusionFraction (0.4)` of its tile's pixels
are. In words: *where the temporal filter is about to have short history, blur more in
space*.

Then `ScreenProbeConvertToIrradianceCS` produces either a 6×6 octahedral irradiance map
(`r.Lumen.ScreenProbeGather.IrradianceFormat` = 1) or SH3, `ScreenProbeFixupBordersCS` adds
the wrap border, `ScreenProbeGenerateMipLevelCS` builds `GatherNumMips` (1) levels.

### 3.6 Integration to pixels

`ScreenProbeIntegrateCS` (`LumenScreenProbeGather.usf`): for each pixel, interpolation
point = pixel + `FullResolutionJitterWidth (1) × 16 px` blue-noise jitter if the jittered
depth lies on the pixel's plane (`exp2(−1e6·(Δ/depth)²) > 0.01`). Weights for the 4
corner probes (`CalculateUniformUpsampleInterpolationWeights`):

```
bilinear (expanded by 1 px)
× exp2(DepthWeightScale(-10000) × r.Lumen.ScreenProbeGather.InterpolationDepthWeight(1.0)
       × (planeDistance(probePos, pixelPlane) / pixelDepth)²)      // 0.25 for foliage, 0.1 fallback
```

Corners with weight < `MIN_PROBE_INTERPOLATION_WEIGHT` are replaced by the best adaptive
probe in that tile. With `r.Lumen.ScreenProbeGather.StochasticInterpolation` = 1 a single
probe is picked proportionally to the weights (`StochasticScreenProbeSample`), which is
why the result needs the temporal filter and TSR. The pass also outputs
`LightIsMoving = Σ weight_i × ScreenProbeMoving_i` (stored ≥ 1/255 when valid, 0 when the
pixel has no valid probe) — this is the "my lighting comes from a moving object" signal.

### 3.7 Temporal filter on pixels: reprojection, rejection, history length

Two passes. **(a)** History lookup and validity, shared with MegaLights
(`StochasticLighting/StochasticLightingTileClassification.usf`, Lumen branch; results
packed into `FLumenPackedPixelData` bits 0–3 and `EncodedHistoryScreenCoordTexture`):

```
HistoryScreenPosition = GetHistoryScreenPosition(ScreenPosition, ScreenUV, DeviceZ)   // velocity buffer / camera reprojection
HistoryScreenCoord    = HistoryScreenUV × HistoryBufferSize − 0.5  (subpixel-encoded)
bHistoryWasOnScreen   = inside HistoryUVMinMax
ReprojectedSceneDepth = depth of this pixel in the previous view
T  = bFoliage ? Temporal.DistanceThresholdForFoliage (0.03) : Temporal.DistanceThreshold (0.01)
T *= lerp(0.5, 1.5, blueNoise)                       // decorrelate the edge of rejection
Threshold = ReprojectedSceneDepth × T / lerp(0.1, 1.0, saturate(NoV))   // looser at grazing angles
tap i valid  ⇔  |HistoryDepth_i − ReprojectedSceneDepth| < Threshold
             ∧  (Temporal.RejectBasedOnNormal (0) ? dot(HistoryNormal_i, N) ≥ cos(NormalThreshold 45°) : true)
             ∧  foliage flag of history tap == foliage flag of pixel
             ∧  bilinear weight_i > 0.01
```

**(b)** `ScreenProbeTemporalReprojectionCS` (`LumenScreenProbeGatherTemporal.usf`), per
pixel, on the *integrated* diffuse (and rough specular, short-range AO, backface diffuse):

```
W_i          = bilinear_i × valid_i                                   // renormalised over valid taps only
History      = Σ W_i × DiffuseHistory_i / Σ W_i × PrevSceneColorPreExposureCorrection
N_hist       = min(Σ W_i × (NumFramesAccumulated_i + 1) / Σ W_i, MaxFramesAccumulated (10))
moving       = LightIsMoving[pixel]
fast         = saturate(moving × 1 / FractionOfLightingMovingForFastUpdateMode (0.1))
fast         = saturate(min((fast − 0.2) / 0.8, MaxFastUpdateModeAmount (0.9)))
fast         = max(fast, min(Σ W_i × fast_i / Σ W_i, 0.9))         // hysteresis: stays fast where it was fast last frame
N            = min(N_hist, (1 − fast) × 10)
N            = AnyHistoryValid ? N : 0                               // no valid tap ⇒ hard reset to the new sample
alpha        = 1 / (1 + N)                                           // steady state 1/11 ≈ 0.09
if (!currentSampleValid && N ≥ 1) alpha = 1 / (1 + 4N)               // trust history when the new sample is a hole
Out          = lerp(History, New, alpha);  Out = max(Out, 0)
store Out, and pack(fast, N) into 8 bits (4+4, PackFastUpdateModeAmountAndNumFramesAccumulated)
```

Optional neighbourhood clamp of the history to the 3×3 current-frame min/max around
moving objects (`FAST_UPDATE_MODE_NEIGHBORHOOD_CLAMP`, `Temporal.FastUpdateModeUseNeighborhoodClamp` = 0).
History buffers: diffuse, rough specular, backface diffuse (RGB), short-range AO,
`FastUpdateMode_NumFramesAccumulated` (R8), stored per view in `FScreenProbeGatherTemporalState`
(`LumenViewState.h`).

What this buys, in order of importance:

1. **Reprojection** means the history for a pixel is the history of the *surface point*,
   so camera motion does not smear or reset the accumulation.
2. **Per-tap depth rejection** means a disoccluded pixel (new surface behind a moved
   object, screen edge, parallax reveal) drops its history instantly (N = 0 ⇒ alpha = 1)
   and the spatial filter (3.5) takes over for the next 4 frames.
3. **History length is a per-pixel state**, not a constant: it grows 0→10 after a reset,
   is capped by `(1 − fast)·10` where traces hit moving geometry (down to ~1 frame, i.e.
   alpha 0.5), and the cap itself has hysteresis so flicker from the moving test is damped.
4. **Variance is not estimated explicitly**: the "confidence" is `NumFramesAccumulated`
   (how many consistent samples went in) and `LightIsMoving` (whether the signal is
   expected to be non-stationary). There is no neighbourhood variance clamp in the default
   path; Lumen relies on the probe-level spatial filter and the firefly clamp to keep the
   input variance low enough that a 10-frame mean is clean.

### 3.8 What Lumen leaves to TSR/TAA

`FDiffuseIndirectCompositePS` (`IndirectLightRendering.cpp`) multiplies the filtered
`DiffuseIndirect` by the material's diffuse color (plus AO/bent-normal terms) and adds it
to SceneColor; TSR runs later on SceneColor like for any other shading. A grep of
`Engine/Shaders/Private/TemporalSuperResolution` for "Lumen" finds nothing — there is no
GI-specific reactive mask or history channel. What remains for TSR is deliberately
high-frequency and blue-noise shaped:

- blue-noise dithered quantization of every intermediate (`QuantizeForFloatRenderTarget`,
  `LumenFloatQuantization.ush`), including the temporal history itself;
- the 1-of-4 stochastic probe pick (`StochasticInterpolation`) and the ±16 px
  interpolation jitter (`FullResolutionJitterWidth`), both re-rolled per frame with
  `View.StateFrameIndex`;
- half-resolution integration reconstruction (`IntegrateDownsampleFactor` = 2 on lower
  scalability) via the stochastic bilinear offset in `FLumenPackedPixelData`.

All structural denoising — disocclusion handling, moving-light response, probe-level
smoothing, firefly suppression — is done before composite. Turning TAA off makes Lumen
grainy, not unstable.

### 3.9 Screen-probe CVars that matter here (defaults, `LumenScreenProbeGather.cpp` / `LumenScreenProbeFiltering.cpp`)

`r.Lumen.ScreenProbeGather.DownsampleFactor` 16 · `TracingOctahedronResolution` 8 ·
`GatherOctahedronResolutionScale` 1 · `NumAdaptiveProbes` 8 · `AdaptiveProbeAllocationFraction` 0.5 ·
`InterpolationDepthWeight` 1.0 / `.Foliage` 0.25 / `.Fallback` 0.1 ·
`FullResolutionJitterWidth` 1 · `StochasticInterpolation` 1 · `IrradianceFormat` 1 ·
`MaxRayIntensity` 10 · `SpatialFilterProbes` 1 · `SpatialFilterNumPasses` 3 ·
`SpatialFilterHalfKernelSize` 1 · `SpatialFilterMaxRadianceHitAngle` 10 (deg) ·
`SpatialFilterPositionWeightScale` 1000 · `SpatialFilter.DisocclusionMaxFrames` 4 ·
`SpatialFilter.DisocclusionFraction` 0.4 · `TemporalFilterProbes` 0 (`.HistoryWeight` 0.5,
`.HistoryDistanceThreshold` 30) · `Temporal` 1 · `Temporal.MaxFramesAccumulated` 10 ·
`Temporal.MaxRayDirections` 8 · `Temporal.DistanceThreshold` 0.01 /
`.DistanceThresholdForFoliage` 0.03 · `Temporal.NormalThreshold` 45 ·
`Temporal.RejectBasedOnNormal` 0 · `Temporal.FractionOfLightingMovingForFastUpdateMode` 0.1 ·
`Temporal.MaxFastUpdateModeAmount` 0.9 · `Temporal.RelativeSpeedDifferenceToConsiderLightingMoving` 0.005 ·
`Temporal.FastUpdateModeUseNeighborhoodClamp` 0 · `Temporal.ClearHistoryEveryFrame` 0.

---

## 4. Why UzEngine crawls and Lumen does not — the mechanism-level diagnosis

### 4.1 A flat EMA of a noisy estimator never converges; its error wanders

UzEngine (`radiance_cache_trace_cs.hlsl::main`): per update, 32 uniform full-sphere rays
(`SphericalSample`, weight 4π/N), projected to L2 SH, then
`sh = lerp(prev, new, a)`. For i.i.d. per-update noise of variance σ², an EMA with
constant `a` has steady-state variance `σ²·a/(2−a)`:

| a | residual variance | residual std | correlation time ≈ 1/a |
|---|---|---|---|
| 0.05 | 0.026 σ² | 16 % of σ | 20 updates |
| 0.12 | 0.064 σ² | 25 % of σ | 8 updates |

With 32 rays into 9 SH coefficients in a scene with hard shadow edges, σ for the
irradiance is tens of percent of the signal, so the residual is several percent and —
the important part — it is a *slowly varying offset per probe*, independent between
neighbouring probes. Trilinear interpolation of 8 independently wandering probes produces
low-frequency blotches that drift over ~8–20 updates: that is the "crawl". Lowering `a`
shrinks the amplitude but lengthens the drift; it cannot remove it.

The round-robin cursor (`kProbesPerFrame` = 1024 ⇒ each probe updated every 4 frames)
turns each probe's step into a 15 Hz event, squarely in the visible flicker band, and the
cursor sweeping through the grid makes the steps spatially coherent (a "wave").

The per-frame jitter makes it worse rather than better:
`Hammersley(i + (frameJitter & 0xFFFF), nRays + (frameJitter & 0xFF))` changes the
sequence *length* every frame, so successive updates are not complementary strata; the
union over K frames is a random set, not a stratified one. Lumen's screen probes pick
`frame % 8` on a blue-noise sequence sized to the history length (3.2), and Lumen's world
probes do not jitter at all (2.5).

**Lumen's structural answer:** the world cache is a deterministic function of the scene
(2.5) — no estimator, no convergence, no drift. Noise exists only in the screen-probe
tier, where it is (i) importance sampled, (ii) firefly-clamped, (iii) spatially filtered
over neighbours, and only then (iv) averaged over ≤ 10 reprojected frames with a
proper 1/(1+N) running mean that restarts on disocclusion.

### 4.2 Reclassification flicker

Every per-frame decision in UzEngine's probe path (grid re-centering, inside/outside
classification, relocation, "change box replaces history") can flip when the inputs
are noisy or when the camera crosses a cell. Lumen removes the per-frame decisions:

- the lattice is world-locked (`SnapToGrid`); scrolling re-maps existing probes
  (`UpdateCacheForUsedProbesCS`) rather than re-allocating them;
- probe validity / offset are computed once per trace and persisted
  (`ProbeWorldOffset`, `ProbeValid`), never per frame;
- probes that fall out of use survive 8 frames before being freed;
- clipmap selection is a deterministic function of world position (dither 0 for GI);
- the screen-probe temporal state is a per-pixel history length that decays and grows
  smoothly instead of a boolean "valid/invalid".

### 4.3 The grid is used for the wrong distance range

UzEngine's 667 cm probe spacing is the sole source of GI for the near field, so any
occlusion feature smaller than ~7 m is reconstructed by interpolation between probes
that see different things — the interpolation artefact moves with the camera-centered
grid, which reads as shadows crawling. Lumen's finest cell is 104 cm, *and* the cache is
not consulted for the first ~3.6 m of any ray (2.8): contact occlusion, small occluders
and shadow edges come from the receiver's own 64 rays, which have no grid at all.

### 4.4 No screen-space temporal filter on the final gather

UzEngine's SDF final gather is re-evaluated per frame and whatever noise or probe drift it
reads is shown immediately. Lumen's single most effective component against visible
flicker is 3.7: reprojected per-pixel history with depth-plane rejection and a
`NumFramesAccumulated` state. It is also the cheapest to add (one full-screen pass plus
the history-validity pass, three RGB history buffers and one R8).

---

## 5. Adaptation blueprint: two designs

Both assume the goal "no crawl, no reclassification flicker, dynamic lighting still
responds within ~10 frames", and both keep the SDF tracer.

### Design A — Lumen-shaped two tiers

1. **World cache becomes deterministic and persistent.**
   - World-locked lattice: origin = `floor(cam / spacing) × spacing`; store probes in a
     3D ring buffer indexed by `worldCoord mod 16` so a scroll by one cell re-maps 15/16
     of the probes without touching their data (the `UpdateCacheForUsedProbesCS` idea).
   - A probe update traces a **fixed stratified direction set** (e.g. 16×16 or 32×32
     octahedral, texel centres, no jitter) and **replaces** the probe. Store the
     octahedral radiance + hit distance per probe (16×16 × (R11G11B10 + R16) ≈ 1.5 KB,
     32×32 ≈ 6 KB ⇒ 6–25 MB for 4096 probes); derive L2 SH from it on the fly or in the
     same pass if the consumer must stay SH.
   - Scheduling: per-probe `lastTraced`, `lastUsed`; a 16-bucket log2 staleness
     histogram and a cost budget (2.4). Start with budget ≈ 100 probes × 256 rays ≈
     26k rays/frame (today: 131k). New probes always traced (downsampled if needed).
   - `TMin = spacing·√3`; the world probe never traces its own cell.
   - Spatial filter across the 6 neighbours with the two-way depth occlusion test (2.6).
   - Invalidate (budget = ∞ for one frame) on sun/sky intensity change > 4× and on
     UzEngine's change boxes; otherwise never.
2. **Final gather gets Lumen's denoiser.**
   - Receiver rays (per pixel or per 16×16 screen probe): blue-noise direction jitter with
     `frame % 8`; trace at least `2√3·spacing` before consulting the cache; sample the
     cache with sphere parallax + depth atlas.
   - Clamp ray radiance to `MaxRayIntensity`.
   - If gathering at probe granularity: the 3-pass edge-aware spatial filter (3.5).
   - Per-pixel temporal pass exactly as 3.7: velocity reprojection, per-tap depth-plane
     test with `0.01·depth / lerp(0.1,1,NoV)`, `N = min(N_hist+1, 10)`, `alpha = 1/(1+N)`,
     `N = 0` when no tap is valid, `N` capped by a "moving" signal. UzEngine has no hit
     velocity from the SDF, so derive the moving signal from the change boxes it already
     tracks (receivers whose rays enter a change box ⇒ `fast` up).
   - TAA/TSR after composite as today.

Trade-offs: largest change (new atlas layout, scheduling buffers, history buffers,
velocity-based reprojection); needs a velocity buffer or camera-only reprojection
(camera-only is enough for static scenes and still removes the crawl); the far-field
quality and all three artefacts are addressed at the root; directly matches a
known-good shipping design, so tuning values (10 frames, 0.01, 8 directions, 10°,
1000) can be copied.

### Design B — keep the SH grid, make the estimator honest

1. Replace the flat EMA by a **running mean with a per-probe sample count**:
   `alpha = 1/(k+1)`, `k = min(k+1, K)` (K ≈ 32); reset `k` only when the probe's world
   cell changes (world-locked ring buffer as in A.1) or a change box covers it. The mean
   now converges instead of wandering.
2. Make successive updates **complementary**: one global 1024-direction stratified set,
   update `j` traces slice `j mod 32` (32 directions) for every probe scheduled that
   frame. After 32 updates a probe has traced the full set exactly once; a static scene
   then re-traces the same slice to the same value, so the converged mean stops moving.
   (Equivalently: store per-slice SH, 32 × 27 floats ≈ 3.5 KB/probe, and replace slices —
   which is Design A's atlas in SH clothing.)
3. Add the per-pixel temporal filter of 3.7 on the final gather output, and the firefly
   clamp on gather rays.
4. Keep camera-centering only as a window over the world-locked ring buffer.

Trade-offs: small diff, keeps SH and every consumer; removes the drift (1, 2) and the
per-frame gather noise (3); does **not** fix the near-field use of a 667 cm grid (4.3) or
leaking through walls, so shadows will still be soft and grid-aligned, just no longer
crawling. Cost is unchanged.

### Recommendation

Do **A, in two phases, with phase 1 = Design B**. B's four items are a strict subset of A
(world-locked ring buffer, converged/deterministic probe content, per-pixel temporal
filter, firefly clamp) and they attack the actual crawl — a non-converging estimator plus
an unfiltered gather — with the smallest diff; ship and measure them first. Phase 2 is the
part only A has: `TMin` near-field exclusion with receiver rays covering the first
`2√3·spacing`, a finer (clipmapped) lattice, and parallax + depth-aware sampling. That phase
is what turns "stable but blurry grid shadows" into Lumen-like contact occlusion.

Boundaries assumed: the SDF tracer stays; the deferred consumer can keep reading SH
during phase 1; a reprojection source exists (velocity buffer, or camera-only
reprojection as a first step). Risks: history-buffer memory at full resolution (3 × RGB +
R8); a camera-only reprojection will ghost on moving objects until a velocity buffer and
a moving signal exist (Lumen's `fast` mode is what covers that); copying `0.01·depth` as
the rejection threshold assumes depth precision comparable to UE's reverse-Z — verify on
UzEngine's depth format before trusting the constant.

---

## 6. Source index

| Topic | File (relative to `E:/UnrealGITHUB/Engine/`) | Symbols |
|-------|-----|---------|
| Clipmap geometry, TMin, state reset | `Source/Runtime/Renderer/Private/Lumen/LumenRadianceCache.cpp` | `FClipmapGeometry::SnapToGrid`, `UpdateRadianceCacheState`, `UpdateRadianceCaches` |
| GI radiance-cache config | `.../Lumen/LumenScreenProbeGather.cpp` | `SetupRadianceCacheInputs`, `GRadianceCache*`, `GLumenRadianceCache*` |
| Probe reuse, priority, trace allocation | `Shaders/Private/Lumen/LumenRadianceCacheUpdate.usf` | `UpdateCacheForUsedProbesCS`, `GetPriorityBucketIndex`, `AllocateUsedProbesCS`, `SelectMaxPriorityBucketCS`, `AllocateProbeTracesCS` |
| Cost model, packed probe data | `.../Lumen/LumenRadianceCacheUpdate.ush` | `GetProbeTraceCost`, `FAllocatedProbeData` |
| Marking used probes | `.../Lumen/LumenRadianceCacheMarkCommon.ush` | `MarkPositionUsedInIndirectionTexture` |
| Tracing, BRDF-PDF tiles, spatial filter, occlusion, borders | `.../Lumen/LumenRadianceCache.usf` | `TraceFromProbesCS`, `GenerateAdaptiveProbeTraceTilesCS`, `ScatterScreenProbeBRDFToRadianceProbesCS`, `FilterProbeRadianceWithGatherCS`, `ComputeProbeWorldOffsetsCS`, `PrepareProbeOcclusionCS`, `FixupBordersAndGenerateMipsCS` |
| Sampling, parallax, coverage, depth encoding | `.../Lumen/LumenRadianceCacheInterpolation.ush`, `LumenRadianceCacheCommon.ush` | `GetRadianceProbeClipmap`, `GetRadianceCacheCoverage`, `SampleRadianceCacheProbeWithParallaxCorrection`, `SampleRadianceCacheInterpolated`, `EncodeProbeDepth` |
| Global lighting change trigger | `.../Lumen/LumenSceneRendering.cpp` | `UpdateGlobalLightingState` |
| Screen probe layout, jitter, ray flags | `.../Lumen/LumenScreenProbeCommon.ush`, `LumenScreenProbeTracingCommon.ush` | `GetScreenTileJitter`, `GetProbeTexelCenter`, `IsTraceMoving`, `EncodeProbeRayDistance` |
| Screen probe tracing → cache hand-off | `.../Lumen/LumenScreenProbeTracing.usf` | `GetRadianceCacheCoverage`, `SampleRadianceCacheAndApply` |
| Composite, moving, spatial filter, irradiance, mips | `.../Lumen/LumenScreenProbeFiltering.usf` + `.cpp` | `ScreenProbeCompositeTracesWithScatterCS`, `ScreenProbeCalculateMovingCS`, `ScreenProbeTemporallyAccumulateTraceRadianceCS`, `ScreenProbeFilterGatherTracesCS`, `ScreenProbeConvertToIrradianceCS` |
| Disocclusion flag for the spatial filter | `.../Lumen/LumenScreenProbeGatherScreenData.usf` | `SCREEN_PROBE_DISOCCLUSION` block |
| Interpolation and integration | `.../Lumen/LumenScreenProbeGather.usf` + `.cpp` | `CalculateUniformUpsampleInterpolationWeights`, `ScreenProbeIntegrateCS`, `DepthWeightScale` |
| History reprojection and tap validity | `Shaders/Private/StochasticLighting/StochasticLightingTileClassification.usf`, `StochasticLightingCommon.ush` | Lumen depth/normal/material tests, `FLumenPackedPixelData` |
| Per-pixel temporal filter | `.../Lumen/LumenScreenProbeGatherTemporal.usf` + `.ush` | `ScreenProbeTemporalReprojectionCS`, `PackFastUpdateModeAmountAndNumFramesAccumulated` |
| Composite into scene color | `Source/Runtime/Renderer/Private/IndirectLightRendering.cpp` | `FDiffuseIndirectCompositePS` |
| UzEngine counterpart | `uzengine/runtime/engine/renderer/gi/radiance_cache.h`, `uzengine/.gi-shaders/gi/radiance_cache_trace_cs.hlsl` | `kProbesPerAxis`, `kRaysPerProbe`, `kTemporalBlendAlpha`, `kProbesPerFrame`, `main` |
