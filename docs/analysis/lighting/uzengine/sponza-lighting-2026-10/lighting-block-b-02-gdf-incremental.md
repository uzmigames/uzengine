# Lighting block B, item 2 — incremental global distance field (2026-10-05)

Companion to the lighting assessment of 2026-10-05 (block B, "GDF incremental") and to the Rulebook
task `phase0_lighting-block-b-02-gdf-incremental` (design.md has the decisions). Branch
`feat/sponza-gi-parity`.

## Phase 2 — toroidal storage, entering slabs only

**Before.** Every recentre of a clipmap rewrote all of its `res^3` voxels over every instance
(`CompositeFromMeshSDFs`, 512-instance batches). On the long sweep of Sponza (project configuration,
256^3, 138 instances) `GDF_Composite` averaged 1.27 ms over 297 frames
(`73-b02-baseline/manifest-sweep.json`; 1.33 ms in the Incremental 0 run of G5). Those frames hold 46
clipmap-0 recentres, plus 11 of clipmap 1 and 3 of clipmap 2
(`test-gdf-incremental/g5-incremental-off/run.log`), so a recentre cost at most ~8.6 ms of GPU.
*(Corrected 2026-10-08: this first read "~125 ms of GPU per clipmap-0 recentre ... with three
recentres", which counted the 3 radiance-cache recentres of the sweep (frames 128/172/216). It also
called the cost "a hitch the flicker programme had measured as a lighting step (H-40 and
followers)"; that step was a mask artefact, ledger H-07c.)* H-49 / H-54 had shown the overlapping
voxels come out bit-identical, so all but the entering slab of that work was redundant.

**Change.** World voxel `k` lives in texel `k mod resolution`; the window centre is always a whole
number of voxels, so a voxel keeps its texel across recentres. `Update()` accumulates the whole-voxel
shift of each clipmap and the composite dispatches one slab per moved axis (the entering side,
`|shift|` planes, corners written twice) through three packed root constants (region origin, region
size, window origin modulo resolution; 12 DWORDs). The whole window is still dispatched on the first
fill, on a scene change (the instance hash) and with `r.GI.GDF.Incremental 0`. Every reader
addresses the volume as `frac(p / (2 extent))` through the static linear WRAP sampler (s3) and treats
the outer voxel of the window as outside, where a trilinear tap would wrap to the far side. The
diagnostic phase offset (`r.GI.GDF.CenterPhaseOffset`) now shifts the crossing frames only, not the
grid. `r.GI.GDF.Diagnostic.FieldCompare` compares the slab recompose with a full recompose of the
same window forced on the next frame.

**Gates** (`scripts/rendering/test_gdf_incremental.py`, pre-registered in the task):

| Gate | Criterion | Result |
|---|---|---|
| G1 | slab recompose == full recompose on every clipmap-0 recentre of the boundary trajectory (`run_gdf_field_compare.py`: IDENTICAL) | PASS (20 recentre events of the boundary trajectory, +-x and +-y single-voxel moves: 100.0000 % identical texels over the whole 256^3 volume, max |delta| 0 cm; 2 events during loading skipped by design) |
| G4 | Sponza Camera C ROI means (project configuration) with Incremental 1 vs 0 within 1e-4 relative | PASS (shadow_wall 2.3e-6, lit_wall 1.7e-7, centre 1.5e-6 relative; unsupported fractions identical) |
| G5 | `GDF_Composite` mean on the long sweep with Incremental 1 <= 25 % of Incremental 0, same recentres | PASS (`GDF_Composite` mean 1.327 -> 0.0224 ms, ratio 0.017, 46 clipmap-0 recentres in both runs; frame 25.1 -> 23.9 ms) |

Other checks on the same build: `test_gdf_controls.py` 5/5 (freeze, phase shift of the crossing frames, world tracking), `verify_sponza_loading.ps1` 3/3, `test_local_light_shadows.py` 6/6, GI gtests 59/59 (`*GI*:*Gdf*:*Radiance*:*Sdf*`), `test_gdf_raw_composite.ps1` 210/210, `test_gdf_raw_lifecycle.ps1` 34/34.

