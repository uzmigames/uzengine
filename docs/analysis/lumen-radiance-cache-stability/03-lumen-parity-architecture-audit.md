# 03 - Lumen parity architecture audit (lighting stability)

Date: 2026-11-02. Branch `feat/soft-shadows-area-lights`, HEAD `70813e96`
("enable the sphere-crawl fixes by default: StableTemporal + RoughReconstruct ON").
Reference: the full UE source at `E:/UnrealGITHUB` (`Engine/Build/Build.version` = UE 6.0,
branch `UE6`, last commit `b841fe70` 2026-07-24). Lumen file paths below are relative to
`Engine/Source/Runtime/Renderer/Private/Lumen/` (C++) and `Engine/Shaders/Private/Lumen/` (usf).
UzEngine paths are relative to `uzengine/`.

Read-only static audit. No code was changed, nothing was built or captured. Every number is
a default read from source (`UZ_CVAR_*` / `TAutoConsoleVariable`), not a measurement.

## 0. Verdict first

**Qualified yes - the lighting direction is right, but the stability direction is half wrong.**

1. The components are the right ones and map 1:1 onto Lumen's (section 1). The stack is not
   a fork of Lumen, but every Lumen layer has a UzEngine counterpart or a planned one.
2. Fixing the radiance cache's temporal crawl **is the right next move**, because the probe SH
   is upstream of everything: the final gather reads it at every ray hit
   (`gi/sdf_reflections.hlsli: ShadeSdfReflectionHit -> SdfReflectionIrradiance`), the RT
   reflections read it as their indirect term, the DDGI writer and the surface cache feed it
   forward. Any wander in the probes reaches the screen through four paths at once, and the
   per-pixel temporal filter downstream cannot remove it (it is low-frequency and spatially
   coherent, so it is indistinguishable from a real lighting change).
3. But the **fix must be Lumen's fix, not a longer EMA**. Lumen's world radiance cache has no
   EMA at all: a probe is retraced wholesale with 1024 rays, spatially filtered with its
   neighbours, written, and then held constant until the update scheduler picks it again
   (`LumenRadianceCache.usf: TraceFromProbesCS` writes `RWRadianceProbeAtlasTexture` directly;
   `LumenRadianceCacheUpdate.usf` schedules by `ProbeLastTracedFrame`/`ProbeLastUsedFrame`
   through a priority histogram; the budget is `NumProbesToTraceBudget` = 100 probes/frame).
   UzEngine blends 32-ray estimates with alpha 0.12 over 1024 probes/frame
   (`gi/radiance_cache.h: kRaysPerProbe = 32, kTemporalBlendAlpha = 0.12f,
   kProbesPerFrame = 1024`). Same order of ray budget, opposite distribution: Lumen makes a
   few probes clean and leaves them alone; UzEngine keeps every probe slightly noisy forever.
   That is the crawl, by construction.
