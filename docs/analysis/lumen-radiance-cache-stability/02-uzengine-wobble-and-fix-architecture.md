# 02 - UzEngine: where the radiance-cache "crawl" enters, and a fix architecture

Date: 2026-11-02. Branch `feat/soft-shadows-area-lights`, HEAD `70813e96`. Read-only code
study; no code, build or commit. Pairs with `01-*` (how Lumen keeps its world probes stable;
separate lane) and with the render-backed diagnosis (separate lane). Every line number below
was read at HEAD; the lines are the evidence, the severity ranking is my judgement.

Symptom under study: with `r.RadianceCache=1`, indirect shadows and gradients "walk" on
objects over time (a slow crawl, not a strobe), most visibly at contacts.

Scope of the paths: the SH probe buffer written by `radiance_cache_trace_cs.hlsl` is read by
six consumers. The default lane (`r.GI.FinalGather=0`, `frame_constants.cpp:84`) is the
per-pixel interpolation in `deferred_lighting_ps.hlsl:1170-1257`; the shipped Sponza lane
(`r.GI.FinalGather=1`, `docs/analysis/GUI/images/sources/sponza-manifest.json:41`) replaces
that with the SSGI world gather (`deferred_lighting_ps.hlsl:1440-1442`) and reads the probes at
the gather's hit points instead (`sdf_reflections.hlsli:165-199`). Both lanes are covered.

---------------------------------------------------------------------------------------------

## 0. One-paragraph answer

The crawl is produced, not merely revealed, by the probe writer. Its temporal update is a
flat EMA (`a = 0.12`, no sample count, no confidence, no change term:
`radiance_cache_trace_cs.hlsl:684,692`, constant at `radiance_cache.h:97`) driven by a
ray set whose per-frame Cranley-Patterson shift is a fresh pseudo-random number every frame
(`radiance_cache_trace_cs.hlsl:518,536-537`; shared with the DDGI writer at
`ddgi_probe_common.hlsli:140-143`). A flat EMA fed independent estimates never stations: it
is an AR(1) wander whose time constant (~33 frames at 1024 probes/frame) sits below what TAA
can average - that is the "crawl". The same mechanism was measured and fixed on 2026-07-27
with a 16-phase periodic shift (commit `c0fe1c0a`, rulebook learning
`2026-07-27T17-35-23-ema-blended-gi-probes-random-walk...`); the periodic shift is no longer
in the tree (the current construction arrived with `7eea6b95`, 2026-10-04). On top of the
wander, three discrete mechanisms add steps at exactly the places the crawl is seen:
the probe-inside-geometry classification is re-decided every update against the moving
biased field and a probe that flips loses its history and re-arms from one 32-ray estimate
(`:444-446,493-507,675,684-685`); every consumer drops a rejected corner with a hard 0/1
test and renormalises the survivors (`deferred_lighting_ps.hlsl:1226-1232,1254`); and the
1024-probe update budget is a contiguous z-slab, so the two corner layers of a cell that
straddles a slab boundary are refreshed on different frames (`radiance_cache.cpp:810-812,886`).
In the default lane the per-pixel probe interpolation reaches the lighting raw (no reprojection,
no spatial filter) and only TAA stands between it and the screen.

Recommended fix: producer-side (restore a periodic, probe-shared ray rotation; replace the
flat alpha with a count-capped, change-detecting blend; hysteresis + a soft weight on the
validity flip; parity-interleaved update scheduling), each behind a default-off CVar, with an
optional consumer-side half-res reprojection + bilateral pre-filter as a second, independent
switch for the `FinalGather=0` lane. Section 4 has the designs, section 5 the CVars, section 6
the validation plan.

---------------------------------------------------------------------------------------------

## 1. Data flow (what feeds what)

```
GlobalDistanceField (4 camera-centred toroidal clipmaps, recentred per voxel
  global_distance_field.cpp:191-234)
   |
   |  SampleGdfSurface / SampleGdfDistance  (gdf_surface.hlsli:57-83, cascade pick :47,:64-78)
   v
radiance_cache_trace_cs.hlsl  (1 thread / probe, 32 rays, L2 SH, EMA)       <- RadianceCache::UpdateProbes
   |  per probe: classify (:438-490) -> trace (:532-638) -> pack (:656) -> blend (:658-695)      radiance_cache.cpp:622-902
   |  feedback: SamplePreviousDiffuse reads LAST frame's probes at hit points (:343-379)
   v
SH buffer  [probe*7 + k] SH records, [4096*7 + probe] guard record (rc_probe_guard.hlsli:23-25)
   |  record 6.w = validity 0/1 (:695; zeroed when inside :493-496)
   |  ping-pong + copy of non-updated probes (radiance_cache.cpp:689-703)
   v
PerFrameCB: rcProbeSHBufferSrv, rcGridOrigin = COMPLETED origin (frame_constants.cpp:840-855)
   |
   +--> deferred_lighting_ps.hlsl:1170-1257   per-pixel trilinear, FinalGather=0 lane  (raw -> lighting -> TAA)
   +--> sdf_reflections.hlsli:165-199         SdfReflectionIrradiance at SDF hit points (final gather, SDF reflections)
   +--> rt_reflection_cs.hlsl:170-186         same, at RT reflection hits
   +--> surface_cache_hit.hlsli:60-90         same, surface cache relight/feed
   +--> surface_cache_feed_cs.hlsl            (includes rc_probe_guard; same read pattern)
   +--> radiance_cache_trace_cs.hlsl:343-379  the writer's own multi-bounce feedback
```