**Harnesses that compile the production code as text.** `scripts/rendering/test_gdf_raw_composite.ps1`
(the shader's `main` against a frozen original, 210/210) still passes: the region constants were
packed so that the world-position formula and `clipmapCenter` stay as the original wrote them.
`scripts/rendering/test_gdf_raw_lifecycle.ps1` had not compiled since followup-13 added the field-compare
diagnostic inside the block it extracts; its template now carries the stubs and checks the 12 root
constants (34/34).

## Phase 3 — truncation band and per-entity regions

**Band.** The stored distance (expanded and raw) is clamped to `r.GI.GDF.BandVoxels` voxels of each
clipmap (default 32: 625 cm on clipmap 0 at 256^3, 2500 cm on clipmap 1). Every reader uses the
distance as a sphere-tracing step, a validity threshold (5 cm) or a clearance, never as a far-field
value, so the band changes nothing but the step count of long open rays; it bounds how far an
instance change can reach, which is what makes a regional recompose exact. Gate G6: Camera C ROIs
with band 32 against band 0: PASS (relative differences shadow_wall 1e-07, lit_wall 2e-06, centre 3e-07; exactly 0 on an earlier run of the phase-4 build).

**Per-entity diff.** Each draw carries its entity id into the instance record (`MeshSDF::instanceKey`);
the composite keeps last frame's records sorted by key and merges them with this frame's: a changed
record (transform, SDF, material SRVs or tint — a streamed texture is a change), an added one or a
removed one yields the box old ∪ new bounds, expanded per clipmap by the band plus two voxels. Each
box becomes a window-relative region with its own cbuffer upload of only the instances whose
influence (bounds + band) reaches it; whole windows are still rewritten on the first fill, with the
band off, or when more than `r.GI.GDF.MaxRegions` (8) instances change in one frame. The scene
revision bumps only for whole-window recomposes, so the radiance cache discards its history only
then; a regional change retraces every probe with the history kept (`RetraceKeepingHistory`, what a
recentre already did) and the SSGI / gather history is left to converge.

**Gates** (`scripts/rendering/test_gdf_regional.py`, reference scene `gdf-incremental-reference`: a 100 cm box
moved 400 cm along +x by `gdf_box_move.lua` over render frames ~103-163, camera above, sun from +x;
`diffuse_indirect` of the 50 cm ground strips beside the box's start and end footprints and of a
control area 300 cm from the path, against the static variant `gdf-incremental-reference-end`
rendered over the same frames; D = the whole-window path, `r.GI.GDF.Incremental 0`):

| Gate | Criterion | Result |
|---|---|---|
| G2a | the move is regional: `GDF regional recompose` lines during the move, no whole-window recompose and no cache invalidation after loading | PASS: regional 60, revoxelise 0, invalidate 0, retrace 60 |
| G2b | `GDF_Composite` mean GPU time over frames 100-266 < 0.5 ms | PASS: B 0.0154308 ms (whole-window D 0.574639 ms) |
| G3a | END strip (beside the arrival) within 3 % of the static reference over frames 230-262 | PASS: +1.16 % (D -1.00 %) |
| G3b | START strip (beside the departure) within 3 % over frames 230-262 | PASS: -0.33 % (D -0.93 %) |
| G3c | control area within 3 % of the reference on every frame 164-262 | PASS: peak 0.96 % (D 8.63 %) |
| G3d | away from the arrival the regional path re-converges at least as well as the whole-window path (mean abs. deviation, START and control) | PASS: START 0.26 vs 1.61 %, control 0.37 vs 3.97 % |
| G3e | END strip peak abs. deviation over frames 164-262 <= 5 % | FAIL: 8.83 % (D 6.55 %) |
| G4 | Sponza Camera C ROIs, regional path vs whole-window path within 1 % | PASS (regional path vs whole-window path on Camera C: shadow_wall 0.13 %, lit_wall 0.14 %, centre 0.15 %, gate 1 %) |
| G6 | band 32 vs band 0 (whole-window recomposes), Camera C ROIs within 1e-4 | PASS (relative differences shadow_wall 1e-07, lit_wall 2e-06, centre 3e-07; exactly 0 on an earlier run of the phase-4 build) |

Phase-2 gates on the same build: G1 PASS (37 events, IDENTICAL); G5 PASS (long sweep `GDF_Composite` 1.408 -> 0.0218 ms, ratio 0.015, 46 clipmap-0 recentres in both).

**How the regional field was proven.** The field-compare diagnostic now compares the incremental result
with a whole-window recompose of the same instances in the same command list (slabs after a
recentre, regions after an instance change), so loading events are valid: IDENTICAL over 37 events
on the Sponza boundary trajectory, 34 of them the regional recomposes of the preload (100.0000 %,
max |delta| 0 cm), and over 30 events of the moving box (every compared texel identical). An A/B
(`scripts/rendering/run_gdf_regional_ab.py`) showed the regional field with the old history discard equal to
the whole-window path exactly on Camera C (2.4776e-4 / 1.3576e-4 / 2.2093e-4 on both).

## Phase 4 — region-scoped history

Phase 3 as first built kept the radiance cache's history everywhere on a regional change and Camera C
came out 45-57 % brighter: probes traced against the partially loaded Sponza (sky through missing
walls) kept that energy alive through the multi-bounce feedback. The change boxes (bounds ⊕ band of
clipmap 0 + one voxel) now travel to both consumers:

- radiance cache: `RadianceCacheTraceConstants` carries up to 8 boxes (464 B); a probe inside one
  has no usable history and replaces it with this frame's trace (`r.GI.RadianceCache.RegionBlendAlpha`
  1.0; a weight below 1 blends the stale history out over a few frames instead), every other
  probe blends as on a recentre (`RetraceKeepingHistory`, log `reason=retrace`; `r.GI.GDF.RegionalRetrace 0`
  restores the old `InvalidateHistory` for A/B runs);