4. The deeper structural gap is real and is **not** "a missing temporal filter". UzEngine
   already has *more* temporal filtering than Lumen (64-frame cap, unbounded in stable mode,
   versus Lumen's `r.Lumen.ScreenProbeGather.Temporal.MaxFramesAccumulated` = 10). What is
   missing is Lumen's **variance reduction before integration**: a probe-space gather
   (64 importance-sampled directions per probe), spatial filtering of *radiance* between
   neighbouring probes gated by hit consistency, then per-pixel integration, then a *short*
   temporal filter. UzEngine filters *irradiance* after integration with a receiver-plane
   kernel and then leans on history length. Extending that history (the `StableTemporal`
   direction) diverges from Lumen; it trades crawl for lag and ghosting instead of removing
   the noise where it is produced.

The two highest-leverage moves, in order, are in section 4.

## 1. Architecture map

| UzEngine component (file) | Lumen counterpart (file) | Status | Where it diverges |
|---|---|---|---|
| World radiance cache: fixed 16^3 L2-SH probe grid, camera-centred, SDF-traced, 32 rays/probe, EMA 0.12, 1024 probes/frame (`gi/radiance_cache.h/.cpp`, `shaders/runtime/gi/radiance_cache_trace_cs.hlsl`) | World Radiance Cache: sparse clipmapped octahedral probes, 4 clipmaps x 48^3 grid, first extent 2500 cm, 32x32 directions per probe, 100 probes/frame, no EMA, spatial filter between probes, parallax-corrected interpolation (`LumenRadianceCache.cpp`, `LumenRadianceCacheInterpolation.h`, `LumenRadianceCache.usf`, `LumenRadianceCacheInterpolation.ush`) | **Equivalent role, divergent mechanism** | Storage (SH L2 vs 32x32 octahedral radiance), allocation (dense fixed grid vs probes marked by the gather's rays), update policy (EMA of noisy estimates vs clean retrace + hold), consumption (every hit vs far field only beyond `TMin + cellSize*sqrt(3)`), density (667 cm spacing at the default 5000 cm extent, 267 cm on Sponza, vs ~104 cm in clipmap 0). |
| Final gather: half-res per-pixel 8 cosine SDF rays, 5x5 receiver-plane spatial filter on irradiance, geometric temporal accumulation, joint-bilateral upsample (`gi/ssgi_renderer.*`, `shaders/runtime/gi/sdf_diffuse_gather.hlsli`, `shaders/runtime/ssgi/ssgi_trace_cs.hlsl`, `ssgi_spatial_denoise_cs.hlsl`, `ssgi_temporal_accum_cs.hlsl`, `ssgi_stable_temporal_accum_cs.hlsl`, `ssgi_upsample_cs.hlsl`) | Screen Probe Gather: probes every 16 px plus adaptive probes, 8x8 octahedral traces per probe with BRDF x lighting importance sampling, screen trace -> mesh SDF -> global SDF -> radiance cache, probe-space spatial filter (3 passes, 3x3, hit-angle gated), convert to irradiance, per-pixel stochastic plane-weighted interpolation with a jittered probe grid, pixel-space temporal (max 10 frames, fast-update mode), ShortRangeAO for contact detail (`LumenScreenProbeGather.cpp`, `LumenScreenProbeTracing.cpp`, `LumenScreenProbeFiltering.cpp`, `LumenScreenProbeImportanceSampling.cpp`, `LumenScreenProbeGatherTemporal.usf`) | **Same role, structurally different** | This is the crux; see section 2. UzEngine has no probe-space stage: no sample sharing before integration, no importance sampling, no screen-trace-first stage in world mode (the world gather returns before the screen march, see `docs/analysis/lighting/uzengine/lumen-gap-analysis-2026-09-17.md` G4), filtering on irradiance instead of radiance, long temporal instead of short. |
| Legacy screen-space SSGI (previous-HDR march, `ssgi_trace_cs.hlsl` screen branch) | Screen traces inside the Screen Probe Gather (`LumenScreenProbeTracing.cpp: TraceScreen`, HZB traversal) | **Partial** | In Lumen, screen traces are the *first* stage of every probe ray and hand unresolved rays to the SDF; in UzEngine they are an either/or mode selected by `r.SSGI` vs `r.GI.FinalGather`. |
| Screen-space ReSTIR GI (`shaders/runtime/ssgi/ssgi_restir_*`, `r.GI.ReSTIR`, default 0) | ReSTIR Gather (`LumenReSTIRGather.cpp`, `ELumenFinalGatherMethod::ReSTIRGather`, non-default alternative to the screen probe gather) | **Equivalent, both optional** | Lumen ships it as an *alternative* final gather, not as a layer under the screen probe gather. UzEngine's is likewise a replacement for the plain trace. Neither is on the default path. |
| DDGI: 16^3 octahedral irradiance (6x6) + depth (16x16) atlases, Chebyshev visibility, TLAS traced (`gi/ddgi_system.*`, `shaders/runtime/gi/ddgi_probe_*`) | Irradiance Field Gather (`LumenIrradianceFieldGather.cpp`: radiance-cache probes with irradiance + occlusion atlases, probe offsets, adaptive placement) - experimental, non-default | **Equivalent to a Lumen side path, not to its main path** | Lumen does not ship DDGI-style probes as the final gather. In UzEngine, DDGI sits *above* the radiance cache in the deferred priority chain (DDGI > RC > LPV > sky SH, `deferred_lighting_ps.hlsl`) when the final gather is off. It shares the radiance cache's grid, and its hit shading reads the same probe SH (`DdgiIndirectTerm`), so it inherits the crawl. |
| Author-placed irradiance volume (`gi/irradiance_probe_grid.*`) | No direct Lumen counterpart (Lumen has no authored probe volumes; sky-only fill is the fallback) | **UzEngine-only** | Static fallback; fine as a tier, irrelevant to parity. |
| Surface radiance cache: world-space spatial hash of lit hit radiance, exact material, EMA 32 frames, stale eviction 64, relight slices, cell-indirect Jacobi (`gi/surface_radiance_cache.*`, `shaders/runtime/gi/surface_cache_*`, `r.GI.SurfaceCache` default 0) | Lumen Scene + Surface Cache: mesh cards, atlas pages with albedo/normal/emissive/depth, direct lighting per card tile, radiosity for indirect with `RadiosityNumFramesAccumulated`, feedback-driven residency (`LumenSceneData.h`, `LumenSceneLighting.h`, `LumenSurfaceCache.cpp`, `LumenRadiosity.cpp`, `LumenSurfaceCacheFeedback.h`, `LumenMeshCards.h`) | **Same purpose, different representation; off by default in UzEngine** | Lumen's cache is a parameterised *surface* representation (cards) that stores material and is lit by its own passes, so every ray returns cached lit radiance at any hit distance. UzEngine's is a hash keyed by position/normal filled by TLAS writers; with it off (the default), SDF hits are shaded with the clipmap's per-mesh-average albedo + one sun shadow ray + probe SH (`ShadeSdfReflectionHit`). Covered as G1/G3 in `lumen-gap-analysis-2026-09-17.md`; still the largest *quality* gap. |
| Global distance field clipmaps + mesh SDFs (`gi/global_distance_field.*`, `gi/mesh_sdf_*`) | Global SDF + mesh SDF tracing (`Lumen.h: UseGlobalSDFTracing / UseMeshSDFTracing`, `LumenMeshSDFCulling.cpp`) | **Equivalent** | Lumen traces mesh SDFs near the receiver before the global SDF; UzEngine's gather traces only the global clipmaps (G5 in the prior gap analysis). |
| HWRT reflections: full-res RT with SSR hole-fill or standalone, SSR temporal + spatial denoise, rough reconstruction, optional unified denoiser (`passes/raytracing/rt_reflection_pass.h`, `shaders/runtime/reflections/rt_reflection_cs.hlsl`, `passes/gi/rough_specular_reconstruction.cpp`, `passes/gi/realtime_denoiser.cpp`, `r.RayTracing.Reflections` default 1) | Lumen Reflections: downsampled tracing, screen traces first, radiance cache for rough lobes, screen-space BRDF reweighting reconstruction (5 samples, 8 px), temporal (12 frames, neighbourhood clamp, 1024 ray directions), bilateral (4 samples, 8 px), tonemapped denoising range, rough > cutoff served by the screen probe gather's rough specular (`LumenReflections.cpp`, `LumenReflections.h`) | **Equivalent and converging** | `RoughReconstruct` is the counterpart of Lumen's `ScreenSpaceReconstruction`; UzEngine lacks the "very rough -> diffuse gather's rough specular" handoff and the radiance-cache fallback for rough lobes. Stability design here is already Lumen-shaped (short temporal + spatial reconstruction). |
| PCSS / LTC area lights, CSM | Virtual shadow maps + Lumen scene direct lighting (`LumenSceneDirectLighting.cpp`) | Direct lighting, outside this audit | Lumen's direct lighting for *hits* lives in the surface cache; UzEngine's hit lighting is sun + K local lights with shadow rays only when the surface cache is on. |
| DXR path tracer as oracle (`passes/pathtracing/*`, `r.PathTracer.*`) | UE Path Tracer (outside Lumen) | Equivalent | Good: both engines validate GI against a path tracer. |
| Final integrator: TAA (`r.TAA`, **default false**), TSR/FSR/DLSS via `IUpscaler` | TSR is assumed; Lumen's noise budget is sized for it (`StochasticInterpolation` = 1, `FullResolutionJitterWidth` = 1 rely on temporal supersampling) | **Present but not in the stability budget** | If the sphere-crawl captures run with `r.TAA=0`, the gather's output reaches the screen with no final temporal integrator; Lumen never runs that way. |

## 2. The stability layer, side by side

### 2.1 Where variance is produced and where it is removed

```
LUMEN (defaults)                                   UZENGINE (defaults, all-GI-on set)
----------------------------------------------     ------------------------------------------------
Surface cache: lit radiance at hits, accumulated    Hit shading inline: GDF-average albedo x
in card space over frames (low variance source)     (sun x 1 shadow ray + probe SH)    [noisy source]
World RC: 1024 rays/probe, spatial filter,          World RC: 32 rays/probe, EMA 0.12, white-noise
replace + hold; far field only (beyond TMin)        per-frame rotation; read at EVERY hit
Screen probes @1/16 res: 64 importance-sampled      Per-pixel @1/2 res: 8 cosine rays, per-pixel
directions, screen trace first, max ray intensity   per-frame Hammersley rotation, no intensity clamp
clamp 10
Probe-space spatial filter: 3 passes, 3x3 probes,   (none in radiance space)
only where the neighbour's hit lies within 10 deg
of the same direction (keeps contact shadows)
Convert to irradiance (6x6 oct or SH3)              Integrated in the trace (E/pi per pixel)
Per-pixel integrate: stochastic 1-probe sample,     Spatial: 5x5 receiver-plane filter on E/pi
plane-weighted, probe grid jittered up to 16 px     (blurs irradiance, correlates residual noise)
Pixel temporal: alpha = 1/(1+N), N <= 10, plane     Pixel temporal: alpha = max(1/N, 0.05), N <= 64;
(1%) + normal (45 deg) rejection, fast-update       stable mode: N unbounded after 16 quiet frames,
mode when TRACES hit moving geometry                receiver velocity + 3-sigma innovation gate
ShortRangeAO at full res for contact detail         GTAO / mesh AO
TSR                                                 TAA (default off)
```

The two pipelines spend comparable ray budgets at 1080p but in opposite places:

| Budget | Lumen | UzEngine |
|---|---|---|
| Gather rays / frame | ~8.1k uniform probes (+ up to 50 % adaptive) x 64 = 0.5-0.8 M | 960x540 half-res pixels x 8 = 4.1 M SDF sphere traces |
| Rays per receiver estimate before any filtering | 64 per probe, importance sampled | 8 per half-res pixel, cosine |
| Samples shared before integration | 3 passes x 3x3 probes of radiance (hit-gated) | none (irradiance filtered after) |
| World RC rays / frame | 100 probes x 1024 = ~100 k | 1024 probes x 32 = ~33 k |
| World RC noise per probe | one clean estimate, then constant until retraced | continuous: a new 32-ray estimate blended in every 4 frames |
| Pixel temporal length | 10 frames (12 for reflections) | 64 frames; unbounded in stable mode |

UzEngine spends roughly six times more gather rays than Lumen and is noisier per receiver,
because none of those rays are shared across receivers before integration and because each
ray's return value carries the probe grid's noise. Lumen's per-pixel estimate, by the time it
reaches the temporal filter, has already been averaged over 64 x 9 directions and fed by a
radiance cache with zero per-frame noise. That is why Lumen can afford a 10-frame history
and UzEngine cannot.

### 2.2 Is UzEngine missing the "screen-probe temporal + spatial filter" layer?

Yes, but the label is misleading. Lumen's `r.Lumen.ScreenProbeGather.TemporalFilterProbes`
(the *probe-space* temporal filter) is **off by default** (`LumenScreenProbeGather.cpp:
GLumenScreenProbeTemporalFilterProbes = 0`). The layer that kills flicker is:

1. **Probe-space spatial filtering of radiance** (`LumenScreenProbeFiltering.usf:
   FilterRadianceWithGather`, `SpatialFilterMaxRadianceHitAngle` = 10 deg,
   `SpatialFilterNumPasses` = 3, `SpatialFilterHalfKernelSize` = 1). A neighbouring probe's
   texel is only blended in when the neighbour's hit point, seen from this probe, lies within
   10 degrees of this texel's direction. This averages noise without averaging away contact
   shadows or leaking across depth discontinuities. UzEngine's 5x5 receiver-plane filter on
   E/pi (`ssgi_spatial_denoise_cs.hlsl`) has no equivalent gate; it can only use receiver
   geometry, so it must stay small and it blurs the signal it is supposed to denoise.
2. **Importance sampling** of the 64 directions from the BRDF and last frame's probe radiance
   (`LumenScreenProbeImportanceSampling.cpp`, `ImportanceSamplingHistoryScreenProbeRadiance`).
   This is temporal *reuse* in probe space without blending: the history steers where new rays
   go, so it cannot ghost.
3. **Radiance cache for far rays** with no per-frame noise (section 0, point 3).
4. **A short pixel-space temporal** with content-aware response: `ScreenProbeMoving` is
   computed from whether the *traces* hit moving geometry
   (`LumenScreenProbeFiltering.usf: ScreenProbeCalculateMovingCS`), and drives a fast-update
   mode that shortens history (`LumenScreenProbeGatherTemporal.usf:
   NewNumFramesAccumulated = min(N, (1 - FastUpdateModeAmount) * MaxFramesAccumulated)`).
5. **Stochastic interpolation and a jittered probe grid** (`StochasticInterpolation` = 1,
   `FullResolutionJitterWidth` = 1) hand the remaining noise to TSR as high-frequency,
   uncorrelated noise, which temporal AA is good at removing. UzEngine's residual is
   low-frequency and spatially coherent (an EMA random walk filtered by a 10x10 full-res
   kernel), which temporal AA cannot distinguish from lighting change.

So the honest statement is: UzEngine lacks items 1, 2, 3 and 5, and has an *overgrown*
version of item 4. `r.GI.FinalGather.StableTemporal` adds a variance-guided unbounded history
(`ssgi_stable_temporal.hlsli: SsgiStableCount` removes the 64-frame cap after 16 compatible
observations). It will reduce the measured standard deviation on the fixed-camera sphere
ROIs, and the rulebook design already flags the cost (lag and ghosting under camera or light
motion, `.rulebook/tasks/gi-diffuse-gather-temporal-reconstruction/design.md`, Risks). It is a
measurement-driven stopgap, not a step toward Lumen's layer.

### 2.3 Why the radiance cache crawls, from the code

`shaders/runtime/gi/radiance_cache_trace_cs.hlsl` (HEAD):

- 32 uniform full-sphere rays per probe update, projected to L2 SH with weight `4*pi/N`. A
  single ray landing on a sun-lit surface contributes `radiance * 4*pi/32` to the DC term;
  there is no intensity clamp (Lumen clamps screen-probe rays at `MaxRayIntensity` = 10 with
  pre-exposure; UzEngine clamps only in the surface cache, `maxRadiance` = 4096, and uses a
  Karis weight only in reflections).
- Per-frame rotation: `frameJitter = frameIndex * 1664525 + 1013904223`, then
  `RadicalInverseVdC(frameJitter)` and `RadicalInverseVdC(frameJitter ^ probeIdx)`. An LCG
  state through a bit reversal is white noise frame to frame. The project's own learning
  (`.rulebook/learnings/2026-07-27T17-35-23-ema-blended-gi-probes-random-walk-...md`) measured
  exactly this regime as a random walk (frame diff growing monotonically with temporal
  distance, d1 = 1.07 -> d40 = 3.57) and fixed it with a 16-phase Cranley-Patterson cycle. The
  comment at the loop says "since H-57 the DDGI writer uses this same construction", so the
  current sequence is a later change. **To verify before acting:** whether H-57 reintroduced
  the white-noise rotation that the 2026-07-27 learning removed. If it did, that alone
  explains a large part of the crawl.
- EMA `a = temporalBlendAlpha` (0.12) against the previous completed buffer; the whole grid is
  touched every 4 frames. The EMA of an i.i.d. noisy input never settles: it random-walks with
  amplitude about `sigma * sqrt(a / (2 - a))` ~ 0.25 sigma, where sigma is the 32-ray estimate's
  noise. A periodic (cycled) input turns that into a stationary limit cycle of amplitude ~ a *
  sigma; a clean estimate replaced wholesale (Lumen) turns it into zero between retraces.
- Consumption at every hit (`SdfReflectionIrradiance` in `ShadeSdfReflectionHit`) with
  normal-weighted trilinear interpolation of a 267 cm grid (Sponza) means that on a curved
  diffuse sphere, neighbouring receivers' rays land on nearby hit points that interpolate the
  *same* eight wandering probes: the wander is spatially coherent across the sphere, which is
  the "crawl" signature rather than sparkle. Lumen never uses the cache this close: a ray
  consults it only past `MinTraceDistanceBeforeInterpolation = TMin + cellSize * sqrt(3)`
  (`LumenRadianceCacheCommon.ush`), and reprojects the direction onto the probe's TMin sphere
  (`SampleRadianceCacheProbeWithParallaxCorrection`).

None of this is fixed by the pixel-space temporal filter downstream, by `StableTemporal`, or
by `RoughReconstruct`: those run after the noise has already been integrated into a smooth,
coherent field.

### 2.4 Is fixing the radiance cache the right priority, or is the screen-probe layer the deeper gap?

Both are true and they are not in conflict:

- The radiance cache is the **right first fix** because it is the common upstream source for
  the gather, the RT reflections' indirect term (`kRTReflectionFlagIndirect`), the DDGI
  indirect term and the surface cache feed, and because the Lumen-shaped fix is small: trace a
  probe fully, filter it with its neighbours, replace it, hold it, schedule by age. It does
  not require new systems.
- The screen-probe layer is the **deeper gap** and the destination. Even with a perfectly
  still radiance cache, the per-pixel 8-ray gather with irradiance-space filtering will keep a
  noise floor that only long history can hide, and long history is the wrong tool. A
  Lumen-shaped gather also *needs* a stable far-field cache (it reads it for every ray that
  leaves the near field), so the radiance cache fix is a prerequisite for it, not a detour.

What does not converge: investing further in longer per-pixel history (`StableTemporal` beyond
its current state, higher caps, more confidence heuristics). Every frame of history added
there is a frame of lag Lumen does not have.

## 3. Gaps and risks, ranked by impact on final-image stability and quality

Convergence column: does the current direction (stabilise the radiance cache and the gather)
move toward (+), away from (-), or orthogonally to (0) Lumen on this item.

| # | Gap | Impact | Evidence | Convergence |
|---|---|---|---|---|
| 1 | **Radiance cache update policy**: EMA of 32-ray estimates, white-noise rotation, no intensity clamp, probes read at every hit distance | Stability (dominant source of low-frequency crawl in diffuse GI, reflections' indirect term, DDGI) | `radiance_cache_trace_cs.hlsl` hysteresis block and ray loop; `radiance_cache.h` constants; Lumen `LumenRadianceCache.usf`, `LumenRadianceCacheUpdate.usf`, `LumenRadianceCacheCommon.ush` | + if fixed Lumen's way (retrace/replace/hold, clamp, far-field-only reads); - if fixed by lowering alpha further |
| 2 | **No probe-space gather**: no sample sharing before integration, filtering on irradiance with a receiver-only kernel, no importance sampling, no screen-trace-first stage | Stability and quality (noise floor that forces long history; blurred contact GI; 6x the ray cost of Lumen for a noisier estimate) | `sdf_diffuse_gather.hlsli`, `ssgi_spatial_denoise_cs.hlsl`, `ssgi_trace_cs.hlsl`; Lumen `LumenScreenProbeGather.cpp` defaults, `LumenScreenProbeFiltering.usf` | - (StableTemporal lengthens history instead) |
| 3 | **Long pixel-space temporal as the primary stabiliser**: 64-frame cap, unbounded in stable mode, versus Lumen's 10 frames with trace-driven fast-update | Stability under motion (lag, ghosting, slow light-change response); hides rather than removes noise | `ssgi_stable_temporal_accum_cs.hlsl`, `ssgi_stable_temporal.hlsli`; Lumen `LumenScreenProbeGatherTemporal.usf` lines 486-531 | - |
| 4 | **Surface cache off by default; SDF hits shaded with per-mesh-average albedo + one shadow ray + probe SH** | Quality (no texture detail, no local lights or emissive at hits unless the cache is on) and stability (the shadow ray and the probe term are fresh noise every frame; Lumen returns accumulated card lighting) | `ShadeSdfReflectionHit`; `r.GI.SurfaceCache` default 0; Lumen `LumenSceneLighting.h`, `LumenRadiosity.cpp` | 0 (Phase D built the mechanism; the default is the gap) |
| 5 | **Probe density and placement**: fixed dense 16^3 grid at 667 cm (default) / 267 cm (Sponza) spacing, no sparse allocation, no adaptive probes, no probe offsets from geometry beyond the guard/relocation rule | Quality (leaking, coarse multi-bounce) and stability (few probes carry each receiver, so each probe's wander is visible) | `radiance_cache.h` grid constants, `rc_probe_guard.hlsli`; Lumen `GridResolution` = 48 per clipmap, `UseAdaptiveProbes`, `ProbeWorldOffset`, `IrradianceProbeOffsetMode` | 0 |
| 6 | **No final temporal integrator in the stability budget** (`r.TAA` default false) | Stability of the shipped image; Lumen's stochastic interpolation and jitter assume TSR | `frame/frame_setup.cpp: cv_TAA`; Lumen `StochasticInterpolation`, `FullResolutionJitterWidth` | 0 |
| 7 | **Reflections: no rough-lobe handoff** to the diffuse gather's rough specular or to the radiance cache; full-res tracing | Cost and rough-metal stability (addressed partly by `RoughReconstruct`) | `rt_reflection_pass.h`, `rough_specular_reconstruction.cpp`; Lumen `LumenReflections.cpp` `RadianceCache.*`, `MaxRoughnessToEvaluateRoughSpecular` = 0.8 in the gather | + |
| 8 | **DDGI as a priority tier above the radiance cache** when the gather is off, sharing the same grid and probe SH | Confusing fallback semantics; inherits gap 1 | `deferred_lighting_ps.hlsl` priority chain; Lumen keeps the irradiance-field path non-default | 0 |
| 9 | **No mesh-SDF near-field tracing** in the gather | Quality (thin walls, detail), not stability | prior `lumen-gap-analysis-2026-09-17.md` G5 | 0 |

Risks of the current direction if it continues unchanged:

- The sphere-crawl harness is a fixed camera over frames 90-149 and 600-659. A longer history
  always wins that measurement. It does not measure the lag and ghosting that the same
  history introduces under camera or light motion; the rulebook design already lists that as
  the main risk. A stabiliser tuned on that harness alone will pass and then regress the
  moving case.
- Lowering the radiance cache alpha (0.12 -> 0.05) reduces random-walk amplitude by
  `sqrt(0.05/1.95) / sqrt(0.12/1.88)` ~ 0.63x and lengthens the light-change response to
  roughly 80 frames. It does not remove the walk. Cycling the rotation (the 2026-07 learning)
  bounds it; replacing the EMA removes it.
- A Lumen-shaped radiance cache (fewer, cleaner probes, held between retraces) changes the
  failure mode from crawl to occasional pops on retrace. Lumen hides those under the 10-frame
  pixel temporal and TSR, and by never reading the cache in the near field. UzEngine reading
  the cache at every hit would make pops visible until gap 1's "far-field-only" part is also
  done, so the two halves of the fix should ship together or with the alpha kept as a
  short fade (`regionBlendAlpha`-style) rather than as the steady state.

## 4. Verdict and next moves

**Yes, qualified.** The component set, the SDF/HWRT split, the surface cache, the path-tracer
oracle and the reflection denoiser are the right pieces and they map onto Lumen's. The
stabilisation work of the last week is aimed at the right symptom but half of it (longer
history) is aimed in the wrong direction. Two designs were weighed:

- **A. Stabilise in place**: keep the per-pixel gather; fix the radiance cache (retrace,
  replace, hold, clamp, cycle); keep extending `StableTemporal`. Cheapest; converges on the
  fixed-camera metric; diverges from Lumen under motion; leaves the noise floor and the 6x
  ray cost in place; no path to probe-space filtering later without redoing the gather.
- **B. Lumen-shaped gather on top of a Lumen-shaped cache**: fix the radiance cache the same
  way, then restructure the gather into probes (1/8 or 1/16 res, 8x8 octahedral directions,
  screen trace first, SDF after), filter radiance between probes with a hit-angle gate,
  integrate per pixel with plane weights and jitter, shorten the pixel temporal to ~10 frames
  with a trace-driven fast-update mode, and keep TAA/TSR on. Larger change (a new pass
  structure in `ssgi_renderer.cpp` and three or four shaders), but it reuses the existing SDF
  tracer, hit shading, surface cache and history reprojection, and it is the only design
  where cost goes down while stability goes up.

**Recommendation: B, executed as A's first step followed by B's gather.** The radiance cache
fix is identical in both and is a prerequisite for B, so it is the right thing to do now.

Highest-leverage moves, in order:

1. **Radiance cache: replace the EMA with Lumen's retrace-replace-hold policy** in
   `radiance_cache_trace_cs.hlsl` / `radiance_cache.cpp`: trace a scheduled subset of probes
   with a full ray set (256-1024 rays, stratified, fixed set with a cycled rotation), clamp ray
   intensity (pre-exposed, as Lumen's `MaxRayIntensity`), spatially filter against the six
   neighbours, write wholesale, and leave the probe alone until its age or an invalidation
   (`InvalidateHistory`, `RetraceKeepingHistory`, the recentre shift) schedules it again. Keep
   the existing recentre/history-shift and change-box machinery; it is the equivalent of
   Lumen's clipmap `VolumeUVOffset` and is already correct. Validate on the existing
   sphere-crawl harness *and* a moving-camera / light-change capture, since the failure mode
   changes from crawl to pops. Also check H-57 against the 2026-07-27 learning (section 2.3).
2. **Stop reading the probe SH in the near field.** Add a minimum hit distance (a multiple of
   the probe spacing, Lumen's `TMin + cellSize*sqrt(3)`) below which `ShadeSdfReflectionHit`
   uses the surface cache or the inline sun term only, and read the probe grid only beyond
   it. This removes the coherent probe wander from curved near-field receivers directly, and
   it is the contract a Lumen-shaped gather needs anyway. With it, the probe grid can also
   become sparser and coarser without visible leaking, freeing budget for move 1.
3. **Start the probe-space gather and shorten the pixel temporal**, instead of extending
   `StableTemporal`: a probe grid at 1/8 res with 64 octahedral directions, radiance filtered
   across neighbouring probes with the hit-angle gate, then per-pixel plane-weighted
   interpolation with a jittered grid, then the existing reprojection with a 10-frame cap and
   a fast-update mode driven by whether traces hit moving geometry (velocity at the hit is
   already available from the TLAS path; for SDF hits, the change boxes are the proxy).
   Freeze `StableTemporal` where it is (default ON is acceptable as the interim) and do not
   add history length to it.

Not recommended now: tuning the radiance cache alpha, adding more confidence heuristics to the
per-pixel accumulator, or enabling the probe-space temporal filter if the gather is rebuilt
(Lumen ships it off; importance sampling from history does the temporal reuse without
blending).

## 5. Evidence limits

- Static review only. Noise amplitudes in section 2.3 follow from the code's constants and
  standard EMA statistics, not from captures; the 2026-07-27 learning is the only measured
  crawl signature cited.
- Lumen behaviour is taken from this checkout's defaults and shaders. `MaxRoughnessToTrace`
  and the exact HWRT radiance-cache variant were not read; nothing in the verdict depends on
  them.
- The sphere-crawl evidence directory (`_evidence/gi/sphere-crawl/`) is not in this working
  tree; its description comes from the rulebook design docs.
- Prior UzEngine notes under `docs/analysis/lighting/lumen/` describe Lumen from published
  material and differ from the source in details (for example 32 rays per screen probe; the
  source default is an 8x8 octahedron = 64, and the adaptive placement is interpolation-failure
  driven, not median-depth driven). This audit's Lumen facts come from the source files
  cited above and should take precedence where they disagree.