Grid geometry: `kProbesPerAxis = 16`, spacing = `2*extent/15` (`radiance_cache.cpp:121`);
at the default `r.GI.RadianceCache.GridExtent = 5000` that is 666.7 cm, at Sponza's 2000 it is
266.7 cm. The grid centre snaps to the spacing (`radiance_cache.cpp:588-613`) and consumers
index with `GetCompletedGridOrigin()` (`frame_constants.cpp:847`), so a fixed world point keeps
the same trilinear weights across a recentre; the recentre scrolls the history by whole probes
(`radiance_cache.cpp:729-731`, shader `:134-138,666-676`).

---------------------------------------------------------------------------------------------

## 2. Wobble sources, with evidence

Ranked by my estimate of their contribution to a slow crawl at a fixed camera. "Lane" says
which consumer path sees it: D = deferred per-pixel (FinalGather=0), G = gather hit points
(FinalGather=1), R = reflections / surface cache (always).

### W1. Flat EMA with no confidence term, fed by a non-periodic ray rotation (lanes D, G, R)

Temporal update (answers IDENTIFY 1):

- `radiance_cache_trace_cs.hlsl:684`
  `float a = hasHistory ? saturate(fieldChanged ? regionBlendAlpha : temporalBlendAlpha) : 1.0;`
  and `:692` `shBuf[base+k] = lerp(history[historyBase+k], newPacked[k], a);`
  A flat alpha. There is no per-probe sample count, no cap, no variance or luminance-change
  term, no distinction between a probe that has been converged for 1000 frames and one that
  was re-armed last update.
- The constant is `kTemporalBlendAlpha = 0.12f` (`radiance_cache.h:97`), passed at
  `radiance_cache.cpp:722`. The brief says 0.05: the header's top comment (`radiance_cache.h:10`)
  still says 0.05 and is stale; the DDGI writer hardcodes the same 0.12 (`ddgi_system.cpp:209`).
- History is discarded (a = 1, `replace`) when: first write / `InvalidateHistory()`
  (`radiance_cache.cpp:667,726`; shader `:667`), the probe entered the grid on a recentre
  (`:666-668`), or the probe was inside geometry at its previous update (`:675`, validity 0).
- Change boxes: a probe inside a box where the field changed this frame blends with
  `regionBlendAlpha` (default 1.0 = replace, `radiance_cache.cpp:61-64,734-743`; shader
  `:683-685`). A whole-window GDF recompose (scene revision change) calls `InvalidateHistory()`
  (`frame_gi_passes.cpp:311-312`): every probe replaces with one 32-ray estimate.

Ray rotation:

- `radiance_cache_trace_cs.hlsl:518` `uint frameJitter = (frameIndex * 1664525u + 1013904223u);`
  `:536-537` `xi = frac(Hammersley(i, nRays) + float2(RadicalInverseVdC(frameJitter), RadicalInverseVdC(frameJitter ^ probeIdx)))`.
  The shift is a radical inverse of an LCG state: a different, effectively independent shift
  every frame, never repeating within any useful window, and (via `^ probeIdx`) decorrelated
  between neighbouring probes.
- `ddgi_probe_common.hlsli:140-143` (`ProbeRayDirection`) is the same construction; the trace
  comment at `:534-535` records that "since H-57 the DDGI writer uses this same construction".
- History: commit `c0fe1c0a` (2026-07-27, "eliminate GI temporal flicker") replaced an LCG
  jitter with `kJitterCycle = 16` and `cpOffset = frac(R2 * (frameIndex % 16))` (verified with
  `git show c0fe1c0a -- radiance_cache_trace_cs.hlsl`); the rulebook learning of that date
  measured frame-diff vs lag d1=1.07 -> d40=3.57 before and d1=0.19 -> d40=0.36 after (10x).
  `git log -S'RadicalInverseVdC(frameJitter ^ probeIdx)'` dates the current construction to
  `7eea6b95` (2026-10-04, "Sponza parity, flicker programme and probe classification work").
  The tree at HEAD therefore has the pre-c0fe1c0a behaviour back, with the extra per-probe
  decorrelation.

Why this is the crawl and not a sparkle. With independent per-update estimates e_t of variance
s^2, the EMA output is AR(1): stationary variance s^2 * a/(2-a) = 0.064 s^2 (std ~0.25 s), and
E[(x_{t+k} - x_t)^2] = 2 var (1 - (1-a)^k), which grows monotonically with lag k - exactly the
"random walk" signature in the learning. With 1024 probes/frame each probe is updated every 4th
frame, so the correlation time is ~4/0.12 ~ 33 frames (0.55 s at 60 Hz). TAA's history (the
usual 8-16 frame effective window) cannot average a 33-frame wander; it passes it through as a
slow drift. A periodic shift of period C turns e_t into a periodic input: the EMA converges to a
deterministic limit cycle of amplitude ~a * (spread of the C estimates), with all its power at
period C and harmonics - which TAA does average. The `^ probeIdx` term makes neighbouring
probes' wander independent, so the trilinear gradient between them wanders too: gradients
crawl rather than the whole patch breathing. The writer's own feedback
(`SamplePreviousDiffuse`, `:343-379,610`) reads last frame's wandering probes at hit points,
which lengthens the loop's correlation time further (gain ~albedo x hit fraction).