- SSGI / final gather: `HistoryCB` (592 B) carries this and last frame's boxes (the gather reads the
  cache one frame late); a pixel inside rejects its history (reject code 6 `field_changed`, exposed
  by `regiondump.py`); the probe classification is rebuilt as before.

With that, Camera C after loading is within 0.13 %, 0.14 %, 0.15 % of the
whole-window path (G4), the residual of the blending outside the boxes.

**What the moving box showed.** The first G3 gates compared the strips 7 frames after the arrival
with the static reference at frame 95 and failed (END strip +15 %). The long series explained it:
the probes are updated in budgeted round-robin batches (`r.GI.RadianceCache.ProbesPerFrame`) and
blended at 0.12, and the multi-bounce feedback runs through that loop, so the GI of a freshly built
scene keeps converging for ~200 frames (the END strip of the static reference: 1.335e-1 at frame 95,
1.398e-1 at 164, 1.411e-1 over 230-262). The gates were re-specified against the reference rendered
over the same frames, with convergence judged ~100 frames after the arrival, and G3e registered
for the transient before the blend-weight candidate was measured. Over frames 164-262 the regional
path deviates from the reference by 0.26 % (START), 3.32 % (END) and
0.37 % (control) on average, the whole-window path by 1.61 %,
2.41 % and 3.97 %: discarding the history drops the lighting
everywhere (-8.63 % on the control area at frame 164) and it takes those ~200 frames
to come back, which is what phase 4 removes. Beside the arrival the regional path overshoots
instead (+6.13 % at frame 164, peak 8.83 %), decaying over ~60 frames: the replaced
probes hold a single-frame Monte-Carlo estimate and the consumers evaluate its SH with a clamp, so
its noise reads as a positive bias until the blend has averaged it.

Blend weight of the probes inside the boxes (`r.GI.RadianceCache.RegionBlendAlpha`; END strip over
frames 164-262 unless stated):

| alpha | END peak abs. dev | END mean abs. dev | END steady dev (230-262) | START mean abs. dev | control peak abs. dev |
|---|---|---|---|---|---|
| 1.0 | 8.83 % | 3.32 % | +1.16 % | 0.26 % | 0.96 % |
| 0.5 | 5.21 % | 1.78 % | +0.61 % | 0.35 % | 1.93 % |
| 0.25 | 3.75 % | 1.18 % | +0.43 % | 0.27 % | 1.59 % |

Default: 1.0 (the replace). The decision rule was registered before the candidates were measured: a
lower weight becomes the default only if it passes G3e while G3a-d and the Sponza gate G4 hold. 0.25
passes the moving box (peak 3.75 %) but fails G4 on Sponza (+6.5-8 % on the three ROIs): a loading
event's box exists for one frame, so a probe inside it blends its stale history (traced through the
walls that had not loaded yet) only once, and the multi-bounce feedback keeps the remainder alive;
the moving box fades it only because its box persists for 60 frames. So G3e stays a measured shortfall
of the replace: 8.83 % (D 6.55 %) against the 5 % gate (FAIL, documented; the whole-window path undershoots by 6.55 % at the same place). The way out is not a weight: persist each box for a few frames and
retrace only the probes inside it at a decreasing weight (no full retrace), which averages the
single-frame noise without keeping stale history (proposed phase 5).