Status: mechanism confirmed by reading; magnitude for the current build is for the
render-backed lane to measure (section 6, metric M1 reproduces the learning's d_k curve).

### W2. Probe classification flips with history loss, and hard drops on the readers (lanes D, G, R)

Answers IDENTIFY 2. The decision is re-made on every update of the probe, and the consequences
of a flip are discontinuous on both sides of the buffer.

Writer side:

- Mode 0 (`r.GI.RadianceCache.ProbeClassification` default 0, `radiance_cache.cpp:65-70`):
  `radiance_cache_trace_cs.hlsl:444-446` `inside = probeSurface.r < kSurfaceEpsilon` (5 cm)
  where `probeSurface = SampleGdfSurface(probePos)` (`:438`). The field is unsigned, biased
  (half a mesh voxel + half a global voxel, `probe_side_cs.hlsl:9-12`) and sampled from
  whichever cascade contains the point (`gdf_surface.hlsli:47,60-78`). The cascade windows
  move by whole voxels with the camera (`global_distance_field.cpp:212-232`); when a probe
  crosses a cascade edge (`gdf_surface.hlsli:47`, one voxel inside the window) its distance
  is re-read from the next cascade with a different voxel size and bias, so a probe sitting in
  the band around a surface (the contact region) can cross the 5 cm threshold without the scene
  changing. Toroidal storage keeps a voxel's value stable across recentres (`:206-209`), so the
  flip trigger is the cascade switch and regional recomposes, not every recentre.
- Modes 1/2: the side buffer is deterministic per grid origin (`probe_side_cs.hlsl:19-21`,
  recomputed only when stale, `radiance_cache.cpp:675-683`), but the trace still runs
  `RelocateOrigin` against the live field on every update (`:484-488`) and the guard plane it
  writes (`:488`) moves with it.
- Mode 3 (Sponza shipped config): the whole reachability classification is recomputed on every
  full retrace, i.e. every recentre (`radiance_cache.cpp:675-680,684-687`), seeded from the
  current camera position (`:426`, `probe_reach_cs.hlsl:148-155`). Probes near the flood fill's
  frontier can change class in bulk once per spacing of travel; with `ReachRelocate` the moved
  position (`:252-286`) and guard plane are also recomputed, and readers weight the probe at its
  moved position (`deferred_lighting_ps.hlsl:1214-1216`).
- Consequence of "inside": SH zeroed and validity 0 (`:493-496`; `:695` sets 1.0 only on the
  valid path). Consequence of coming back: `:675` finds validity 0 in the history, so
  `hasHistory = false` and `:684-685` REPLACE with this frame's 32-ray estimate - the
  noisiest possible state - and the EMA then has to converge from there.

Reader side (every reader, hard tests):

- `deferred_lighting_ps.hlsl:1226` `rcValid = probeSH[base+6].w > 0.0`, `:1232` `continue`,
  `:1229` guard rejection sets `w = 0`, `:1254` renormalises by the surviving weight. A corner
  flipping valid<->rejected is an instantaneous change of the interpolation support: the
  remaining 7 (or fewer) corners are re-weighted to sum to 1, so the whole cell's irradiance
  steps, and the step is largest where the dropped corner had the most weight - next to it,
  i.e. at the surface it sits in the band of.
- Same pattern in `sdf_reflections.hlsli:191-192`, `surface_cache_hit.hlsli:81-82`,
  `rt_reflection_cs.hlsl:181-182`, and the writer's feedback `radiance_cache_trace_cs.hlsl:371-372`.
- `rc_probe_guard.hlsli:41-52` `ProbeGuardRejects` is a plane test with a 5 cm tolerance: a
  receiver straddling the plane flips with sub-centimetre changes of the plane (which W2 above
  moves on relocation).

The existing measurement `docs/analysis/rc-probe-support-followups/07-support-motion.md`
recorded the deferred `rc_support` (`dbgRcSupport`, `deferred_lighting_ps.hlsl:1247`) per
frame over the S3 motion sweep: the diffuse step correlates with the support-weight step
(0.33-0.62 under gather on), and turning the gather off inflates `lit_floor` steps 50x - the
D lane without any filter.

Status: hard-drop semantics confirmed by reading; per-frame flip frequency in mode 0 at a
fixed camera is a hypothesis that the render lane can settle with a per-frame class readback
(the current `r.GI.RadianceCache.Diagnostic` logs the class once at frame 120,
`radiance_cache.cpp:520-522`).

### W3. Update scheduling: contiguous 1024-probe z-slabs (lanes D, G, R)

- `radiance_cache.cpp:810-812` `probeBudget = fullRetrace ? 4096 : r.GI.RadianceCache.ProbesPerFrame (1024)`,
  `probeOffset = m_probeUpdateCursor`; `:886` advances the cursor by the budget. The shader maps
  thread t to probe `(offset + t) % total` (`radiance_cache_trace_cs.hlsl:414-417`) in the
  x-fastest layout (`:421-423`), so 1024 contiguous probes are 16x16x4: four z-layers.
- Cells whose two corner layers straddle a slab boundary (z = 3/4, 7/8, 11/12) have their lower
  four corners refreshed on frame f and their upper four on frame f+1; the interpolated value of
  every receiver in those cells changes on two of every four frames with alternating sign. The
  grid centre is the snapped camera (`radiance_cache.cpp:612-613`), so layers 7 and 8 bracket the
  camera height: the 7/8 straddle is the cell band at eye level, where the walls and props the
  user looks at live.
- The effective per-probe alpha is 0.12 once every 4 frames; the EMA time constant quoted in W1
  follows from this cadence. Probes outside the budget are carried over by a buffer copy
  (`:696-701`), so the four slabs are always at four different ages (0..3 frames).
- The DDGI writer updates every probe every frame (`g_probeOffset` "reserved",
  `ddgi_probe_update_cs.hlsl:39`); its only amortisation is none.

Status: confirmed by reading; a 4-frame component in the fixed-camera spectrum at the straddle
layers is the testable prediction (M1 at k = 4 and 8 vs k = 2 and 6).

### W4. Consumer interpolation (answers IDENTIFY 3) (lane D; the same code shape in G and R)

- `deferred_lighting_ps.hlsl:1175-1196`: trilinear over the 8 corners of a 266.7-666.7 cm cell
  (`uvw = saturate((worldPos - gridOrigin)/gridSize)`, `cell = uvw*(ppa-1)`). Weights are a
  function of the receiver position and the COMPLETED origin only, so they do not slide with
  camera motion or recentres (section 1). What changes per frame is the corner VALUES (W1), the
  SUPPORT SET (W2) and, in mode 3 with relocation, the EFFECTIVE POSITION used by the normal
  weight (`:1216`).
- `:1218-1220` squared geometric-normal weight; `:1244` `max(probeIrrad, 0.0)` per corner. The
  clamp rectifies the noise of the L1/L2 terms into a positive bias that scales with the noise
  amplitude (the trace shader says the same at `:680-682`): as a probe's noise changes with
  its age since re-arm, its mean contribution changes too, a second-order crawl.
- No distance weight, no Chebyshev-like visibility, no smoothing of the support set.

### W5. Nothing temporal or spatial between the probes and TAA in the default lane (answers IDENTIFY 4)

- FinalGather=0: `cosineIrradiance = irradiance` (`deferred_lighting_ps.hlsl:1366`) straight from
  the block at `:1170-1257`; the only later processing is TAA on the composited scene colour.
  There is no reprojection, no history, no bilateral filter on the indirect term.
- FinalGather=1: `cosineIrradiance = gatheredDiffuse` (`:1440-1442`), the SSGI world-gather
  texture, which has a spatial denoise (`r.SSGI.SpatialSigma*`, `ssgi_renderer.cpp:1015-1017`),
  a temporal accumulation (`r.SSGI.TemporalAlpha` 0.05 and `r.GI.FinalGather.StableTemporal`,
  `ssgi_renderer.cpp:45-46,456-457`) and a bilateral upsample (`:1333-1334`). The probes enter
  this lane only at hit points (`sdf_reflections.hlsli:165-199` via `ShadeSdfReflectionHit`
  `:213`), where their noise is one factor of a 8-ray estimate that the chain then filters. This
  is why 07-support-motion.md sees the gather "stabilise" `lit_floor`. The R-lane readers
  (reflections, surface cache feed) stay raw in both configurations.

### W6. Smaller contributors, for completeness

- Change boxes replace rather than blend (`regionBlendAlpha` 1.0): a geometry edit pops the
  probes inside the box to a one-frame estimate (W1 noise at full amplitude for ~30 frames).
- `r.GI.RadianceCache.ProbesPerFrame = 4096` (used by several analysis manifests) removes W3 but
  raises the per-frame independent-sample rate, which under W1 changes the time constant to
  ~8 frames and the std by nothing: the wander gets faster, not smaller.
- The sky term: escaping rays sample the prefiltered cubemap mip 0 (`:564-567`); with the
  rotation of W1 the sky estimate of a probe near a window varies per frame too (the same
  statistics as the hit term).

---------------------------------------------------------------------------------------------

## 3. DDGI in this tree, as a comparison

The brief asks how DDGI's temporal update and classification differ. In UzEngine they mostly
do not: the DDGI writer is also a flat EMA at 0.12 (`ddgi_system.cpp:209`;
`ddgi_probe_update_cs.hlsl:310-312` irradiance, `:325-327` depth), updates every probe every
frame, and has no probe classification, relocation, change detection or variability term
(grep of `ddgi_system.cpp/.h`, `ddgi_probe_common.hlsli`, `ddgi_probe_trace_cs.hlsl` for
hysteresis/classif/variab/relocat: no matches). It shares the ray construction of W1
(`ddgi_probe_common.hlsli:139-145`). Two things it does have that the radiance cache lacks,
and that matter for stability:

1. A continuous per-corner weight at the consumer. `deferred_lighting_ps.hlsl:1108-1141`
   multiplies the trilinear weight by a Chebyshev visibility from the depth atlas
   (`visibility = var/(var+diff^2)`, cubed) instead of a 0/1 validity test. A probe behind a
   wall fades out with distance to the mean depth rather than being dropped.
2. An octahedral atlas sampled bilinearly (`:1145-1152`, border fill `ddgi_system.cpp:320-331`)
   whose per-texel cone integration (`ddgi_probe_update_cs.hlsl:195-201`) is a wider low-pass
   than a 9-coefficient SH fit of 32 rays.

The reference DDGI toolkit the brief refers to (RTXGI: hysteresis ~0.97 as the default rate,
a per-probe change threshold on the max texel change and a brightness threshold that re-arm
the blend, back-face-fraction classification, relocation, a variability estimate that lets
converged probes be updated less often) is not present in either UzEngine writer. The design
below ports the parts that matter into the radiance cache; the DDGI writer can take W1's
fix for free since it shares `RadianceCacheTraceConstants` and the ray helper.

---------------------------------------------------------------------------------------------

## 4. Fix architecture

Three candidate designs. A is producer-side, B is consumer-side, C is A with B as an optional
second switch. Recommendation: C, implemented in the order A1 -> A2 -> A3 -> A4 (-> A5), each
gated and measured on its own, B last and only for the FinalGather=0 lane.

### 4.1 Design A - stabilise the producer

All changes live in `radiance_cache_trace_cs.hlsl`, `radiance_cache.cpp/.h` and one new
include; the consumers change only in A3 (weight multiply) and only when the CVar is on.

#### A0. Per-probe state record (shared by A2, A3, A5)

Append one float4 per probe after the guard records, the same way the guard was added
(`radiance_cache.h:85`, `rc_probe_guard.hlsli:22-25`):

- `kStateFloat4PerProbe = 1`; index `kTotalProbes * (7 + 1) + probe`; helper `ProbeStateIndex()`
  in a new `rc_probe_state.hlsli` (or appended to `rc_probe_guard.hlsli`).
- Layout: `x` = update count n (0 = no history), `y` = history DC luminance (for change
  detection), `z` = validity confidence in [0, 1] (A3), `w` = packed counters/flags (A3 flip
  counter, A2 "change pending" bit).
- Host: buffer size at `radiance_cache.cpp:131-132` and the carry-over copy at `:699-700` grow
  by `kStateFloat4PerProbe` (+64 KiB); `GetProbeSHBufferSize()` (`radiance_cache.h:192-194`)
  stays the SH-only size for the readback. The state record travels with the history shift
  like the guard does (read at `historyBase`-equivalent index), and is zeroed on a reset
  (`firstWrite`) by the writer.
- With every CVar off the record is written but never read: output identical to today.

#### A1. Periodic, probe-shared ray rotation (W1, the input side)

`radiance_cache_trace_cs.hlsl:518,536-537` and `ddgi_probe_common.hlsli:140-143`:

```
// r.GI.RadianceCache.JitterCycle (0 = today's LCG shift; C > 0 = period-C R2 shift)
float2 frameShift = (cycle == 0) ? float2(RadicalInverseVdC(frameJitter), RadicalInverseVdC(frameJitter ^ probeIdx))
                                 : frac(float2(0.75487767, 0.56984029) * float(frameIndex % cycle));
float2 probeShift = (cycle == 0) ? 0 : frac(float2(0.75487767, 0.56984029) * float(probeIdx)); // fixed in time
xi = frac(Hammersley(i, nRays) + frameShift + probeShift);
```

- The per-frame shift is shared by every probe (common-mode: neighbours see the same rotation,
  so the gradient between them does not wander); the per-probe shift is constant in time (keeps
  the fixed-pattern bias of the Hammersley set decorrelated across probes without temporal
  decorrelation). Period C = 16 reproduces c0fe1c0a; 32 halves the limit-cycle amplitude at the
  cost of a longer warm-up to the full direction set.
- Expected effect (from the 2026-07-27 measurement of the same change): d1 and d40 of the
  frame-diff curve drop ~5-10x; the d_k curve plateaus by k = C instead of growing.
- Same edit in `ProbeRayDirection` so the DDGI writer and the B.6 ray buffers stay in step;
  read the cycle from the shared `RadianceCacheTraceConstants` (a free slot:
  `_vfxEmissivePad1`, `radiance_cache.h:430`, shader `radiance_cache_trace_cs.hlsl:103`) so both
  shaders see one value.
- Interaction with `r.GI.RadianceCache.Diagnostic.DdgiSequence` (`radiance_cache.cpp:53-55`):
  that diagnostic is already the identity (`:534-535`); leave it.

#### A2. Confidence-aware blend with change detection (W1, the filter side)

Replace `:684` with a per-probe alpha derived from the state record:

```
n       = state.x                      // updates since re-arm (0 = none)
aCount  = 1.0 / (n + 1.0)              // running mean while young
aFloor  = r.GI.RadianceCache.Temporal.MinAlpha      // steady-state rate once old (e.g. 0.03)
a       = max(aCount, aFloor)
// change detection on the lowest-variance statistic we have, the DC luminance:
rel     = abs(lum(newDC) - state.y) / max(state.y, epsLum)
if (rel > r.GI.RadianceCache.Temporal.ChangeThreshold)           // e.g. 0.35
    pending = (state.w has the pending bit) ? re-arm : set pending bit   // two consecutive hits
if (re-arm) { n = r.GI.RadianceCache.Temporal.ReArmCount (e.g. 3); a = 1/(n+1); }
state.x = min(n + 1, r.GI.RadianceCache.Temporal.MaxHistory)     // e.g. 64
state.y = lerp(state.y, lum(newDC), a)
```

- Converged, stable probes hold more history (alpha falls to the floor: at 0.03 the stationary
  std of W1 drops by sqrt(0.12/0.03) = 2x on top of A1's periodic input, and with A1 the input
  is periodic so the floor can be lower still without a lag penalty on static scenes).
- Re-arm is what bounds the lag: a lighting change the change boxes cannot see (sun moves, a
  light toggles, a door opens outside the GDF change regions) shows as a DC step above the
  threshold on two consecutive updates (the two-hit rule is what keeps 32-ray noise from
  re-arming converged probes; the threshold scales with `1/sqrt(rays)` if `RaysPerProbe` is
  changed). After re-arm the probe converges as a running mean (1/2, 1/3, 1/4 ...) - faster than
  today's 0.12 for the first 8 updates, then slower.
- Geometry changes keep their existing, stronger signal: change boxes and `InvalidateHistory`
  set n = 0 exactly as `regionBlendAlpha`/`firstWrite` replace today (`:683-685`), so the
  change-box path is unchanged in behaviour and the new logic only decides the rate afterwards.
- Default-off: `r.GI.RadianceCache.Temporal.Mode = 0` keeps `a = temporalBlendAlpha` (bit-exact
  with today); `= 1` enables the above.
- Blast radius: the writer only. No consumer reads the alpha.

#### A3. Hysteresis and a soft weight on the validity flip (W2)

Writer (`radiance_cache_trace_cs.hlsl:438-507`):

- Compute today's decision as `insideRaw` (modes 0-3 unchanged, including the mode-3 bulk
  recompute on a recentre).
- Counter c in `state.w` (0..K, `K = r.GI.RadianceCache.Classification.Hysteresis`, e.g. 4
  updates): `c = insideRaw ? min(c+1, K) : max(c-1, 0)`. A probe is `inside` only when `c == K`;
  it becomes valid again only when `c == 0`. Single-update flips (a cascade switch, a GDF
  regional recompose touching the band) never reach K.
- While `0 < c < K` on the way down, DO NOT zero the SH (`:493-496` runs only at `c == K`): keep
  tracing and blending as a valid probe (its trace origin is the relocated one in modes 1-3 and
  the raw position in mode 0, exactly as the valid path does today) and write
  `state.z = 1 - c/K` as the validity confidence. On the way up (coming back from inside) the
  probe re-arms through A5 (or, without A5, replaces as today) and ramps `state.z` from 0 to 1
  over K updates.
- Record 6.w keeps its 0/1 meaning (`:695`) for every reader that is not updated: a probe is
  dropped only after K consecutive inside decisions, which alone removes single-frame flicker
  without touching any consumer.

Readers (opt-in, `r.GI.RadianceCache.Classification.SoftWeight = 1`):

- `deferred_lighting_ps.hlsl:1226-1232`, `sdf_reflections.hlsli:191`,
  `surface_cache_hit.hlsli:81`, `rt_reflection_cs.hlsl:181`, writer feedback
  `radiance_cache_trace_cs.hlsl:371`: multiply the corner weight by `state.z` instead of the
  binary skip (keep the skip for `state.z == 0`). The renormalisation at
  `deferred_lighting_ps.hlsl:1254` then fades a corner out over K updates instead of stepping.