**Cost of a regional change today.** `RetraceKeepingHistory` sets the full retrace: every probe of the
16^3 grid is traced that frame instead of the 1024-probe budget, so a frame with a moving object pays
about 4x the normal trace (Sponza: 0.72 ms per frame -> an estimated ~3 ms while something moves);
the moving-box scene measured `RadianceCache_Trace` 0.708 ms mean over frames 100-266 (60 full
retraces) against 0.617 ms static. The budget also sets the convergence time: each probe is retraced
every 4 frames and blended at 0.12, ~33 frames per e-fold before the feedback, which is the ~200-frame
settling the reference scene shows.

Other checks on the final build: GI gtests 59/59, `test_gdf_raw_composite.ps1` 210/210,
`test_gdf_raw_lifecycle.ps1` 34/34, `test_gdf_controls.py` 5/5, `verify_sponza_loading.ps1`
3/3, `test_local_light_shadows.py` 6/6; Sponza preload: 34 regional recomposes, 0 whole-window.

## Item 4.3 tried: persisted change boxes (2026-10-06, negative result)

Hypothesis: the overshoot beside an arriving object (G3e) is the single-frame noise of the replaced
probes, read as a positive bias by the clamped SH evaluation. Experiment: a change box stays alive
`RegionPersistFrames` (6) frames with its age in the constants; a region-only dispatch of the trace
shader traces every probe inside a live box every frame, whatever the budget window; the probe
replaces at age 0 and then takes the running mean of its traces (1 / (age + 1)), so no stale history
is blended back (what made the 0.25 weight fail G4 on Sponza); the full retrace of the grid on a
regional change is dropped. Both builds measured with their own fresh references (the first attempt
reused references from the build before block B.3, which moved the arrival frames of the fields, and
read 8.2-8.8 % / 3.9 % for every variant — a build mismatch, not a result).

| Build (same day, same scene) | G3e END peak | END mean abs. dev | control peak | START peak | `RadianceCache_Trace` over the move |
|---|---|---|---|---|---|
| HEAD (replace once, full retrace on the change frame) | 6.51 % | 0.70 % | 3.12 % | 2.33 % | 0.722121 ms (whole-window 0.690923 ms) |
| persisted boxes, region-only dispatch, no full retrace | 7.34 % | 1.03 % | 3.20 % | 1.92 % | 0.84954 ms (whole-window 0.667968 ms) |

Averaging six fresh traces does not move the peak, so the overshoot is not single-frame noise: the
fresh traces themselves are too bright right after the arrival and settle over ~60 frames (the 0.25
weight only looked better because it blended the darker pre-arrival history in). The code was
reverted; the harness keeps `--cvar` and prints the trace cost of both paths. Next hypothesis to test
with the per-probe transport readback (`r.GI.RadianceCache.Diagnostic`): the first traces after the
arrival see the box top or east face lit before the surrounding probes have settled, i.e. the
multi-bounce feedback through probes that are themselves in replace mode.

## Still open

- The transient beside an arriving object (G3e 6.51 % on the block B.3 build, where the
  fields arrive asynchronously; 8.8 % before): persisted boxes with averaged traces did not move it (see
  above); the next step is the per-probe transport readback on the arrival frames. The control area
  peaks at 3.12 % on that build against the 3 % gate (0.96 % before).
- A regional change still costs a full retrace of the probe grid that frame (~4x the budgeted trace).
- Mobility authoring (Static / Movable) is not needed by the diff; it would only skip the per-frame
  record build for static scenes (task 4.2 left open as a cost item).
- The SSGI history inside a change box is rejected outright; a weighted blend like the cache's could
  shorten the single-frame noise of those pixels (not measured).
- More than `r.GI.GDF.MaxRegions` (8) changed instances in one frame still rewrite the whole windows
  and discard the GI history; Sponza's preload stays below it, a large streaming burst would not.