- Keep `ProbeGuardRejects` HARD. The guard is the only thing that stops a band probe from
  lighting the far side of a thin wall (`rc_probe_guard.hlsli:3-8`); softening it re-opens the
  leak that followup-01 closed. What can be softened safely is the tolerance band: replace the
  5 cm step at `:48,:51` by a linear ramp over `[-tol, -tol - rampCm]` (e.g. 10 cm) so a
  receiver straddling the plane fades rather than flips. Gate it with the same CVar.
- Mode 3 recentre bulk flips: the hysteresis spreads them over K updates automatically; if the
  render lane shows them as the dominant residual, the next step is to run the reach passes on a
  fixed cadence (every N frames, seeded from the current camera) instead of only on recentres,
  so the class of a probe changes when the connectivity changes rather than when the grid moves.
  Not part of the first implementation.

#### A4. Parity-interleaved update scheduling (W3)

`radiance_cache.cpp:810-812,886` and `radiance_cache_trace_cs.hlsl:414-423`:

- `r.GI.RadianceCache.Schedule = 0` (today: contiguous cursor) / `1` (interleaved). With a
  budget of 4096/P probes and P = 4 phases, thread t of phase p = frameIndex % P maps to
  `x = 2*(t % 8) + (p & 1)`, `y = 2*((t / 8) % 8) + (p >> 1)`, `z = t / 64`: every cell has
  exactly two of its eight corners refreshed every frame, every z-layer is touched every frame,
  and the four ages present inside a cell are the same for every cell. P = 8 (budget 512) adds the
  z parity for one corner per cell per frame. The mapping is closed-form, so the shader keeps
  one thread per probe and the host keeps `probeBudget` as the dispatch size; `probeOffset`
  carries the phase.
- The history lookup at `:666-676` is by coordinate and unaffected; the carry-over copy at
  `radiance_cache.cpp:696-701` is unaffected.
- Expected effect: removes the 4-frame slab cadence at the straddle layers (M1 at k = 4);
  does not change the stationary variance.

#### A5. Re-arm from the neighbourhood instead of from one estimate (W2 tail)

When a probe becomes valid again (A3 counter reached 0) or enters the grid on a recentre
(`:666-668`), seed its history from the mean of its valid 6-neighbours' SH in the history buffer
(6 x 7 float4 reads) with n = 1, then blend this frame's estimate at a = 1/2. Gate with
`r.GI.RadianceCache.Temporal.SeedFromNeighbours`. Removes the full-amplitude pop of a
re-armed probe at the cost of one update of spatial blur at that probe. Optional; measure
A1-A4 first.

Trade-offs of design A: fixes the source, so every lane (D, G, R) and the writer's own feedback
benefit; no new passes, no screen-space resources, ~100 HLSL lines + ~40 C++ lines + one
include; everything is default-off and bit-exact when off. Costs: one extra float4 per probe,
a few extra ALU per probe update, and - the real risk - A2's change detector is a heuristic
with two thresholds that need the lag test of section 6 to tune; A3 holds a probe's SH for up
to K updates after it entered geometry (bounded, and the hard guard still blocks the far side).

### 4.2 Design B - filter the consumer

Only for the FinalGather=0 lane. Route the per-pixel probe interpolation through the existing
SSGI reconstruction chain as a "probe-only gather":

- `SSGIRenderer` gains a mode in which the half-res gather pass writes the trilinear probe
  irradiance at the receiver (the code of `deferred_lighting_ps.hlsl:1170-1257`, moved to a
  shared include) with zero traced rays, and the existing spatial denoise
  (`ssgi_renderer.cpp:1015-1017`), temporal accumulation (`:456-457`, history geometry,
  change-box rejection via `HistoryCB` `:63-88`) and bilateral upsample (`:1333-1334`) run
  unchanged. The deferred shader then reads `ssgiSrvIndex` as it does for FinalGather=1
  (`:1440-1442`).
- Gate: `r.GI.RadianceCache.ScreenFilter = 1`; requires `r.GI.FinalGather = 0` and the SSGI
  resources (`PrepareFrame`, `frame_constants.cpp:893-894`).

Trade-offs: hides W1-W4 for the deferred lane with code that already exists and is already
measured (07-support-motion.md: gather on vs off); reuses the SSGI history invalidation. It
adds the SSGI chain's cost (half-res), its lag (`r.SSGI.TemporalAlpha` 0.05 -> ~20 frames at
a lighting change), and its disocclusion behaviour; and it fixes nothing for the G and R lanes
or for the writer's feedback, which keep reading raw probes - the random walk is still in the
buffer and still reaches reflections and the surface cache. It also hides regressions in the
producer from the one lane that shows them most clearly. On its own it is the wrong fix; as a
second switch for the D lane it is cheap.

### 4.3 Design C (recommended) - A as the fix, B as an independent switch

Order and why:

1. A1 first. Smallest change, strongest prior (the same change was measured at 10x in this
   codebase), zero semantic risk (an unbiased estimator stays unbiased; only the correlation
   structure changes). If the render lane's d_k curve does not plateau after A1, W1 is not the
   dominant term and the ranking above is wrong - stop and re-measure before A2.
2. A2 second, with the lag test as its acceptance gate. Without A1, A2 alone would make the
   wander slower, not smaller; with A1 it converts a limit cycle of amplitude ~a*s into one of
   ~aFloor*s.
3. A3 third; measure flips per frame (`rc_support` steps) before and after. Readers' soft
   weight only after the writer-side hysteresis is in, since the hysteresis alone removes the
   single-frame class of flip.
4. A4 when M1 still shows a k = 4 component at the straddle layers.
5. A5 and B as residual tools.

Boundaries assumed: the SH buffer layout change (A0) is append-only and the six readers keep
working unchanged until they opt into A3's weight; `RadianceCacheTraceConstants` keeps its 464
bytes (the cycle length goes in an existing pad); the DDGI writer takes A1 through the shared
helper and nothing else.

What I would NOT do: lower `kTemporalBlendAlpha` globally (slower wander, same amplitude, more
lag); soften the guard; replace trilinear with a wider kernel on the consumer (blurs contacts
without removing the wander); switch to a pure running mean without change detection (lag
unbounded as n grows).

---------------------------------------------------------------------------------------------

## 5. CVars (all default = today's behaviour)

| CVar | Default | Range | Effect when changed |
|---|---|---|---|
| `r.GI.RadianceCache.JitterCycle` | 0 | 0, 8..64 | A1: period of the shared R2 shift; 0 = current LCG shift (both writers) |
| `r.GI.RadianceCache.Temporal.Mode` | 0 | 0..1 | A2: 0 = flat `kTemporalBlendAlpha`; 1 = count-capped blend + change detection |
| `r.GI.RadianceCache.Temporal.MinAlpha` | 0.03 | 0.005..0.5 | A2: steady-state rate floor |
| `r.GI.RadianceCache.Temporal.MaxHistory` | 64 | 4..256 | A2: cap on n |
| `r.GI.RadianceCache.Temporal.ChangeThreshold` | 0.35 | 0.05..2 | A2: relative DC-luminance change that re-arms (two consecutive updates) |
| `r.GI.RadianceCache.Temporal.ReArmCount` | 3 | 0..16 | A2: n after a re-arm |
| `r.GI.RadianceCache.Temporal.SeedFromNeighbours` | 0 | 0..1 | A5 |
| `r.GI.RadianceCache.Classification.Hysteresis` | 1 | 1..16 | A3: K updates to confirm a class change (1 = today) |
| `r.GI.RadianceCache.Classification.SoftWeight` | 0 | 0..1 | A3: readers multiply by the confidence; guard tolerance ramp |
| `r.GI.RadianceCache.Classification.GuardRampCm` | 0 | 0..50 | A3: width of the guard fade (0 = hard step) |
| `r.GI.RadianceCache.Schedule` | 0 | 0..1 | A4: 0 = contiguous slabs, 1 = parity-interleaved |
| `r.GI.RadianceCache.ScreenFilter` | 0 | 0..1 | B: route the deferred probe term through the SSGI reconstruction |

Existing knobs that stay as they are and interact: `r.GI.RadianceCache.ProbesPerFrame` (A4's
phase count is derived from it), `RegionBlendAlpha` (A2 keeps it authoritative for geometry
changes), `FreezeAfterFrame` (the control for every measurement below), `ProbeClassification`
(A3 wraps all four modes), `r.GI.FinalGather.FreezeAfterFrame` (control for lane G).

---------------------------------------------------------------------------------------------

## 6. Validation plan

Infrastructure that exists: `scripts/rendering/run_probe_support_motion.py` (per-frame
`rc_support` over the S3 sweep, recentres at frames 113/157/201),
`scripts/rendering/run_flicker_baselines.py` / `report_flicker_baselines.py`,
`r.Lighting.DebugMode 3` (`dbgIrradiance`, the irradiance that is lit), the probe readback
`r.GI.RadianceCache.Diagnostic` (SH + class per probe at frame 120), the opening-wall fixtures
(`scripts/rendering/probe-opening-core.uzproject`, `-ext.uzproject`,
`docs/analysis/rc-probe-support-followups/05-opening-wall.md`), and the freeze controls.
One addition is needed: a per-frame class/validity flip counter in the diagnostic readback
(today it is a single snapshot).

Fixed camera (Sponza camera C, 240-frame warm-up, then 128 frames captured; linear Rec.709;
both lanes: FinalGather 0 and 1; classification modes 0 and 3):

- M1 - frame-diff vs lag on `dbgIrradiance`: d_k for k in {1, 2, 4, 8, 16, 32, 64}. Baseline
  prediction: monotonic growth (W1). Acceptance after A1: d_64 / d_1 <= 1.5 and d_1 down >= 3x;
  after A2: d_1 down a further >= 1.5x; after A4: no excess at k = 4 vs k = 2 and 6 in the
  straddle layers (mask by probe-layer, derivable from world position).
- M2 - per-pixel temporal std over the 128 frames in the ROIs already used
  (`left_wall`, `rear_door`, `lit_floor`, `right_shadow`): each step down, no ROI worse than +5%.
- M3 - support flips: per-frame count of corners whose validity or guard decision changed
  (new counter) and the `rc_support` step: ~0 at a fixed camera after A3.
- M4 - bit-identity with everything off: SH readback at frame 120 identical to the baseline
  binary (the A0 layout change must not alter the SH records).

Lag and leak (the "no new lag/leak" half of the gate):

- L1 - sun step: rotate the sun by 30 degrees at frame N (or toggle a local light); frames to
  reach 90% of the new steady state in a lit ROI. Baseline is the 0.12 EMA at the 4-frame cadence
  (~30-35 frames). Acceptance for A2: <= baseline + 10%; failure means the change threshold or
  the two-hit rule is too conservative.
- L2 - geometry step: move a Sponza curtain / spawn a box so a change box fires; the probes
  inside must still replace (n = 0) on that frame (unchanged path), and A3 must not hold a
  now-buried probe's SH for more than K updates (measure the ROI on the far side).
- L3 - opening wall: with A3's soft weight on, the far side of the wall receives no more
  energy than with it off at any frame (the guard is hard by design; this test proves the ramp
  did not open it). Use the opening-wall fixtures' existing pass/fail.
- L4 - S3 motion sweep: at recentre frames the `rc_support` step after A3 is spread over K
  frames (peak step down, integral unchanged), and no visible dark blink (the recentre history
  shift path is untouched; this guards against a regression in A0's index arithmetic).

Energy sanity: grid mean DC from the readback within +-2% of baseline after A1/A2 (both are
unbiased changes); a small darkening is expected where the `max(.,0)` clamp bias shrinks with
the noise - record it, do not tune it away.

---------------------------------------------------------------------------------------------

## 7. Risks, unknowns, and what this study could not settle

- Magnitudes. This is a code study: the ranking W1 > W2 > W3 is a prediction. The render lane's
  M1 curve is the arbiter; a plateauing baseline curve would mean the rotation is not the
  dominant term and W2 (flips) should be attacked first.
- The dating of the W1 regression comes from `git log -S` on the current expression
  (`7eea6b95`); `-S'kJitterCycle'` returned nothing on this clone, so the intermediate history
  may be squashed. The content comparison against `git show c0fe1c0a` is unambiguous.
- A2's thresholds are heuristics; the lag test L1 is the only honest tuning method. If a scene
  has large legitimate per-update DC variance (32 rays at a window edge), the two-hit rule may
  still re-arm; the fallback is to detect on a short EMA of the DC rather than the raw estimate.
- A3 changes what "valid" means for up to K updates; the guard stays hard so leaks through
  walls cannot increase, but a probe inside an opaque solid keeps shining for K updates after
  it got there (bounded, and only on geometry changes - static scenes never hit it).
- A0's layout change touches every reader's buffer indexing only through the helper; the risk
  is a missed `kGuardFloat4PerProbe` arithmetic site (`radiance_cache.cpp:131,164,700` and the
  PassInspector readback size).
- B is explicitly a mask for one lane; shipping it without A would leave reflections and the
  surface cache on the wandering probes.

## 8. File index of the evidence

- `runtime/engine/renderer/gi/radiance_cache.h`: 85, 93, 97, 105, 192-194, 210, 360, 430
- `runtime/engine/renderer/gi/radiance_cache.cpp`: 31-80, 121, 131-132, 581-616, 622-902
  (667-668, 675-687, 689-703, 722, 726-743, 810-812, 886)
- `runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl`: 119-138, 343-379, 414-423,
  438-507, 518, 536-537, 564-567, 610, 658-695
- `runtime/shaders/runtime/gi/rc_probe_guard.hlsli`: 23-25, 35-37, 41-52
- `runtime/shaders/runtime/gi/probe_side_cs.hlsl`: 9-12, 19-21, 200-284
- `runtime/shaders/runtime/gi/probe_reach_cs.hlsl`: 145-155, 190-289
- `runtime/shaders/runtime/gi/gdf_surface.hlsli`: 38-55, 57-83
- `runtime/engine/renderer/gi/global_distance_field.cpp`: 191-234
- `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`: 1108-1141, 1170-1257, 1366,
  1419-1449
- `runtime/shaders/runtime/gi/sdf_reflections.hlsli`: 165-199, 213
- `runtime/shaders/runtime/gi/surface_cache_hit.hlsli`: 81-82;
  `runtime/shaders/runtime/reflections/rt_reflection_cs.hlsl`: 181-182
- `runtime/engine/renderer/frame/frame_constants.cpp`: 84-85, 832-855, 862-898
- `runtime/engine/renderer/frame/frame_gi_passes.cpp`: 36-41, 96-100, 119-197, 311-328,
  362-401
- `runtime/engine/renderer/gi/ssgi_renderer.cpp`: 45-46, 456-467, 1015-1017, 1333-1334
- `runtime/engine/renderer/gi/ddgi_system.cpp`: 203-209; `ddgi_probe_update_cs.hlsl`: 39,
  195-201, 304-327; `ddgi_probe_common.hlsli`: 139-145
- `.rulebook/learnings/2026-07-27T17-35-23-ema-blended-gi-probes-random-walk-when-ray-jitter-is-white-noise-use-short-cycle-cranley-patterson-rotation.md`
- `docs/analysis/rc-probe-support-followups/07-support-motion.md`
