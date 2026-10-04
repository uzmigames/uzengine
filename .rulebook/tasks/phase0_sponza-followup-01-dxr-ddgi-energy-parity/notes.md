# Notes — phase0_sponza-followup-01-dxr-ddgi-energy-parity

2026-09-08. Exe rebuilt at 12:20 / 12:24 / 12:28 / 12:32 (one per fix step). All measurements 1920×1080, Sponza, `--screenshot-frame 300`, mean luminance /255 and share of samples below 16.

## The measurement that mattered

The task-06 observation (DXR 77.1 vs SDF 52.0) was **confounded**: `run_sponza_dxr.bat` sets `r.GI.FinalGather=0`, and with the world gather off the deferred shader re-enables the *additive* screen-space SSGI term (`deferred_lighting_ps.hlsl:969`, gated on `_sdfDiffuseSamples == 0`). The SDF capture it was compared against used `FinalGather=1`.

Controlled A/B — identical CVars (`r.RadianceCache=1 r.GI.FinalGather=0`), auto exposure **off** so adaptation does not mask the difference (tonemapped PNG luminance — a display-referred proxy, not an HDR-linear measurement), only `r.RayTracing.GI` differing:

| Build | Capture | mean | <16 | vs SDF |
|---|---|---|---|---|
| before | `ab-A-sdf-fg0.png` (SDF) | 8.1 | 90.5 % | 1.00 |
| before | `ab-B-ddgi-fg0.png` (DXR) | 12.1 | 87.8 % | **1.49** |
| +1/π +shadow | `ab-B2-ddgi-fixed.png` | 9.3 | 89.2 % | 1.15 |
| +Chebyshev fix | `ab-B3-ddgi-cheby.png` | 9.2 | 89.4 % | 1.14 |
| +GDF attributes | `ab-B4-ddgi-gdfattr.png` | 9.2 | 89.4 % | 1.14 |
| +scene lighting | `ab-B5-ddgi-scenelight.png` | 9.8 | 87.3 % | 1.21 |

The last step *raises* the number because it replaces a placeholder sun with Sponza's real one — brightness alone is not the goal, matching the scene is. The deliverable-level check is below.

## Four defects found and fixed

1. **Missing Lambert 1/π** (`ddgi_probe_update_cs.hlsl`, `TraceProbeRay`). The writer returned `sunRad * groundAlbedo`; the radiance leaving a diffuse surface is `albedo/π · E`. The SDF writer has always carried the term (`albedo * (sunRad / kPI + indirect)`, `radiance_cache_trace_cs.hlsl:388`). Every surface bounce was π× too bright.
2. **No shadow ray.** Each hit was lit as if the sun were unoccluded — interior walls fed the probes full sunlight. Added an inline `RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH>` from the hit point toward the sun, mirroring the SDF writer's `TraceGDF(shadowOrigin, L, ...) == -1` test.
3. **Chebyshev visibility was dead.** The depth atlas is RG16F and stored `hitT²` in cm²; at Sponza's scale `maxTraceDistance = probeSpacing·16·2 ≈ 4000`, so the second moment (~1.6e7) saturated FP16's 65504. `variance = max(meanD2 - meanD², 1e-4)` then collapsed to 1e-4 with `distToProbe < meanD` everywhere, so **every probe returned visibility = 1** — DDGI had no occlusion at all and lit through walls. Proof in the new diag target: `gi.ddgi_depth max = 65504.00000` before, `0.99609` after. Both moments are now stored normalised by `maxTraceDistance` and rescaled in the consumer with the same definition (`_ddgiProbeSpacing * _ddgiProbesPerAxis * 2`).
4. **The dispatch hardcoded the lighting environment.** `DDGISystem::DispatchUpdate` built its constants with `lightDir = (0,0,1)` (sun straight up), `lightIntensity = 1`, `lightColor = white`, `skyColor = (0.6, 0.7, 0.9)` and `numGdfClipmaps = 0` — no relation to the loaded scene. It now reads the same `PerFrameCB` fields and the same `GlobalDistanceField` clipmaps `RadianceCache::UpdateProbes` reads. This is the defect the "DXR ANTES" screenshot shows: flat, shadowless, warm ceiling.

Supporting change: `SampleGdfClipmap` / `SampleGdfSurface` / `SampleGdfDistance` / `GdfNormal` moved out of `radiance_cache_trace_cs.hlsl` into `runtime/shaders/runtime/gi/gdf_surface.hlsli`, included by both writers. Inline `RayQuery` reports a hit distance but no attributes (no SBT/hit-group, no bindless geometry table), so the DXR writer now takes the hit **normal and albedo** from the distance field — the same surfaces, shaded the same way — and uses DXR only for visibility. `-dir` and `groundAlbedo = 0.18` remain as fallbacks outside every cascade.

## Deliverable-level result (item 4.2)

Default project CVars, auto exposure on, frame 640:

| View | SDF (task 06 baseline) | DXR after | Δ |
|---|---|---|---|
| Camera A (`fu01-camA-dxr.png`) | 52.0 / 36.5 % | **52.0 / 36.5 %** | 0.0 % |
| Camera C QA (`fu01-camC-dxr.png`) | 90.1 / 15.7 % | **91.8 / 12.1 %** | +1.9 % |
| Camera A, before the fix (`sponza-06-dxr-f300.png`) | — | 77.1 / 1.0 % | +48 % |

Both within the ±10 % criterion. Camera A is identical because the default project runs `r.GI.FinalGather=1`, and the world gather replaces `cosineIrradiance` for both GI sources (`deferred_lighting_ps.hlsl:858`) — Camera C QA (no final gather) is the view that actually exercises the probe path. Visual check in `.screenshots/fu01-ddgi-antes-depois.jpg`: the "before" frame is flat and shadowless, the "after" reproduces the SDF reference's floor shadows, dark arcade and sun shaft.

## Residual and open

- The display-referred ratio (PNG luminance, auto exposure off) stays at 1.21 with the final gather off; no HDR-linear or per-probe measurement exists yet. Two candidates, not separated: the SDF path *loses* energy on rays that exhaust the 96-step sphere-trace budget (`TraceGDF` returns −2 → radiance 0; the same budget exhaustion the SDF harnesses assert), and the DDGI consumer's Chebyshev rejection is weaker than the radiance cache consumer's `normalWeight²` + per-probe validity. Separating them needs the enclosed-box GPU fixture with a TLAS, which is why item 3.1 shipped as a source-level test instead.
- DDGI still uses a flat `skyColor` for escaping rays while the SDF writer samples the sky **cubemap** in the ray direction. Making them identical needs one more root constant on the DDGI pipeline (`g_skyRadianceSrv`, as the RC has). Direction of the error: DDGI loses the cubemap's directional variation.
- `raysPerProbe` is hardcoded to 32 in `DispatchUpdate` while the SDF path reads `r.GI.RadianceCache.RaysPerProbe`.

## Verification

| Check | Result |
|---|---|
| `scripts/test_ddgi_energy.ps1` (new, source-level) | 16/16 |
| `--diag` DDGI targets (new registration, item 1.1) | `gi.ddgi_irradiance` min 0 / max 0.996 / avg 0.181; `gi.ddgi_depth` max 65504 → **0.996** after the fix |
| SDF path regression (same A/B run, `ab-A2-sdf-recheck.png`) | 8.1 / 90.5 % — identical to before the changes |
| `verify_sponza_loading.ps1` + `compare_sponza_stationary.ps1` | see item 4.3 in tasks.md |
| Error-level log lines in every DXR run | only the pre-existing `rt_uv_test_lib.hlsl` `[raypayload]` compile failure (3 lines) |

## Ray count shared between the paths (2026-09-15)

One of the three open residuals is closed. `raysPerProbe` was hardcoded to 32 in
`DdgiSystem`'s dispatch while the SDF radiance cache read `r.GI.RadianceCache.RaysPerProbe`, so
moving that CVar changed one GI path and not the other — an energy difference between the paths
that nothing would have reported. DDGI now reads the same CVar through the registry rather than
re-registering the name, which would collide with the cache's own declaration. The default is 32 on
both sides, so shipping behaviour is unchanged.

`scripts/test_ddgi_energy.ps1` gained two checks for it and is 19/19. **That suite is a textual check of the sources**: it
confirms code patterns, including that the CVar is now read, and validates no energy on the GPU —
it does not close DDGI/SDF parity. The negative one is the one
that matters, and it was verified against both forms rather than assumed: the pattern
`constants\.raysPerProbe\s*=\s*\d+\s*;` matches the old hardcoded line (so a regression fails the
test) and does not match the new call. Note the existing 17 checks do **not** cover the ray count —
they passed before this change too, so they were never evidence for it.

**Still open, unchanged:** the flat `skyColor` for DDGI's escaping rays against the SDF writer's
directional cubemap sample (needs `g_skyRadianceSrv` on the DDGI pipeline), and the 1.21
display-referred ratio that still has no HDR-linear or per-probe measurement. Both were left alone
deliberately: this task is not to divert the flicker work.

## The DDGI sky, and the first HDR-linear measurement (2026-09-16)

**The change.** Escaping DDGI rays sampled a flat `skyColor`; the SDF radiance cache samples the sky
cubemap in the ray direction. DDGI now does the same — same cubemap (`prefilteredSrvIndex`, the field
the radiance cache passes), same Z-up to Y-up swizzle, same multiply by the sky colour. Root constants
went from 8 to 9 (the signature allows 16). `r.GI.DDGI.DirectionalSky` (default 1, the new behaviour)
passes the sentinel SRV when set to 0, which is exactly the old flat path, so the change is measurable
on one binary instead of asserted.

**The measurement** (`scripts/run_ddgi_sdf_parity.py`, `38-ddgi-sdf-parity`): Camera C, the QA view the
record used, `r.GI.FinalGather=0` so the probes actually feed the consumer, one rectangle, 32 frames,
2 repeats per condition, one binary, every dump retained. The metric is the mean HDR-linear luminance
of `diffuse_indirect` — the deferred pass's own record of what it consumed.

| condition | `diffuse_indirect` (HDR-linear) | ratio to `sdf` |
|---|---|---|
| `sdf` (r.RayTracing=0) | 9.747781e-04 | 1.0000 |
| `ddgi_flat` (DirectionalSky=0, the old behaviour) | 1.566174e-03 | 1.6067 |
| `ddgi_directional` (the default now) | 4.983560e-04 | 0.5113 |

* **C1 (control that must not move):** the recorded view distance, 962.4553 cm with geometry at 100 %
  of pixels, identical across all three conditions (relative difference 0.00e+00). Same view, same
  geometry.
* **C2 (the switch acted):** the largest difference from `sdf` is 5.914e-04 against a repeat spread of
  3.275e-09.
* **P1 parity:** 0.5113, outside the task's +-10 % band.
* **P2 what the sky change did:** 0.3182 — the directional sky makes DDGI about three times dimmer
  here, so the flat colour had been brighter than the cubemap radiance in the directions the rays
  actually take.

**What this settles and what it does not.** The two paths now take their escaping-ray sky from the
same source, which was the open item. **Energy parity is NOT closed:** DDGI was 1.61x the SDF path
before the change and is 0.51x after it. The disparity changed sign and stayed far outside the band,
so at least one other difference remains — the candidates the record already lists (the SDF path
losing rays to the 96-step sphere-trace budget, the consumer's Chebyshev rejection) are untouched by
this and unmeasured. This is a consumer-level measurement over ONE rectangle at ONE pose: the
per-probe comparison the record asks for is still missing, and the 1.21 display-referred figure is not
comparable to these numbers (different quantity, different mask).

**Two attempts failed first, and both were the harness working.**

* The first captures were rejected by the reader: `taa_stamp_missing` on all 32 frames of every run.
  This project leaves TAA off, and the TAA resolve writes the stamp each frame is validated against.
  No preset offers LIGHTING_EXTRA without TAA, so the captures now run with `r.TAA=1` — identical in
  all three conditions and irrelevant to the metric, which the deferred pass writes before the resolve.
* The second run's control was degenerate: `direct_light` is **identically zero over the whole view**
  at this pose (probed across 1280x720 in `39-camera-c-probe`: max 0.0, 0 % non-zero). A control that
  cannot move cannot fail, so the study now reports such a control UNUSABLE and stops, instead of
  passing on nothing. The control became the view distance.
* **The zero is the pose, not a recording gap.** The scene has a shadow-casting directional sun
  (intensity 10) and a SkyLight, and the same channel records non-zero at pose A (mean 8.4e-03, max
  0.65, non-zero at 2.9 % of samples, with shadow visibility non-zero at the same 2.9 %). Camera C's
  probed view is simply fully shadowed.


## Review decision: parity stays open (2026-09-16)

The directional sky is kept as the implementation of directional sampling — the two paths now take
their escaping-ray sky from the same source. It is **not** to be presented as validated parity: the
consumer-level measurement puts the DXR path at 0.51x the SDF path (it was 1.61x with the flat sky),
both outside the +-10 % band.

**Next measurement:** a per-probe comparison, taken BEFORE the final reconstruction, and only then
isolating the tracing budget and the Chebyshev visibility.

### Performance claims are not measured (2026-09-16)

Two statements about cost are withdrawn and must not be repeated without evidence: that the ~34 s of
loading may be discounted a second time, and that "DDGI is cheap". The engine's own diagnostic report
carries an EMPTY timing array (`"timing":[]` in `DIAG_JSON`), so there are no per-pass timings in this
build's output at all. Both points stand as NOT MEASURED.


## Review decision (2026-09-16)

Proceed with the per-probe comparison, before the final reconstruction, then isolate the tracing
budget and the Chebyshev visibility. This does not depend on any TAA intervention.

## Per-probe comparison of the two writers (2026-09-16)

`scripts/run_probe_parity.py`, `43-probe-parity`, status complete. One run with BOTH diagnostics on
(the SDF cache keeps updating under the DDGI configuration), Camera C, 2 repeats. Both writers now log
every probe's transport statistics — mean ray-radiance luminance and sky fraction, the same two
quantities, before atlas integration or SH projection.

* **Q0:** both snapshots at frame 120, on the same grid (origin (-2266.67, -2533.33, -2000),
  spacing 266.67 cm), 4096 probes from each writer; paired by index (both x-fastest, then y, then z).
* **Q1:** 3495 probes outside geometry (SDF field distance >= 5 cm); 3378 traced by both.
* **C:** the two repeats give ratio medians 1.7302 and 1.7301 (0.01 % apart).

| quantity, per probe (n = 3378) | median | p10 | p90 |
|---|---|---|---|
| DDGI / SDF mean radiance | **1.730** | 1.558 | 1.864 |
| sky fraction, DDGI minus SDF | +0.0625 | -0.125 | +0.219 |

Binned by the SDF writer's sky fraction: 0-10 % sky (n = 132) ratio median ~6e6 — the SDF writer
records ~0 radiance where the DDGI records a finite value; 10-50 % (n = 58) 1.668; 50-100 %
(n = 3188) 1.729. Grid means: DDGI 2.114 (sky 0.724), SDF 1.364 (sky 0.746).

**At the writers the DXR path is 1.73x the SDF path per probe; at the consumer it was 0.51x.** The
sign of the disparity flips between the writer and the consumer, so a large part of the consumer-level
difference sits after the writers — in the atlas integration, the Chebyshev visibility or the SH
projection and consumer scaling — which is where the review's next isolation points.

**What separates the 1.73 at the writer, on the 1936 probes the DDGI sees as pure sky** (all 32 rays
escape; its mean radiance there is exactly 2.8929 at every such probe):

* the SDF writer sees sky in 87.5 % of its rays (p10 71.9 %, p90 93.75 %) and front-facing hits in
  12.5 %; the remainder — where rays that exhaust the 96-step budget or hit back faces would fall —
  has median 0.0000 and p90 0.0000. **At these probes the tracing budget is not the mechanism.**
* SDF mean radiance divided by (SDF sky fraction x 2.8929) is 0.66 (p10 0.61, p90 0.83). Hits only
  add radiance, so the SDF writer's sky rays carry at most 0.66x the DDGI's per sky ray: **the two
  writers do not get the same radiance from the same sky.** Both log the same sky multiplier (1, 1, 1).
* Decomposed: the sky-fraction difference accounts for ~1.14x (1 / 0.875) and the per-ray sky term for
  ~1.52x (1 / 0.66); their product, 1.74, matches the measured 1.737 on those probes.

**What this does not say.** Why the per-ray sky term differs — the cubemap SRV, the mip, the direction
convention or the directional distribution of the two ray sets over a cubemap whose lower hemisphere
is dark are all candidates and none is measured. Why the SDF rays hit geometry the DXR rays miss (a
conservative field, the surface epsilon) is not measured either. A ratio is not a causal share. One
pose, one frame, 2 repeats.

**Next isolation, in the review's order:** the sky term per sky ray from each writer directly (a
sky-only radiance statistic per probe), then the Chebyshev visibility and the SH projection on the
consumer side, where the sign flips.


## Review decision (2026-09-16): make the quantities comparable first

* **The 0.66 is a bound**, conditioned on the decomposition and on non-negative hit contributions —
  not an isolated measurement of the sky. Next: a sky-only radiance per probe in each writer,
  recording the direction convention, the mip, the cubemap identity, the ray count and the weighting.
* To separate the direction distribution from differences in the sampling itself, **also compare both
  paths with the same positions and the same directions.**
* The per-probe ratio median and the ratio measured on the image are different aggregations. The sign
  flip is a strong clue; on its own it does not locate an attenuation after the writers. After the
  quantities are aligned, proceed by stages, at identical positions and normals: radiance -> atlas/SH
  -> query without visibility -> query with Chebyshev.

## Sky-only radiance per probe — pre-registration (2026-09-16, before the captures)

`scripts/run_probe_sky_parity.py`; the full design is its docstring. Each writer now records the
radiance carried by its sky rays alone (the SDF writer also the fraction of rays that exhausted the
budget and the hits' share), and logs the identity of its sky sample: cubemap SRV, mip, direction
swizzle, ray count, sequence, multiplier. `r.GI.RadianceCache.Diagnostic.DdgiSequence=1` makes the SDF
writer trace the DDGI's ray sequence, so both sample the same directions from the same positions.

* **Conditions:** `own_sequences` (shipping) and `same_sequence`; both diagnostics on under the DDGI
  configuration, Camera C, 2 repeats each. Pairing by probe index with Q0 as before.
* **S0** the identity lines agree on SRV, mip, swizzle, ray count and multiplier, and the SDF writer
  reports the sequence the condition asked for; a difference is a finding and stops the sky ratio.
* **S1** per probe, where both writers have sky fraction >= 0.25: per-sky-ray radiance of each,
  and their ratio DDGI / SDF (median, p10, p90, n), in both conditions.
* **S2** the SDF writer's lost fraction per probe (median, p90, share of probes with any lost ray)
  and the sky / hit split of each writer's mean.
* **C** repeats within 5 % on the S1 median.
* **Reading, fixed now (same_sequence S1 median, band 1 +- 0.05):** FETCH DIFFERS if outside the band
  with identical directions; DISTRIBUTION if inside while own_sequences is outside; NEITHER if both
  inside (the 0.66 bound was driven by hits); UNRESOLVED otherwise. Not a share of the consumer's
  0.51x: the writers' sky term only.

## Sky-only radiance, stage 2 — pre-registration (2026-09-16, before capture)

Stage 1 (`45-probe-sky-parity`) is recorded below with its result. Reading its logs showed that its
`same_sequence` condition met the "identical directions" precondition on only one quarter of the grid:
the SDF writer traces `r.GI.RadianceCache.ProbesPerFrame` = 1024 probes per frame, so at the frame-120
snapshot its four slices carry four different frame rotations of the Hammersley set, while the DDGI
writer traces the full grid every frame. Stage 2 enforces the precondition instead of assuming it.

* capture: Camera C, still, the same base CVars, `r.GI.RadianceCache.Diagnostic.DdgiSequence=1` and
  `r.GI.RadianceCache.ProbesPerFrame=4096`; 2 repeats. Output `45b-probe-sky-parity-full`.
* Q0, S0 as in stage 1.
* **P1 (precondition, must hold or the comparison stops):** both snapshots are queued at the same frame
  and the cache's snapshot-frame line reports `probeOffset=0 probeBudget=4096`.
* **S1x (exact):** on every probe where BOTH writers report all 32 rays as sky, the per-sky-ray
  radiance ratio DDGI/SDF must satisfy |ratio − 1| ≤ 1e-5. Reported: the count of such probes, the
  count outside the tolerance, max |ratio − 1|. One probe outside → **FETCH DIFFERS** (with the list);
  none outside → **FETCH EQUAL**. No aggregate median is used for this verdict.
* S1p: on probes where both have sky fraction ≥ 0.25 but not all rays: the ratio distribution, reported
  only (different sky subsets are not the same rays).
* S3: the count of probes the DDGI sees as all-sky where the SDF field still hits (the field-vs-triangle
  difference), reported only.
* C: repeats must agree on S1x's count outside the tolerance and on the DDGI per-sky-ray value to 1e-6.

What stage 2 cannot say: anything about hits, the atlas/SH, or the consumer; and nothing about the sky
term's temporal variance beyond the one rotation it captures.

## Sky-only radiance — results (2026-09-16)

### Stage 1 (`45-probe-sky-parity`, 2 conditions x 2 repeats, repeats identical to the last digit)

* **S0 held** — after the gate caught two defects in the identity logging itself (fixed before any
  ratio was read; neither changes what a writer traces): the cache logged its sky SRV on its first
  dispatch, before the IBL bake, so it reported the sentinel while its snapshot used SRV 37195 — it
  now logs at the snapshot; the DDGI copied the sky multiplier into its identity fields before the
  constants were assigned, so it reported (0,0,0) — the copy now follows the assignment. Both writers
  then report skySrv 37195, mip 0, swizzle (x,z,y), 32 rays, multiplier (1,1,1); the cache reports
  the sequence each condition asked for.
* **S1** on the 1936 probes where both writers have sky fraction >= 0.25: per-sky-ray radiance DDGI
  2.8929; SDF 1.8176 under its own sequences (ratio median 1.5916, p10 1.3448, p90 1.6928) and
  2.0926 under the DDGI's sequence (ratio median 1.3824, p10 1.0000, p90 1.3824).
* **S2** the SDF writer's lost fraction is 0 on every probe (no ray exhausts the 96-step budget);
  its mean is sky 0.940 + hit 0.060 (own sequences), 1.000 + 0.000 (DDGI sequence).
* **Verdict by the pre-registered rule: FETCH DIFFERS** (same-sequence ratio median 1.3824, outside
  1 +- 0.05).

**What the logs showed afterwards (post-hoc, dated 2026-09-16).** The same-sequence ratio was
two-valued: exactly 1.000000 on 849 probes (indices 1024-2047) and exactly 1.382441 on 1024 probes
(indices 0-1023) — the DDGI's per-sky-ray value is one number (2.892943) on every probe, the SDF's is
2.892943 on the first set and 2.092634 on the second. The cache's snapshot-frame line reads
`probeOffset=1024 probeBudget=1024`: `r.GI.RadianceCache.ProbesPerFrame` = 1024, so at frame 120 the
cache traced probes 1024-2047 only; probes 0-1023 were traced at frame 119, with that frame's rotation
of the Hammersley set. The DDGI traces the whole grid every frame. **The condition's precondition —
identical directions on every compared probe — held on one quarter of the grid. This is a design
error of the study, like task 13's ray-origin indicator; the verdict stands as the rule's output and
does not describe the fetch.** Measured on the way: two adjacent rotations (frames 119, 120) of the
32-ray set over this cubemap at mip 0 give per-sky-ray means 2.0926 and 2.8929 — 1.38x between
consecutive frames. Both writers blend with the same EMA (alpha 0.12).

### Stage 2 (`45b-probe-sky-parity-full`, `ProbesPerFrame=4096` + DDGI sequence, 2 repeats identical)

* Q0, S0 held. **P1 held:** both snapshots queued at frame 120; the cache's line reads
  `probeOffset=0 probeBudget=4096`.
* **S1x: on all 1873 probes where both writers escape every ray, 0 probes outside |ratio - 1| <= 1e-5;
  max |ratio - 1| = 6.1e-9. Verdict: FETCH EQUAL.** With identical directions the sky fetch of the two
  writers is the same number.
* S1p (reported only): 63 probes with both sky fractions >= 0.25 but not all rays — ratio median
  0.99998 (p10 0.99995): different ray subsets, near-equal.
* S3: 63 probes the DDGI sees as all-sky where the field still hits — all in the layer just below the
  floor (world z -133); 0 the reverse. Lost fraction 0.
* Report `probe_sky_parity_full.json`; script `scripts/run_probe_sky_parity_full.py`.

**The 0.66 bound of 43-probe-parity is superseded:** it measured direction sets, not the sky sample.

### Post-hoc reading of the stage-2 captures, by grid layer (identical directions; reported, no verdict)

Grid origin z = -2000, spacing 266.67 cm; probes with field distance >= 5 cm (3495 of 4096); repeat 0
(repeat 1 identical). Sky fraction, mean radiance and the SDF's sky share are per-layer medians.

| layer | world z | probes | DDGI sky | SDF sky | DDGI mean | SDF mean | SDF sky share |
|---|---|---|---|---|---|---|---|
| 0-6 | -2000..-400 | 256 each | 1.000 | 1.000 | 2.8929 | 2.8929 | 1.000 |
| 7 | -133 | 144 | 1.000 | 1.000 | 2.8929 | 2.8929 | 1.000 |
| 8 | +133 | 176 | 0.000 | 0.688 | 0.0000 | 2.1265 | 0.935 |
| 9-11 | +400..+933 | 164-179 | 0.000 | 0.625 | 0.0000 | 1.94-1.97 | 0.91-0.94 |
| 12-13 | +1200..+1467 | 211-218 | 0.000 | 0.594 | 0.0000 | 1.89-1.90 | 0.90 |
| 14-15 | +1733..+2000 | 177-256 | 0.000 | 0.594-0.625 | 0.0000 | 1.93-1.96 | 0.90-0.91 |

* The 1936 probes the earlier studies called "the DDGI's pure-sky probes" are exactly the layers below
  the floor (z <= -133). There the writers agree to the last digit.
* Inside and above the building (z >= +133, 1559 probes) the two writers disagree on **which rays hit**,
  with the same directions: the DDGI classifies a median 0 % of rays as sky and returns exactly zero
  mean radiance on 1272 of the 1335 probes whose escaped counts differ; the SDF classifies 59-69 % as
  sky and returns ~1.9-2.1, 90 % of it sky. On no probe does the SDF see less sky than the DDGI (by
  >= 1/64). Trace limits differ: the cache traces to 2 x grid extent = 4000 cm, the DDGI to
  spacing x 16 x 2 = 8533 cm; the SDF's lost fraction is 0, so its escapes are not budget exhaustion.
* This is consistent in sign with the consumer's 0.51x (DDGI darker) but the size is not derivable from
  these medians (different aggregations), and neither writer is shown right: a probe under a gallery
  ceiling should see sky through the atrium opening only — not 0 %, and not 62 %.
* Not identifiable from the record: what the DDGI's rays hit (no hit distance recorded; a distant
  surface, sun-shadowed hits, or a sky-covering mesh in the TLAS would all give zero) and which light
  constants the DDGI used (the cache logs lightColor (10, 9.8, 9.5) and lightDir (0.107, 0.549, 0.829);
  the DDGI logs none — its constants fall to zero when it finds no light); and where the SDF's
  escaping rays leave the field (through the atrium, or through walls and ceilings the coarser cascades
  do not hold — cascades 1-3 have 78-1250 cm voxels at 64^3 over 100-1600 m half-extents).

### Dated correction to the per-probe comparison (43-probe-parity, "DDGI 1.73x the SDF writer")

That ratio compared single-frame traces with different direction sets (one DDGI rotation against the
cache's per-probe rotations, three quarters of its grid traced at frames 117-119), and its median was
taken over a population 55 % of which (1936 of 3495 probes) lies below the floor, where both writers
see only sky and the sky term alone differs 1.38x between adjacent rotations. **The 1.73x is not a
writer difference at the positions the consumer reads.** The earlier text is kept as written; this
paragraph corrects it. The per-probe writer comparison inside the building, with identical
directions, is the layer table above: DDGI ~0, SDF ~2.

### What is next (candidates, for deliberation — no correction chosen)

1. The DDGI's hits made identifiable: per probe the median hit distance and the fraction of hits
   beyond a fixed distance, plus the light constants in its identity line — to separate "dark hits"
   from "no light constants" from "a mesh covering the sky".
2. The SDF's escapes made identifiable: the cascade last sampled and the distance at which each
   escaping ray left the field, against the building's extent.
3. Then the chain as agreed: radiance -> atlas/SH -> query without visibility -> query with Chebyshev,
   at identical positions and normals, on the temporally blended outputs (both alpha 0.12) or with the
   whole grid traced each frame so the rotations coincide.


## Sky-only radiance, stage 3 — pre-registration (2026-09-17, before capture)

Stage 2 closed the sky term (FETCH EQUAL). What it left open, post-hoc and without a verdict: inside
the building the two writers disagree on **which rays hit at all** with identical directions (DDGI 0 %
sky and exactly 0 mean radiance on 1272 probes; the cache 59-69 % sky and ~1.9-2.1), and neither the
DDGI's hits nor the cache's escapes are identifiable from what is recorded. Stage 3 records them.

New records, diagnostic-only (nothing changes what either writer traces or accumulates):
* DDGI, per probe: mean hit distance, the fraction of HIT rays that are lit (`NdL * visibility > 0`),
  the mean albedo luminance at hits, the hit count; and the sun in the identity line (colour,
  direction, intensity), which the DDGI never logged.
* Cache, per probe: the mean distance at which an ESCAPED ray left the trace range, and the hit
  radiance split into its sun term and its indirect (previous-frame SH) term.

Capture: Camera C, still, stage 2's condition (`DdgiSequence=1`, `ProbesPerFrame=4096`), 2 repeats.
Q0, S0, P1 as in stage 2 — a failure there stops stage 3 as it stopped stage 2.

Measured on the probes the DDGI classifies as all-hit (sky fraction < 0.001), which is where the
disagreement lives:

  H1  the fraction of those probes whose DDGI mean radiance is exactly 0, and among their hit rays
      the lit fraction. Reported with the identity line's light constants.
  H2  the mean albedo luminance at those probes' hits.
  H3  the cache's escape distance on the same probes: median, p10, p90, and the fraction of escapes
      that leave at >= 0.99 x maxTraceDistance (4000 cm, the end of the trace range) against those
      that leave earlier (through an opening or a wall the coarse cascades do not hold).
  H4  the cache's hit radiance split on the same probes: median share of the indirect term.

  The reading, fixed now (H1 decides; the rest characterise):
    NO LIGHT        the identity line reports lightIntensity = 0 or a black light colour -> the DDGI
                    writer has no sun, and every "0 radiance" follows from that, not from visibility.
    SHADOWED+NOBOUNCE  light constants non-zero AND the lit fraction of hit rays <= 0.01 -> the hits
                    are shadowed and the DDGI writer, unlike the cache, adds no indirect term, so a
                    shadowed hit returns exactly 0. The asymmetry is structural.
    BLACK ALBEDO    light constants non-zero, lit fraction > 0.01, mean albedo <= 0.01 -> the field's
                    albedo is black where the DDGI shades.
    MIXED           anything else, reported as such.

What stage 3 cannot say: which writer is right; anything about the atlas/SH, the query or the
consumer; anything beyond Camera C and this one frame's rotation.

## Stage 3, first attempt — STOPPED by its own checks, and a dated correction to stage 2 (2026-09-17)

The first stage-3 run completed and printed `NO LIGHT`. **That verdict is void and is not a finding.**
Two of its inputs were defective and the run's own internal consistency gave them away before
anything was recorded as a result: a probe reported `skyFraction = 0` and `hitCount = 0` at the same
time, which is impossible — every ray either escapes or hits.

1. **The DDGI readback was sized with a stale factor (pre-existing, mine only by inheritance).**
   `QueueDiagnosticReadback` copied `kTotalProbes * texels * 2 * sizeof(float)` while the stats buffer
   held 4 floats per texel at stage 2 and 8 at stage 3. Only the first half (stage 2), then the first
   quarter (stage 3), of the buffer was ever copied; the rest arrived zero-filled and reads exactly
   like a probe that recorded nothing. Confirmed by the boundary: in the stage-3 log, probes 0-1023
   carry records and probes 1024-4095 are all zero, to the index.
2. **The DDGI light constants were copied into the identity line above the assignments they read**
   (mine) — the same defect the gate caught in the sky multiplier on 2026-09-16, repeated. It reported
   colour (0,0,0) and intensity 0, which is what produced `NO LIGHT`.
3. **The escape-distance indicator (H3) is void by construction** (mine). `TraceGDF` returns the
   escape sentinel exactly when `t >= maxT`, so an escaped ray's end distance is always at the end of
   the range: the indicator reported "100 % at range end" and could not have reported anything else.
   Measured values are kept for the record (median 4759 cm, p10 4518, p90 5055 — the range end of
   4000 cm plus one final step) but they answer nothing. This is the second indicator in this
   programme whose threshold could not fail (after task 13's ray-origin indicator, 2026-09-16).

### Dated correction to the post-hoc layer reading of stage 2 (H-51), 2026-09-17

The paragraph "Post-hoc reading of the stage-2 captures, by grid layer" above is **wrong for the rows
with world z >= +133 (grid layers 8-15, probes 2048-4095)**, and so is the sentence in the H-51 ledger
row that reads "DDGI 0 % sky and exactly 0 radiance on 1272 probes". Those probes were never copied
back: their DDGI columns were the zero fill of an uncopied readback, not a measurement. The earlier
text stays as written; this paragraph corrects it.

* What survives: everything measured on probes 0-2047 — the sky-fetch equality (S1x, 1873 both-all-sky
  probes, all in the copied range), the identity checks, the layers 0-7 rows, and the 63 probes where
  the DDGI is all-sky and the cache hits. **Stage 2's verdict FETCH EQUAL is unaffected.**
* What is withdrawn: every statement about what the DDGI does inside the building (layers 8-15),
  including "0 % sky", "exactly 0 radiance on 1272 probes", and the reading that the two writers
  disagree on hit/sky classification there. **That disagreement is now unmeasured, not established.**
* The cache's own columns in those rows came from its own log lines and are unaffected by this defect,
  but a comparison needs both sides, so the comparison rows are withdrawn with the DDGI's.

### H3b — pre-registration, before the re-run (2026-09-17)

H3 is replaced, not relaxed: a void indicator is repaired, and the original stays in the record above
with its numbers. The new one can fail.

* **H3b:** for the cache's escaped rays, the **closest approach to a surface** along the march
  (`min SampleGdfDistance` over the ray's steps), averaged per probe over that probe's escaped rays;
  reported as median, p10 and p90 across the population, plus the share of probes whose mean closest
  approach is within one clipmap-0 voxel (19.53 cm — a stated scale, not a threshold).
* Reading: a ray that leaves through an opening never comes near a surface; one that leaves through a
  wall the coarse cascades do not hold passes within roughly a voxel of one without registering a hit.
  Reported as a characterisation; **no verdict is attached to H3b** — it feeds the next stage's design.
* H1, H2, H4 and the C gate keep the thresholds pre-registered on 2026-09-17 before the first run,
  unchanged.

## Stage 3 — result on the repaired build (2026-09-17)

`48-probe-hit-identity`, status complete, 2 repeats identical to the last digit, C gate passed.
Q0/S0/P1 held: 4096 probe lines from each writer, the same sky sample, the whole grid traced at the
snapshot frame. **The DDGI's identity line now carries its sun: colour (10, 9.8, 9.5), direction
(0.107, 0.549, 0.829), intensity 1.0 — the same the cache logs, and `indirectTerm=none`.**

### The pre-registered population: probes the DDGI classifies as all-hit (n = 274)

| quantity | value |
|---|---|
| H1 mean radiance exactly 0 | 42.7 % of the 274 probes |
| H1 lit fraction of their hit rays | median 0.0312 (1 ray of 32), max 0.5000 |
| H1 hits per probe / mean hit distance | 32.0 / 576.6 cm |
| H2 albedo luminance at those hits | median 0.2338, max 0.2700 |
| H4 cache hit radiance on the same probes | sun 0.0, indirect 2.6e-08 |

**Verdict by the pre-registered rule: MIXED.** The light constants are non-zero (not NO LIGHT), some
hits are lit (lit fraction max 0.5, so not SHADOWED+NOBOUNCE) and the albedo is 0.234 (not BLACK
ALBEDO). Reported as such: on these probes the DDGI's zeros are shadowed hits on a non-black surface
under a real sun, and the writer has no bounce term to give them — but that describes 42.7 % of 274
probes, not a mechanism for the scene.

### H3b — where the cache's escapes go (characterisation, no verdict)

Over the 3208 probes with escaped rays: the closest a ray came to a surface, averaged per probe,
has median **331.6 cm**, p10 70.1, p90 560.2; **0.5 %** of probes average within one clipmap-0 voxel
(19.53 cm). The cache's escaping rays leave with clearance — they are not squeezing through walls the
coarse cascades fail to hold. Whatever separates the two writers, it is not that.

### The layer table, re-measured (replaces the one withdrawn above; reported, no verdict)

Same capture, repeat 0 (repeat 1 identical); probes with field distance >= 5 cm (3495 of 4096);
per-layer medians. `ratio` is the per-probe DDGI/SDF mean radiance, median over the layer.

| layer | world z | n | DDGI sky | SDF sky | DDGI mean | SDF mean | ratio | DDGI lit hits | SDF escape clearance |
|---|---|---|---|---|---|---|---|---|---|
| 0-7 | -2000..-133 | 144-256 | 1.000 | 1.000 | 2.8929 | 2.8929 | 1.000 | — | 185-569 cm |
| 8 | +133 | 176 | 0.781 | 0.688 | 2.2602 | 2.1265 | 1.049 | 0.000 | 199 cm |
| 9 | +400 | 164 | 0.688 | 0.625 | 1.9890 | 1.9736 | 1.006 | 0.000 | 220 cm |
| 10-11 | +667..+933 | 178-179 | 0.656/0.625 | 0.625 | 1.90/1.81 | 1.94/1.95 | 0.967/0.962 | 0.000 | 224-233 cm |
| 12-13 | +1200..+1467 | 211-218 | 0.594 | 0.594 | 1.7177 | 1.89/1.90 | 0.961/0.965 | 0.000 | 239-244 cm |
| 14 | +1733 | 177 | 0.656 | 0.625 | 1.9144 | 1.9629 | 0.977 | 0.182 | 249 cm |
| 15 | +2000 | 256 | 0.656 | 0.594 | 2.0101 | 1.9317 | 1.040 | 0.551 | 187 cm |

* **The two writers agree.** Inside and above the building (layers 8-15, 1559 probes) the per-probe
  mean-radiance ratio has median **0.9893**; per layer it stays within 0.96-1.05. Below the floor it
  is 1.000 to the last digit. The sky-fraction difference (DDGI minus cache) has median **+0.0312** —
  one ray in 32, and in the DDGI's favour — with 71.1 % of all probes escaping the identical ray
  count. **Everything the withdrawn table said about the interior was the uncopied readback.**
* The cache's hit radiance is essentially all multi-bounce where it has hits (n = 1622: sun median 0,
  indirect median 0.375), which is expected at Camera C — the view is fully shadowed, so the sun term
  is legitimately zero there (the same reason `direct_light` is zero at this pose). The DDGI has no
  such term at all. The effect on the writers' totals is small because the sky carries 90.8 % of the
  cache's mean.

**What this settles.** At the writers, with identical positions and directions and the whole grid
traced, the DXR and SDF paths produce the same probe radiance to about 1 % in the median and 5 % per
layer. **The consumer-level 0.51x is therefore not made at the writers** — it is made after them, in
the atlas/SH, the query, or the visibility test. That is the next stage, and it is the one the review
asked for: atlas/SH -> query without visibility -> query with Chebyshev, at identical positions and
normals.

**What it does not settle.** One pose, one frame's rotation, no temporal statistics; nothing about the
consumer; and the 1.73x of 43-probe-parity stays corrected, not re-measured, for the reasons recorded
there.


## Consumer stage — known lighting — pre-registration (2026-09-18, before capture)

Stage 3 (H-52) put the consumer-level 0.51x after the writers. The review of 2026-09-18 asked for the
direct test before any energy is added anywhere: feed BOTH formats a known radiance and compare
writing, reconstruction and query, visibility included, so that a consumption error is not compensated
by producing more energy.

Instrument, diagnostic only: `r.GI.Diagnostic.KnownRadiance = L` makes every probe ray of both writers
return the constant (L, L, L) as an escape — the cache's SH holds the analytic projection of a
constant, the DDGI atlas holds L in every texel and the far distance in its depth atlas (Chebyshev
visibility 1). `r.GI.Diagnostic.KnownRadianceKeepDepth = 1` keeps the DDGI's real hit distances with
the radiance still constant, which isolates the visibility term. Both writers echo the values in their
identity lines.

What the consumer should then produce, from the code (deferred_lighting_ps.hlsl): `cosineIrradiance =
irradiance` (final gather off), `diffuseAmbient = (1 - metallic) * irradiance * baseColor`, and the
region dump records `diffuseIndirect = diffuseAmbient * ao * skyTint`. For a constant L, `irradiance`
is L on the cache path (PackL2 stores E/pi; EvalSH_L2 of a constant returns it) and L on the DDGI
path (each texel is sum(w L)/sum(w) = L; the trilinear/normal/visibility weights are normalised), so
**the two paths must record the same `diffuseIndirect` at the same pixel** — every other factor in it
is the pixel's own. The comparison is per pixel and relative; no absolute anchor is claimed, because
the per-pixel base colour and tint are not in the dump.

* capture: Camera C, still, `r.GI.FinalGather=0`, `r.GI.Reflections=0`, `r.SSGI=0`, `r.GTAO=0`,
  `r.RadianceCache=1`, `r.GI.RadianceCache.ProbesPerFrame=4096`, `r.TAA=1`, `r.AutoExposure=0`,
  `r.GI.Diagnostic.KnownRadiance=1`; three conditions, 2 repeats each:
    RC         `r.RayTracing=0` (the cache is the consumer's source)
    DDGI       `r.RayTracing=1`, `r.RayTracing.GI=1`, `KeepDepth=0`
    DDGI-CHEB  as DDGI with `KeepDepth=1`
  region dump `components` on `shadow_wall` (150,300), `lit_wall` (1100,300), `centre` (896,476),
  128x128 each, frames 200-202 (both writers' EMA at alpha 0.12 converged to < 1e-4 by then).
* **K0 (the injection took):** both identity lines echo knownRadiance=1 and the keepDepth asked; the
  per-probe writer logs show, on every probe outside geometry, cache skyLum/skyFraction = 1 +- 1e-3
  and DDGI meanRadiance = 1 +- 1e-3 (KeepDepth=0). A miss here stops the stage.
* **K1 (reconstruction + query, visibility 1):** per pixel with geometry (view distance >= 0),
  `diffuseIndirect` luminance DDGI / RC — median, p10, p90 per ROI, over the three frames. Band fixed
  now: **1 +- 0.05.** Between-process variability of this kind of ratio was 2e-4, so 5 % is a real
  difference, not noise.
* **K2 (visibility):** DDGI-CHEB / DDGI, same statistic, same band.
* **C:** repeats agree on the K1 and K2 medians to 1e-3.

  The reading, fixed now:
    CONSUMER SCALES   K1 outside the band on any ROI — the consumer's reconstruction or query scales
                      one path against the other even with identical stored energy; the ratio says
                      by how much, and the next step is which factor (SH evaluation, atlas sampling,
                      weights, intensity).
    VISIBILITY        K1 within the band, K2 outside — the Chebyshev test removes energy on this
                      scene; the ratio is its size at this pose.
    NEITHER           both within the band — the consumer chain treats equal stored energy equally;
                      the 0.51x is then made by what is stored under REAL lighting, i.e. the temporal
                      blending or the hit shading (no indirect term in the DDGI), which stage 3 could
                      not see at a single frame.
    MIXED             anything else, reported as such.

What this cannot say: anything about real lighting or the bounce-dominated interior (that is the next
item of the review's order); nothing about which correction, if any, follows.

## Consumer stage — result, and a pre-registered post-hoc test of one candidate (2026-09-18)

`52-consumer-known-radiance`, status complete, 2 repeats identical to the last digit, C passed.

**K0 held on all six captures:** both identity lines echo `knownRadiance=1` and the keepDepth asked;
on every one of the 3495 probes outside geometry the cache stored per-sky-ray 1.00000 and the DDGI
stored mean 1.00000. The tracing is out of the way and the two stores hold the same constant.

| ROI | K1: DDGI / RC (median, p10, p90) | pixels | K2: CHEB / DDGI |
|---|---|---|---|
| shadow_wall | **0.7236** (0.5382, 0.9924) | 42 443 (13.6 % excluded, dark) | 1.0000 |
| lit_wall | **0.5915** (0.4978, 0.9848) | 16 995 (65.4 % excluded, dark) | 1.0000 |
| centre | **0.5728** (0.4974, 0.9781) | 48 429 (1.5 % excluded) | 1.0000 |

**Verdict by the pre-registered rule: CONSUMER SCALES.** With identical stored energy the consumer
returns, on the DDGI path, roughly 0.57-0.72x what it returns on the cache path, per pixel between
0.50 and 0.98. The consumer-level 0.51x of 38-ddgi-sdf-parity is reproduced in sign and rough size
with the tracing removed: **it is made in the consumer chain.**

**K2 is void under a uniform known radiance — recorded as a limit of this design, not as a result.**
With every probe holding the same value, any normalised weighting of them returns that value, so the
Chebyshev term can only show up as a rejected probe, never as a scale. A non-uniform known field would
be needed to measure it; nothing is claimed about visibility here.

### One candidate, from the code, with a pre-registered prediction before the post-hoc test

The writer skips the atlas tiles' 1-texel border (`ddgi_probe_update_cs.hlsl:361-362`: threads with
`localX == 0 || localX > tileSize` return), no pass fills the border, and the atlas is never cleared
(`ddgi_system.cpp:42-77`). The consumer samples the 6x6 tile with a bilinear sampler at
`origin + oct * 6` (`deferred_lighting_ps.hlsl:725-731`), which reaches into the border texels for
octahedral coordinates near 0 or 1. If the border holds 0, the sampled value is the interior value
times the bilinear weight that stayed inside the tile.

* **Prediction, fixed before computing:** per pixel, `predicted = (1 - bx) * (1 - by)` where `bx`,
  `by` are the bilinear weights that fall on a border texel in each axis, from the pixel's normal
  through the consumer's own `DDGIOctEncode`. If the border is the mechanism, the measured DDGI/RC
  ratio equals the prediction: **median of measured/predicted within 1 +- 0.05 on every ROI, and the
  fraction of pixels within +-0.05 of their prediction >= 90 %.** If it is not, the two do not agree
  and the candidate is rejected. The normal is read from the region dump; a gate requires it to be
  unit length on >= 99 % of geometry pixels.
* What agreement would and would not mean: it would identify the mechanism of the consumer's scale
  under a uniform field; it would say nothing about the depth atlas (same border, Chebyshev) or about
  real lighting until measured.

### The border test — result (2026-09-18)

`consumer_border_test.json`, status complete; the normal channel is unit length on the gated fraction.

| ROI | measured DDGI/RC | predicted (border = 0) | measured / predicted (median, p10, p90) | within +-0.05 | normals touching a border texel |
|---|---|---|---|---|---|
| shadow_wall | 0.7236 | 0.7313 | 0.9924 (0.9682, 0.9958) | 95.9 % | 77.9 % |
| lit_wall | 0.5915 | 0.5957 | 0.9951 (0.9898, 0.9984) | 97.4 % | 86.0 % |
| centre | 0.5728 | 0.5790 | 0.9962 (0.9926, 0.9985) | 98.4 % | 90.0 % |

Both repeats identical. P1 and P2 hold on every ROI. **Verdict by the pre-registered rule: BORDER.**

**What is established.** The DDGI consumer's scale under a uniform stored field is the atlas's
unwritten 1-texel border sampled bilinearly: the writer never fills it (`ddgi_probe_update_cs.hlsl:
361-362`), nothing clears the atlas (`ddgi_system.cpp:42-77`), and the consumer's bilinear tap at
`origin + oct * 6` (`deferred_lighting_ps.hlsl:725-731`) reaches it for every normal whose octahedral
coordinate is within half a texel of the tile edge — 78-90 % of the normals in these ROIs. The
per-pixel loss is the bilinear weight that landed on the border, from nothing at the tile centre to
half of the value at an edge and three quarters at a corner, and the prediction from the consumer's
own encode reproduces the measured ratio to within 1 % in the median. The 0.5-0.8 % residual below
the prediction is not attributed.

**What it means for the 0.51x.** Under real lighting the consumer read the DXR path at 0.51x the SDF
path (38-ddgi-sdf-parity, Camera C). Under a uniform known field the same consumer reads 0.57-0.72x,
by this mechanism alone; the two agree in sign and rough size, and the remainder, if any, is not
attributed here. The 0.51x was never a matter of energy produced: adding an indirect term to the DDGI
writer would have raised the stored energy to compensate for a consumer that discards part of it.

**What it does not say.** The depth atlas has the same border (16x16 tiles, same writer gate, same
bilinear sampling of the moments): the Chebyshev test reads unwritten depth at the tile edges too, and
K2 could not see it under a uniform field. Not measured. Nothing is said about real lighting beyond
the sign and size above, nor about the bounce-dominated interior.

**No correction is chosen.** What a correction would have to do is stated for the record: fill the
border texels of both atlases after each update (the octahedral wrap the header comment already
describes), or clamp the sampling to the interior; then re-run this stage — the pre-registered K1
band is the acceptance test, and the depth atlas needs its own.

## The border fill — correction and re-run (2026-09-18)

Review decision (2026-09-18): fill the border of both atlases respecting the octahedral mapping's
continuity; not clamping the read to the interior, which avoids unwritten texels but changes the
interpolation at the edges. Clear at creation for predictable initial contents — it does not replace
the fill, and uninitialised memory is not guaranteed to be zero, so the H-55 prediction describes
those runs, not every environment.

**Correction.** `ddgi_border_fill_cs.hlsl` (new) copies each tile's interior into its 1-texel border
after every probe update — edges from the adjacent interior column/row with the along-edge coordinate
mirrored, corners from the diagonally opposite interior corner (Cigolle 2014 identifications, the
same fold as `OctWrap`) — for the 6x6 irradiance tiles and the 16x16 depth tiles alike, each behind a
UAV barrier and before the transition the deferred lighting reads through; on the first dispatch it
writes zeros to both atlases (`g_clear`) because the RHI has no UAV clear. Wired as
`PipelineLibrary::GetDdgiBorderFill()` and a `borderPipeline` parameter of `DDGISystem::DispatchUpdate`
(`ddgi_system.cpp`, `frame_gi_passes.cpp`).

**Re-run of the consumer stage, original criteria** (`53-consumer-known-radiance-borderfix`, 2 repeats
identical, K0 held on all six captures):

| ROI | K1 before (H-55) | **K1 after** (median, p10, p90) | K2 |
|---|---|---|---|
| shadow_wall | 0.7236 | **0.9923** (0.9681, 0.9953) | 1.0000 |
| lit_wall | 0.5915 | **0.9953** (0.9896, 0.9970) | 1.0000 |
| centre | 0.5728 | **0.9963** (0.9939, 0.9970) | 1.0000 |

**Verdict by the pre-registered rule: NEITHER** — both K1 and K2 within 1 +- 0.05. With identical
stored energy the consumer now treats the two paths equally to within 1 %. The border prediction
re-run on the fixed captures reads NOT BORDER (measured/predicted 1.35-1.71), which is what a filled
border must produce against a prediction that assumed an empty one; it is the same test giving the
opposite answer on the two builds, not a contradiction.

**Residual, reported, not attributed:** DDGI/RC sits 0.4-0.8 % below 1 in the median, with p10 down to
0.968 on `shadow_wall`. Small against the 28-43 % it replaced; not zero.

**Limits carried forward:** K2 is still void under a uniform field, so the depth border's effect on
the Chebyshev test is corrected in the same pass but **not yet measured** (review step 4); continuity
of the filled border — an inverted or misplaced copy — is invisible to a uniform field and needs the
directional known field (review step 3); and nothing here is real lighting (review step 5).


## Review step 3 — border continuity under a directional known field — pre-registration (2026-09-18, before capture)

A uniform field cannot distinguish a correctly mirrored border from an inverted or misplaced copy:
every texel holds the same value. `r.GI.Diagnostic.KnownRadianceMode = 1` makes both writers return
`L * (0.5 + 0.5 * dot(dir, d0))`, d0 = normalize(0.3, 0.5, 0.8) — a smooth, non-negative function of
direction that crosses every octahedral tile edge and corner. For this field both stores are exact in
the sense the consumer needs: the cache's L2 SH holds an L0+L1 function exactly, and the atlas texel's
cosine-weighted mean of a linear term is (2/3) of it, so both give irradiance/pi = L (0.5 + (1/3)
dot(N, d0)); the two paths must again agree per pixel, and the filled border must not show.

* capture: as the consumer stage (Camera C, still, final gather / SDF reflections / SSGI / GTAO off,
  cache on with the whole grid, TAA on, `KnownRadiance = 1`, `KnownRadianceMode = 1`), conditions RC
  and DDGI (KeepDepth 0), 2 repeats, the same three ROIs, frames 200-202.
* **K0:** both identity lines echo knownRadiance = 1 and knownMode = 1; the cache's sky fraction is
  1 on every probe outside geometry and its per-probe sky luminance lies in [0, 1]; the DDGI's
  per-probe mean radiance lies in [0, 1] and, because the field does not depend on position, is the
  **same number on every probe** (max - min <= 1e-3). A miss stops the stage.
* **K1:** per pixel with geometry, diffuseIndirect DDGI / RC — median, p10, p90 per ROI; band
  **1 +- 0.05** (unchanged from the consumer stage).
* **K3 (continuity):** pixels split by the consumer's own encode into *border-touching* (a bilinear
  tap reaches a border texel) and *interior*. Per ROI: |median ratio(border-touching) - median
  ratio(interior)| <= **0.02**, and >= **90 %** of border-touching pixels within +-0.05 of the interior
  median. A border copied from the wrong texel, or mirrored the wrong way, deviates exactly and only
  on the border-touching set.
* **C:** repeats agree on the K1 and K3 medians to 1e-3.

  The reading, fixed now:
    CONTINUOUS     K1 and K3 hold on every ROI — the filled border is consistent with the octahedral
                   mapping and the two paths agree under a directional field.
    DISCONTINUOUS  K1 holds, K3 fails — the border is filled but from the wrong place or orientation.
    SCALED         K1 fails — a scale remains under a directional field that the uniform one hid.
    MIXED          anything else, reported as such.

What this cannot say: anything about the depth atlas and Chebyshev (a direction-only field is identical
on every probe, so the visibility weights still normalise to nothing — review step 4 needs known
distances); anything about real lighting (step 5).

## Review step 3 — result (2026-09-18): SCALED, and the reason is the DDGI's ray set, not the border

`54-consumer-known-directional`, status complete, 2 repeats identical, C passed.

| ROI | K1 DDGI/RC (median, p10, p90) | K3 touching vs interior | K1 | K3 |
|---|---|---|---|---|
| shadow_wall | 0.9293 (0.8461, 1.2581) | 0.9315 vs 0.9173, gap 0.014, 49 % within band | fail | fail |
| lit_wall | 0.9394 (0.8678, 1.2961) | 0.9428 vs 0.9184, gap 0.024, 55 % | fail | fail |
| centre | 0.9101 (0.8516, 1.1220) | 0.9155 vs 0.8492, gap 0.066, 32 % | fail | fail |

**Verdict by the pre-registered rule: SCALED.** But K0, read before any of that, locates it: for a
field whose spherical mean is exactly 0.5, the cache measured 0.4897-0.4940 per probe (its own
stratified set under a per-probe rotation) and the DDGI measured **0.37304 on every probe**. The
DDGI's identity line says its 32 rays are the same on every probe; that number is a property of the
ray set, not of the scene.

### The mechanism, replicated to the last digit

The DDGI draws its rays as `Hammersley(i + (jitter & 0xFFFF), 32 + (jitter & 0xFF))`
(`ddgi_probe_update_cs.hlsl:418-420`). The first Hammersley coordinate is `i / N`, so for a
contiguous run of 32 indices out of an N of 32-287 the rays cover a slice of the polar range, not the
sphere. Replicating the set for the snapshot frame (frame 120: jitter 0x4856c977, N = 151, indices
51575..51606) gives **z in [-0.523, -0.113]** for all 32 rays and a predicted mean of the directional
field of **0.37305 against the measured 0.37304**; the cache's construction — a full 32-point
Hammersley set with a Cranley-Patterson rotation per frame and per probe — replicates its measured
0.4897 / 0.4940 / 0.4917 / 0.4898 / 0.4903 on five probes to four digits. The "index/count
perturbation" was never a rotation: it is a window into a longer sequence, and the window moves with
the frame.

### What this changes in the record

* The border continuity test (K3) is **not readable on this build**: its "interior" reference is an
  atlas reconstructed from a band of directions, so the touching/interior gap measures the band, not
  the copy. K3 is deferred to the re-run after the ray set is corrected; nothing about the border's
  continuity is concluded either way here.
* H-51's side fact — "two adjacent rotations of the 32-ray set give 2.09 vs 2.89 on the sky term
  (1.38x)" — now has its cause: not two rotations, two **bands**.
* H-52's writer agreement (0.9893) stands as what it measured: the cache traced the DDGI's band
  under the `DdgiSequence` mode, so both writers saw the same directions and the comparison of the
  fetch and the hit logic was fair. It did not say the band was a good sphere sample, and it was not.
* Under real lighting the band changes every frame, so each frame's probe estimate is biased in a
  direction that moves: a per-frame variance source in the DDGI path in its own right (not measured
  as such; recorded as the reading it invites).

### Correction chosen (the review's principle: a concrete defect, a delimited intervention)

Give the DDGI the cache's construction: `frac(Hammersley(i, nRays) + (VdC(jitter), VdC(jitter ^
probeIdx)))` — a stratified 32-point set, rotated per frame and per probe. The cache's `DdgiSequence`
diagnostic branch is made identical to it so "same sequence" remains literally true. No other
behaviour changes.

**Acceptance, pre-registered before the re-run:** step 3 repeated unchanged, plus
**K0b:** the DDGI per-probe mean of the directional field lies in **[0.48, 0.52]** on every probe and
is **no longer the same number on every probe** (max - min > 1e-3) — the K0 identity clause of the
first run is replaced by this, since a per-probe rotation makes the means differ by design. K1 and K3
keep their bands. If K1 holds and K3 fails now, the border copy is wrong and that is what is reported.

## Review step 3 — re-run on the corrected ray set (2026-09-18)

`55-consumer-known-directional-rayset`, status complete, 2 repeats identical, C passed. The DDGI
identity line now reads `sequence=stratified-frame-probe-rotation`.

* **K0b held:** the DDGI's per-probe means of the directional field are **0.48970-0.49403 — the same
  range, to five digits, as the cache's**, and they vary per probe. The two writers now draw the same
  construction and, under the same jitter and probe index, the same directions.
* **K1 held:** DDGI/RC median **1.0061 / 1.0229 / 1.0068** (p10 0.942-0.967, p90 1.117-1.156).
* **K3 failed:** touching vs interior 1.0117 vs 0.9903 (gap 0.021), 1.0249 vs 1.0034 (0.022), 1.0071 vs
  0.9361 (0.071); 61 / 64 / 17 % of touching pixels within +-0.05 of the interior median.

**Verdict by the pre-registered rule: DISCONTINUOUS.** The rule's interpretation — "the border is
filled but from the wrong place or orientation" — is **not** what the evidence shows, and it is
withdrawn as an interpretation; the verdict stays as the rule's output.

### Why K3 cannot be read that way, established without the engine (post-hoc, so labelled)

1. **On `centre` the deviation sits on the interior pixels, not on the border-touching ones** (0.936
   against 1.007). A misplaced border moves the touching set and leaves the interior alone; the sign is
   the opposite of the rule's premise.
2. **The corner rule is exact:** for both tile sizes, every corner border texel copies the interior
   texel whose direction is the reflection through the -z axis of the adjacent interior corner's
   direction — angle **0.000 deg** — so the four texels around a corner are the four directions
   symmetric about the pole and their bilinear mean is the pole's value.
3. **The edge rule is continuous, and a wrong rule is not:** filling a tile with the directional field
   at each texel's decoded direction, applying the border rule and sampling bilinearly at 200 000
   uniformly distributed normals, the largest error on border-touching normals **equals** the largest
   error in the interior (0.0934 = 0.0934 at 6x6, 0.0351 = 0.0351 at 16x16) — no jump at the seam —
   while the same test with the along-edge mirror removed doubles the seam error (0.151, 0.054). The
   control fails; the test discriminates.
4. **A correct 6x6 border still produces a touching/interior gap for this field.** In the same model
   the median est/truth ratio is 0.985 in the interior and **1.167** on border-touching normals with
   the correct rule (1.042 at 16x16): the fold stretches the octahedral map near the edges, so bilinear
   interpolation of a smooth directional field overshoots there. K3's +-0.02 band was fixed a priori
   without modelling that intrinsic seam error; it cannot separate a correct border from a wrong one
   for a non-flat field. **K3 is recorded as a mis-calibrated indicator (the programme's fourth
   indicator whose a-priori threshold did not model its own quantity), not as a finding about the
   border.** The engine's measured gaps (0.02-0.07) are smaller than the model's because the atlas
   holds cosine-weighted means, not point samples, and the reference is the cache's SH, not the field.

**What is established for the border:** correct at the corners exactly, continuous at the edges to
the interior's own interpolation error, and — under a uniform field — the consumer reads the two
paths equally to within 1 % (H-56). What remains open is only the fidelity of a 6x6 octahedral atlas
to a directional field, which is resolution, not the copy.

**On the K1 spread (p10 ~0.95, p90 ~1.14):** the expected per-normal difference between two
different reconstructions — a 6x6 atlas sampled bilinearly and an L2 SH — of the same field; the
medians agree within 2.3 %. Not a defect in either path; noted for step 5, where it sets the floor
on what "agreement" can mean per pixel under real lighting.


## Review step 4 — depth moments and Chebyshev with known distances — pre-registration (2026-09-18, before capture)

The visibility test never showed under the uniform (K2) or the direction-only (step 3) fields: every
probe held the same value, so any normalised weighting returned it. Step 4 makes the probes differ and
the distances known, so the consumer's Chebyshev weighting can be predicted offline from geometry and
compared with what it does.

Instrument, diagnostic only: `KnownRadianceMode = 2` — both writers return L on probes whose grid
cell has odd parity (x+y+z) and L/4 on the others; `KnownDistance = D` (cm) — every DDGI ray reports
the hit distance D (`KnownDistanceMode` 0), or `D (0.75 + 0.25 dot(dir, d0))` (mode 1), so the depth
atlas holds known moments. Both echoed in the identity line.

The consumer's DDGI query (`deferred_lighting_ps.hlsl:636-724`), which the offline model reproduces:
the 8 corners of the pixel's grid cell, trilinear weights, per corner `visibility = 1` if the distance
to the probe is `<= meanD`, else `(variance / (variance + diff^2))^3` with `variance = max(meanD2 -
meanD^2, 1e-4)`; irradiance = sum(w vis L) / sum(w vis). Under a constant D the moments are D and D^2,
the variance is the 1e-4 floor and the test is binary at `distToProbe = D` (a 0.1 cm excess already
gives ~1e-9). The pixel's world position comes from the region dump's recorded view distance
(`regiondump.Frame.world_positions`), the grid origin and spacing from the DDGI snapshot line.

* capture: as the consumer stage (Camera C, still, cache on, TAA on, `KnownRadiance = 1`,
  `KnownRadianceMode = 2`, RT on with the DDGI path), 2 repeats, the same three ROIs, frames 200-202:
    FAR    KeepDepth 0, KnownDistance 0 — the far distance, visibility 1 everywhere (the reference)
    D300   KnownDistance 300, mode 0 — between the corner distances of a 266.67 cm cell (0-462 cm),
           so some of a pixel's 8 probes fail the test and some pass
    D400   KnownDistance 400, mode 0 — a second, distinct visibility situation (fewer rejected)
    DDIR   KnownDistance 350, mode 1 — the moments vary across each depth tile: the depth border and
           the bilinear read of the moments are exercised
* **K0:** identity lines echo the values; per probe outside geometry the writers' means equal L on odd
  cells and L/4 on even cells (+-1e-3), both writers.
* **K4 (Chebyshev, constant D):** per pixel with geometry, measured `R_m = diffuseIndirect(D) /
  diffuseIndirect(FAR)` against the model's `R_p`; pixels within 1 cm of a probe's boundary
  (|dist - D| < 1 cm) and pixels the model leaves with no passing probe (the consumer then falls to the
  cache) are excluded and their fractions reported. **Band, fixed now: median of R_m / R_p within
  1 +- 0.05 on every ROI, for D300 and for D400, and >= 90 % of pixels within +-0.05 of their
  prediction.** The constant-D case is exact in the model, so the band is the consumer stage's.
* **K5 (depth atlas, directional D):** the same statistic with the moments predicted as the writer's
  cos^2-weighted expectations over the hemisphere around the sampling direction (mean
  `D (0.75 + 0.1875 dot(d, d0))`, second moment likewise), an expectation over the per-frame
  rotations the EMA averages. **Band, fixed now and looser for that reason: median within 1 +- 0.10,
  >= 80 % within +-0.10.**
* **C:** repeats agree on the K4 and K5 medians to 1e-3.

  The reading, fixed now (K4 and K5 read separately):
    CHEBYSHEV OK / DIFFERS   K4 inside / outside its band on both D300 and D400 — the consumer's
                             visibility weighting is / is not what its code says under known moments.
    DEPTH BORDER OK / DIFFERS  K5 inside / outside its band.
    MIXED                    anything else, reported as such.

What this cannot say: whether Chebyshev is the right visibility model for this scene; anything about
real lighting (step 5).

## Review step 5 — the real-lighting comparison repeated on the corrected build — pre-registration (2026-09-18, before capture)

`38-ddgi-sdf-parity` measured, at the consumer under real lighting (Camera C, final gather off):
`ddgi_directional / sdf = 0.5113`, `ddgi_flat / sdf = 1.6067`, `ddgi_directional / ddgi_flat = 0.3182`,
with the view-distance control at 0.00 relative. Since then two writer/consumer defects were corrected
on this build: the unwritten atlas border (H-55/H-56) and the DDGI's windowed ray set (H-57). Step 5
repeats that study **unchanged** — same pose, rectangle, conditions, controls C1/C2, criteria P1/P2
(the task's +-10 % band), 2 repeats — so the only difference between the two results is the build.

* Output `57-ddgi-sdf-parity-corrected`. The original report and its numbers stay as they are.
* **Reading, fixed now:** P1 within 1 +- 0.10 -> the consumer-level parity the task asked for holds
  on this pose; outside -> it does not, and the remainder is **not** attributed to anything by this
  step. The change from 0.5113 is attributed to the two corrections **together**; this study cannot
  split it between them (a split would need the corrections applied one at a time, which is not
  pre-registered here and is not run).
* What it cannot say: anything about the bounce-dominated interior (the review's next item), about
  temporal behaviour, or about which path is right — parity is agreement, not correctness.

## Review step 4 — result (2026-09-18): CHEBYSHEV OK, DEPTH BORDER OK

`56-consumer-known-chebyshev`, status complete, 4 conditions x 2 repeats, identical to the last digit,
C passed. **K0 held:** the per-probe parity field was stored with zero error on all 3495 probes by both
writers, in every capture; the identity lines echo the distances and modes asked; grid origin
(-2266.7, -2533.3, -2000.0), spacing 266.67 cm.

| condition | ROI | measured median | predicted median | measured / predicted (median, p10, p90) | within band |
|---|---|---|---|---|---|
| D300 | shadow_wall | 1.0037 | 1.0037 | 1.0000 (0.9999, 1.0000) | 100 % |
| D300 | lit_wall | 1.0027 | 1.0027 | 1.0000 (0.9999, 1.0002) | 100 % |
| D300 | centre | 1.0000 | 1.0000 | 1.0000 (1.0000, 1.0000) | 100 % |
| D400 | all three | 1.0000 | 1.0000 | 1.0000 | 100 % |
| DDIR | shadow_wall | 1.0005 | 1.0003 | 1.0005 (0.9994, 1.0020) | 100 % |
| DDIR | lit_wall | 0.9765 | 0.9757 | 0.9998 (0.9962, 1.0022) | 100 % |
| DDIR | centre | 0.9873 | 0.9851 | 1.0001 (0.9991, 1.0090) | 100 % |

**Verdicts by the pre-registered rules: CHEBYSHEV OK (K4) and DEPTH BORDER OK (K5).**

### The bands were too loose to make those verdicts falsifiable on their own — and the data is far
### tighter than the bands (post-hoc, so labelled)

The predicted medians sit within 0.4 % of 1 for a constant D, so a consumer that ignored the
visibility test entirely (visibility = 1, predicted = 1) would also have passed K4's +-5 % band. The
pre-registered band did not model the effect size; that is the programme's fifth such indicator and it
is recorded as one. What makes the result falsifiable is the per-pixel comparison, read afterwards:

| condition | ROI | predicted R_p per pixel (min .. p10 .. p90 .. max) | pixels with a rejected probe of weight > 1e-3 | mean abs error, model | mean abs error, null (visibility 1) |
|---|---|---|---|---|---|
| D300 | shadow_wall | 0.975 .. 0.989 .. 1.016 .. 1.035 | 87.1 % | **7e-5** | 7.5e-3 |
| D300 | lit_wall | 0.963 .. 0.986 .. 1.020 .. 1.053 | 89.4 % | **9e-5** | 9.8e-3 |
| D300 | centre | 0.972 .. 0.979 .. 1.000 .. 1.027 | 23.2 % | **2e-5** | 5.3e-3 |
| D400 | shadow_wall / centre | 1.000 (no probe rejected) | 0.0 % | — | — |
| D400 | lit_wall | 0.998 .. 1.000 .. 1.000 .. 1.002 | 4.8 % | 2e-5 | 1.5e-4 |
| DDIR | shadow_wall | 0.938 .. 0.986 .. 1.006 .. 1.012 | 82.7 % | **1.0e-3** | 5.8e-3 |
| DDIR | lit_wall | 0.811 .. 0.931 .. 1.077 .. 1.089 | 99.3 % | **2.1e-3** | 5.1e-2 |
| DDIR | centre | 0.928 .. 0.947 .. 1.001 .. 1.050 | 74.3 % | **2.7e-3** | 2.1e-2 |

* Under a constant known distance the consumer's per-pixel answer matches the model of its own code
  — trilinear weights, the binary test at `distToProbe = D` with the 1e-4 variance floor and the
  cubic sharpening — to **1e-4**, where the per-pixel prediction ranges over +-4 % and the null model
  is wrong by 100x more. The Chebyshev math does what the code says.
* Under a directional known distance the answer matches the writer's cos^2-weighted moments, sampled
  bilinearly through the filled 16x16 depth tiles, to **1-3e-3**, where the prediction ranges over
  +-10 % and the null is wrong by 25x more. The depth atlas, its border and its read are consistent.
* **D400 is a weak situation** (a rejected corner on 0-5 % of pixels, all of tiny weight): it cannot
  fail and is reported, not counted. D300 and DDIR carry the result.
* The residual 1-3e-3 under the directional distance is the expectation-vs-realisation gap of a 32-ray
  estimate averaged by the EMA; not attributed further.

**What is established.** The consumer's visibility weighting and the depth atlas are correct with
respect to the consumer's own definition, on known inputs. **What is not.** Whether that definition —
Chebyshev on cos^2-weighted moments with a 1e-4 floor and a cubic sharpening — is the right visibility
model for Sponza's walls; and anything under real lighting (step 5, running).


## Review item 2 — the indirect term as the sole remaining writer difference — pre-registration (2026-09-18, before capture)

Where is Sponza bounce-dominated? From the stage-3 logs (real lighting, frame 120): the cache's probe
radiance inside the building is ~80 % sky and ~20 % bounce (indirect share median 0.19-0.22 on
layers z = 8-13); the only probes whose share exceeds 0.5 are the sky-blocked ones, whose mean is
~0 (bounce 0.004). Under this sun a bounce-dominated interior is a dark one, and validating a term
there means comparing two near-zeros. A configuration where bounce carries real energy (another sun
angle, a local light) is a separate, later measurement and is **not** run here.

What can be tested now, exactly: after H-52 (fetch and hit classification equal on identical
directions), H-57 (the same stratified ray set in both writers) and H-55..59 (the consumer equal on
known inputs), the only writer-side difference left by the code is the cache's indirect term at hits
(`albedo * indirect`, absent from the DDGI). So the prediction is:

    ratio_p(probe) = (skyLum + hitSunLum) / (skyLum + hitSunLum + hitIndirectLum)   from the cache's own split

and the DDGI/RC per-probe mean-radiance ratio must equal it.

* capture: the stage-3 protocol unchanged (`run_probe_hit_identity.py`: Camera C, `DdgiSequence=1`,
  `ProbesPerFrame=4096`, 2 repeats) on the corrected build; K0/S0/P1 as there. Output
  `58-probe-indirect-residual`.
* **R1 (writers):** on probes outside geometry with cache mean radiance > 0.05 (the dark ones carry
  no information), measured ratio_m = DDGI mean / RC mean against ratio_p: median of ratio_m / ratio_p
  within **1 +- 0.05**, and >= **90 %** of probes within +-0.05 of their prediction. A null control
  that must fail: ratio_p = 1 (no indirect term) — its error must exceed the model's.
* **R2 (reported, not judged):** the indirect share itself, by grid layer, as the size of the term the
  DDGI lacks at this pose.

  The reading, fixed now:
    SOLE RESIDUAL   R1 holds — at the writers, the whole remaining DXR/SDF difference at this pose is
                    the cache's bounce term, quantified per probe.
    NOT SOLE        R1 fails — something else still separates the writers; the direction and size of
                    the residual are reported and nothing is attributed.

Whether the DDGI *should* carry such a term, and how, is a decision for after the measurement; this
step does not choose it.

## Review step 5 — result (2026-09-18): parity still outside the band, moved by +0.08

`57-ddgi-sdf-parity-corrected`, status complete, the 38-ddgi-sdf-parity study unchanged (Camera C,
frames 237-268, 3 conditions x 2 repeats). C1 held (view distance identical across conditions, 0.00
relative); C2 held (the switch acted, 4.0e-4 against a repeat spread of 1.1e-9).

| | 38 (2026-09-16, before) | **57 (corrected build)** |
|---|---|---|
| sdf, diffuse_indirect | 9.75e-04 (unchanged, no DDGI on that path) | 9.747762e-04 |
| ddgi_flat | — | 1.269720e-03 |
| ddgi_directional | — | 5.784011e-04 |
| **P1 ddgi_directional / sdf** | **0.5113** | **0.5934** (outside +-10 %) |
| P2 ddgi_directional / ddgi_flat | 0.3182 | 0.4555 (outside +-10 %) |

**Reading by the pre-registered rule: P1 outside 1 +- 0.10 — the consumer-level parity does not hold
on this pose.** The change from 0.5113 to 0.5934 is attributed to the two corrections together (the
atlas border, H-56; the ray set, H-57); this step cannot split it. **The remainder is not attributed
here.**

### What the remainder can and cannot be, given what is now established

* Not the consumer's treatment of stored energy (H-56: equal to 1 %), nor its visibility math or
  depth atlas on known inputs (H-59), nor the sky fetch or the hit classification on identical
  directions (H-52), nor the ray set (H-57).
* What the code still leaves, under real lighting: (a) the cache's indirect term at hits, which the
  DDGI lacks — the writer-side prediction of review item 2, pre-registered above (expected ~0.8 at
  this pose from the cache's own split); (b) **the visibility test under real depths** — the DDGI
  path drops probes behind walls, which is what Chebyshev is for, while the cache path has no
  visibility and takes light from occluded probes. At a fully shadowed pose under gallery ceilings
  that difference is expected to be large, and it is a design difference, not a defect: parity in
  the sense of P1 was never a correct target if one path leaks. (c) Temporal: both writers blend at
  alpha 0.12 with per-frame rotations; the atlas's per-texel estimates are noisier than an SH's.
  None of the three is measured under real lighting yet; (a) and (b) are separable with the
  instrument already built plus one switch, and that is the next pre-registration.


## The remainder decomposed: visibility vs writers, under real lighting — pre-registration (2026-09-18, before capture)

Instrument: `r.GI.Diagnostic.DdgiFarDepth = 1` makes the DDGI's DEPTH pass store the far distance for
every ray while the irradiance pass traces the real radiance — the consumer then applies visibility 1
on real energy. With it, on the same pose, rectangles and frames:

    V = DDGI / DDGI_FAR        the energy the Chebyshev test removes under real depths
    W = DDGI_FAR / SDF         what separates the two paths with visibility out of the way
    P1' = DDGI / SDF = V x W   (algebraic on the same captures; a consistency line, not a criterion)

* capture: Camera C, still, real lighting, the consumer-stage base with the known-radiance switches
  OFF (`KnownRadiance = 0`), final gather / SDF reflections / SSGI / GTAO off, cache on with the whole
  grid, TAA on; conditions SDF (RT off), DDGI (RT on, real depths), DDGI_FAR (RT on, `DdgiFarDepth = 1`);
  2 repeats; region dump `components` on the three ROIs, frames 200-202. Output
  `59-consumer-visibility-split`.
* **K0:** the DDGI identity line echoes `farDepth` as asked; the two DDGI captures share the writer's
  irradiance identity (same sequence label, same sky, same sun).
* **V, W:** per pixel with geometry, luminance of `diffuseIndirect`; median, p10, p90 per ROI.
* **Expectation, stated now and reported against, not judged:** W's median should sit near the R1
  writer-side prediction (the cache's indirect share at this pose, ~0.8 from the stage-3 split) to
  within +-0.10 — two aggregations of the same difference; V's size is the finding, whatever it is.
* **C:** repeats agree on the V and W medians to 1e-3.

What this decides: how much of the remaining DXR/SDF gap at the consumer is the visibility test and
how much is the writers. What it does not decide: whether the visibility test is right for the scene
(the cache path has none and therefore leaks; the DDGI path may reject correctly or too much), which
is a question about the lighting model, not about parity — and it is not decided here.

## Review item 2 — result (2026-09-18): NOT SOLE — the indirect term plus the escape count

`58-probe-indirect-residual`, status complete, 2 repeats identical, Q0/S0/P1 held; 3251 probes with
cache mean > 0.05.

**R1 by the pre-registered rule: fails — NOT SOLE.** DDGI/RC measured median 0.9652 against the
predicted 0.9518; measured/predicted median 1.0043 but p90 1.128, only 75.3 % within +-0.05; the model
beats the null (|err| 0.044 vs 0.064) but not by much. The layer table says where and why:

| layers | indirect share | measured DDGI/RC | R1 prediction (no indirect term) | + the DDGI's own sky term |
|---|---|---|---|---|
| 0-3 (below the floor) | 0.03-0.05 | 0.9727 / 0.9652 / 0.9595 / 0.9568 | 0.9727 / 0.9642 / 0.9593 / 0.9543 | same |
| 4-6 | 0.06-0.08 | 0.9467 / 0.9390 / 0.9309 | 0.9436 / 0.9335 / 0.9208 | 0.9465 / 0.9377 / 0.9308 |
| 8-13 (inside) | 0.06-0.08 | 0.99 / 0.98 / 0.97 / 0.97 / 0.97 / 0.97 | 0.94 / 0.93 / 0.92 / 0.92 / 0.92 / 0.93 | 1.00 / 0.99 / 0.98 / 0.97 / 0.98 / 0.98 |
| 14-15 (above) | 0.05 / 0.02 | 0.9932 / 1.0444 | 0.9495 / 0.9768 | 1.0207 / 1.0575 |

* **Below the floor the prediction is exact** to the third digit: there the two writers escape the
  same rays, and the whole difference is the cache's indirect term (3-8 % even there — bounce off the
  floor's underside).
* **Inside the building the DDGI is brighter than "the cache minus its bounce"** — because it escapes
  more rays than the cache: its per-ray sky luminance is **6.5-9.2 % higher** on layers 8-15 (the
  `sky_D / sky_R` column of the post-hoc table). H-52 had measured that as "one ray in 32 in the DDGI's
  favour"; a sky ray at ~2.9 in place of a hit at ~0.3 is worth ~4 % of a mean of ~1.9, per ray.

### Post-hoc decomposition on the same logs (labelled)

Both quantities are recorded per probe, so the prediction can be refined after the fact: measured =
(sky_D + hit_D) / (sky_R + sun_R + ind_R). Inside the building: R1's prediction (identical
classification assumed) reads m/p **1.061**, 44 % within +-0.05, |err| 0.098; adding the DDGI's own
sky term reads **0.9968**, 85.5 % within, |err| 0.027. The DDGI's hit term at this pose is ~0 per ray
against the cache's sun term of ~0.011, a -0.4 % residual. **At the writers, inside the building, the
remaining difference is two terms of opposite sign and similar size: the cache's bounce (-7 to -10 %)
and the DDGI's extra escapes (+6.5 to +9 %).** Neither is a defect on its face: the first is a missing
term the review already named; the second is where the distance field says "surface" and the
triangles say "opening" — consistent with the field being conservative (`r.GI.MeshSdf.TwoSided=1`
in the Sponza project adds a surface bias that thickens every mesh), which is a candidate, not a
finding. Which writer is right at those rays is not measured.

Limit: the identity measured = (sky_D + hit_D)/(sky_R + sun_R + ind_R) does not close exactly on
every probe (max 0.09): one of the cache's recorded hit terms is not normalised the same way as its
mean (the per-hit means are rescaled by the hit fraction here). It closes in the median; the
per-probe residual is reported and not interpreted.

**What this settles for the review's item 2.** The indirect term's size at this pose is 7-10 % of
the cache's probe radiance inside the building (2-5 % above it, 3-8 % below the floor), and it is not
the only writer-side difference: the escape count is of the same size and the opposite sign. A
bounce-dominated interior where the term would be large does not exist on this scene under this sun
(the sky-blocked probes are ~0 on both writers); validating it there needs another lighting
configuration, which is a separate measurement.

## The remainder decomposed — result (2026-09-18): two visibility models, not one gap

`59-consumer-visibility-split`, status complete after one gate correction, 2 repeats identical, C
passed. **K0 as first written stopped the study on its own defect:** it compared the sky cubemap's
bindless SRV index across processes (37223 with DXR off, 37225 with it on — the same cubemap in a
different heap slot). The index is a handle, comparable only within a process; the gate now checks
that the cache and the DDGI share the SRV inside each DXR-on process and that the identity tuple
(mip, swizzle, rays, multiplier) matches across processes. Recorded as a design error (over-strict),
the captures were not repeated.

### Absolute levels — `diffuseIndirect` luminance per ROI, repeat 0 (repeat 1 identical)

| ROI | condition | median | mean | pixels at zero |
|---|---|---|---|---|
| shadow_wall | SDF | 1.9e-05 | 1.05e-04 | **20.6 %** |
| shadow_wall | DDGI | 2.4e-04 | 3.43e-04 | 1.1 % |
| shadow_wall | DDGI_FAR | 2.1e-04 | 3.22e-04 | 0.7 % |
| lit_wall | SDF | **0** | 9.7e-06 | **96.0 %** |
| lit_wall | DDGI | 1.4e-03 | 2.12e-03 | 0.0 % |
| lit_wall | DDGI_FAR | 1.5e-03 | 2.19e-03 | 0.0 % |
| centre (floor) | SDF | 8.7e-04 | 9.44e-04 | 1.5 % |
| centre (floor) | DDGI | 6.8e-04 | 7.31e-04 | 0.0 % |
| centre (floor) | DDGI_FAR | 4.6e-04 | 5.16e-04 | 0.0 % |

### The decomposition, as ratios of means (energy-weighted, the aggregation of 38/57)

| ROI | V = DDGI / DDGI_FAR (Chebyshev under real depths) | W = DDGI_FAR / SDF (writers + representation + the cache's own rejection) | P1 = DDGI / SDF |
|---|---|---|---|
| shadow_wall | 1.064 | 3.06 | 3.26 |
| lit_wall | 0.967 | 225 | 217 |
| centre (floor) | 1.416 | 0.547 | 0.775 |

Per-pixel medians (the study's printed statistic) tell the same story with wider tails (V 1.09 / 1.00 /
1.50; W 8.7 / 3.3 / 0.53; P1 9.4 / 2.9 / 0.79). The expectation "W near R1's 0.9652" is **not met**
anywhere; it assumed the two consumer paths treated the same stored energy the same way, which
H-56 established for a uniform field and which real lighting shows to be false — for a reason in the
code, not in the arithmetic:

**The two paths implement different visibility models at the consumer.** The cache's query
(`deferred_lighting_ps.hlsl:774-812`) weights each of the 8 probes by
`saturate(dot(geometricNormal, toProbe))^2` — probes behind the surface's tangent plane get zero —
and skips probes flagged buried (`sh[6].w <= 0`); when every probe is rejected the irradiance is
**zero by design** ("an unlit enclosure really does receive no bounce light"). On Sponza's walls,
whose cells hold probes on the far side of the wall and probes inside the masonry, that is 96 % of
`lit_wall` and 21 % of `shadow_wall`. The DDGI's query has no facing weight and no validity flag; its
visibility is the Chebyshev depth test, which admits probes in front of the wall and rejects occluded
ones by distance. So on the walls the DDGI path is 3x to 200x brighter than the cache path (the cache
is black), and on the floor — where the facing test admits the four probes above and the Chebyshev
test only partly rejects the four bright probes below (V = 1.42) — the cache path is brighter (P1
0.77). **The rectangle mean of 38/57 (0.59) is the energy-weighted mix of a floor at 0.77 and walls
at 3-200x; it was never one number to bring into a +-10 % band.**

**What the review's step 5 question gets as its answer.** The two corrections (border, ray set)
removed the consumer's reading loss and the sampler's bias, which is what the known-input stages
established. What remains between the paths under real lighting is (i) the cache's bounce term and
the DDGI's extra escapes at the writers (H-61, +-7-10 %, opposite signs) and (ii) at the consumer, the
choice of visibility model — a facing-plane-plus-validity rejection that blackens walls against a
depth test that lights them. (ii) dominates. It is a design decision, not a defect in either path's
arithmetic, and it is not decided here.

**Not measured, recorded as the reading it invites:** the cache path's wall irradiance is a
renormalised mean over the few probes that survive rejection; when the surviving set changes (a
probe's validity flag at a recentre, an SH going negative and clamping to zero, a cell boundary),
the wall's value can jump between zero and a renormalised bright value. That is a candidate
mechanism for "dark parts flickering with the radiance cache on" — it belongs to the flicker track,
which stays separate, and no temporal measurement was made here.

## Review of 2026-09-18 — corrections and decision (appended; nothing above is rewritten)

**H-62 over-claimed.** The sentence "a design decision, not a defect" is withdrawn. What the code
shows is that the cache query *can* reject all eight probes and return zero; it does not show that
doing so on whole walls is adequate. The zeros may come from insufficient probe coverage, from the
validity classification, from a facing weight unsuited to this configuration, or from the absence of
any handling for queries without support — and the DDGI path lighting those walls does not prove it
right either: it may be leaking. The decomposition also did not isolate the cache's rules one by one
(it forced the DDGI's depth far while leaving the cache's rejections in place, and it compares an
octahedral atlas with an SH on top). Corrected reading: **the remaining gap is dominated by what the
two consumer queries do with visibility; which of them is wrong, and where, is not established.**

**H-61 did not close per probe.** The refined prediction reaches a median of 0.9968, but 85.5 % of
the probes sit within +-0.05 against the 90 % the rule asked. The decomposition explains most of the
writer-side difference; it does not close it.

**Scope of the parity measurements.** 38/57/59 ran with the final gather off, as the study
pre-registered; the Sponza project runs it on. A similar query exists inside the gather and the
reflections, but the consumer-only conclusions do not transfer to the visible flicker by themselves.

**Report overwrite (organisation).** The re-analysis of 59 with `--reuse` wrote its report under the
full run's filename instead of `runner.report_path`'s `.reuse.json`, so the STOPPED first report was
replaced; the captures are intact. Eight studies written this week bypassed the mechanism; all eight
now route through it (`run_consumer_known_radiance/_directional/_chebyshev/_visibility_split`,
`run_consumer_border_test` has no reuse, `run_probe_indirect_residual`, `run_probe_hit_identity`,
`run_probe_sky_parity_full`, `run_gdf_field_compare`).

### Decision (the reviewer's, recorded)

1. Keep the two corrections (atlas border, DDGI ray set), subject to the normal regression checks.
2. Before any change to the visibility model: **instrument the black regions** — per pixel, the
   cache query's total surviving weight, the number of valid probes, and the reason each probe was
   rejected — and confirm that the zeros come from lack of support.
3. Test the rules separately in a simple scene with a known reference: a lit wall, a wall blocking
   light, probes on both sides. A solution must recover lighting without passing through walls.
4. The temporal flicker hypothesis may be measured, **on the active path with the final gather on**:
   whether changes in the probe set or weights precede the flashes. No smoothing, no removal of
   rejections yet.
5. The DDGI's indirect feedback waits for that isolation, in a bounce-dominated interior.


## Support in the black regions — pre-registration (2026-09-18, before capture; review decision, item 2)

Before any change to a visibility model: do the cache path's zeros on Sponza's walls come from lack of
support — every one of the eight probes rejected — and by which rule?

Instrument, diagnostic only, two new LIGHTING_EXTRA records (11 -> 13; the packed layout and
`regiondump.CURRENT_LAYOUT` move together, the runner's layout gate verifies the binary):
* `rc_support` (+11): x = the cache query's surviving weight `rcTotalWeight`; y = probes with a facing
  weight > 0 (in front of the geometric tangent plane); z = probes valid (`sh[6].w > 0`, not buried);
  w = probes surviving both (the ones that contributed).
* `ddgi_support` (+12): x = the DDGI query's visibility-weighted weight sum; y = probes with
  Chebyshev visibility > 1e-3; z = probes rejected (<= 1e-3); w = probes with a trilinear weight > 0.

Capture: Camera C, real lighting, the split study's SDF and DDGI conditions (final gather off — the
same scope as 38/57/59, stated), 2 repeats, the three ROIs, frames 200-202. Output `60-consumer-support`.

* **S0 (layout):** the runner's layout gate passes and both channels read finite values on every
  geometry pixel.
* **S1 (the zeros):** on pixels where the SDF path's `diffuseIndirect` is exactly 0, per ROI: the
  fraction with `rc_support.w == 0` (no surviving probe). **Reading fixed now: SUPPORT if >= 99 % on
  every ROI with >= 100 black pixels; OTHER if not — then zeros exist with support and something else
  produces them (an SH clamped to zero, or a weight underflow), reported by count.**
* **S2 (which rule):** among the unsupported black pixels: the share with y == 0 (facing rejected all),
  with z == 0 (validity rejected all), with both, and with y > 0 and z > 0 but w == 0 (the survivors of
  each rule are different probes). Reported, no verdict.
* **S3 (context):** on non-black SDF pixels the distribution of w (1..8) and x; on the DDGI path per
  ROI the distribution of rejected probes (z) and the share of pixels with y == 0 (the DDGI's own
  no-support case, which its code then hands to the next source).
* **C:** repeats agree on S1's fractions to 1e-3.

What this decides: whether "black" is "unsupported" and which rule empties the support. What it does
not decide: whether the rules are right — that is the reference-scene test (decision item 3).

## Support in the black regions — result (2026-09-18, H-63)

Study `scripts/run_consumer_support.py`, output `.screenshots/flicker-elimination/60-consumer-support/`.
Instrument: LIGHTING_EXTRA 11 -> 13 records (`rc_support` +11, `ddgi_support` +12) written by the
lighting PS; `test_region_format.py` 12/12 and `test_binary_layout.py` 6/6 on the new layout. No rule
was changed. Camera C, real lighting, final gather OFF (as 38/57/59), SDF and DDGI conditions x 2.

**Organisation.** The first run captured all four dumps and then STOPPED at S0 on the study's own gate:
the file header packs 0 for a group absent from the capture's field mask (`(2, 5, 0, 0, 13, 0)` for the
`components` preset) and the gate compared it with the unmasked table. The binary gate
(`test_binary_layout.py`, masked form) had passed. The gate now masks the table; the criterion is
unchanged. `consumer_support.json` (STOPPED) is retained; the complete report is
`consumer_support.reuse.json` (same captures, both repeats).

* **S0:** binary layout gate 6/6; both channels finite on every geometry pixel; the per-pixel valid
  count agrees with the writer's own per-probe log (`fieldDistance < 5 cm` = invalid, mapped through
  the eight corner cells) on 100.00 % / 99.96 % / 100.00 % of the geometry pixels — the counters,
  the mapping and the grids (cache and DDGI coincide) are right.
* **S1 — SUPPORT.** Black (diffuse_indirect exactly 0) pixels: shadow_wall 6 709, lit_wall 32 157,
  centre 723 of 49 152 geometry pixels each (three frames); **100 % of them have zero surviving
  probes** on every ROI; no unsupported pixel is non-black; no all-zero record.
* **S2 — which rule.** Facing alone: 0 / 0 / 0. Validity alone (all eight probes classified inside
  geometry): 4 315 / 24 467 / 2. Both: 0. Disjoint survivors (valid probes exist, facing probes
  exist, no probe is both): 2 394 / 7 690 / 721. The facing weight never empties the support by
  itself; the validity classification does on 64 % / 76 % / 0.3 % of the black pixels, and on the rest
  the only valid probes lie behind the tangent plane.
* **S3 — context.** Supported (non-black) pixels rest on ONE probe on 89 % (shadow_wall), 19 %
  (lit_wall: 81 % on two) and 99 % (centre) of them, with a surviving weight sum of median 0.015 /
  0.013 / 0.036 (p90 0.04 / 0.03 / 0.08) — the query renormalises 1-4 % of its nominal weight to full
  strength. DDGI path, same ROIs: black 0; probes rejected (visibility <= 1e-3) 0 on 97 % / 40 % /
  98 % of pixels and never more than 1 (2 on 15 pixels); all eight trilinear weights > 0; weight sum
  median 0.90 / 0.95 / 0.66; the DDGI's own no-support branch never runs.
* **C:** the two repeats are identical to the pixel.

**Reading (pre-registered): SUPPORT.** The cache path's black walls are unsupported queries, and the
support is emptied by the validity classification — a probe whose GDF distance is below
`kSurfaceEpsilon` = 5 cm is written with all-zero SH and validity 0 (`radiance_cache_trace_cs.hlsl`
~339) — never by the facing weight alone. What this does not decide: whether the classification is
right. That is the reviewer's item 3.

### Post-hoc, descriptive (not pre-registered; reported, no verdict)

* The whole grid: 601 of 4 096 probes invalid (538 with a negative field distance, 63 in [0, 5) cm),
  occupying cells x 5..12, z 7..14, y 2..15 — the building. Around the black pixels: shadow_wall 13 of
  its 14 corner probes invalid (field distance -12 .. -66 cm), lit_wall its twelve most frequent all
  invalid (-4 .. -52 cm), centre 11 of 12. The non-black pixels of shadow_wall and centre are carried
  by single probes reading +13 / +14 cm.
* The same probes in the DDGI capture (`hitDist=` per probe, mean over 32 DXR rays, `hitCount` 32):
  104-481 cm for the probes listed above, sky fraction 0. Over the 601 GDF-invalid probes: hitDist
  median 266 cm (p10 66, p90 529); only 11.8 % have a mean hit distance <= |field distance|. Over the
  3 495 valid probes: median 1 267 cm, 0.1 %.
* Two readings fit and are not separated by these data: (a) the field is fat at the probes (a coarse
  cascade, or the sampling) and the validity rule discards free-space probes — the cache path's black
  walls would be a classification defect; (b) the probes are inside geometry and their DXR rays leave
  through back faces — the DDGI path would be leaking exactly where it lights the walls. The DDGI
  trace is an inline `RayQuery` with `RAY_FLAG_NONE` (`ddgi_probe_update_cs.hlsl` ~282), so nothing is
  culled and a probe inside a closed volume would hit back faces at short range; Sponza's walls may be
  open shells, which a mean distance cannot tell apart. The query's `CommittedTriangleFrontFace()` is
  available and not recorded (the readback's "hit side" record holds distance, lit fraction, albedo
  and count — not the facing). **Next (instrument only): record the back-face fraction per probe in
  the DDGI diagnostic and print it in the per-probe line; read it on the same probes; then item 3
  (the rules in a reference scene) with that answer in hand.**
* Regression checks for the two kept corrections: `test_region_format` 12/12, `test_criteria_logic`
  28/28, `test_binary_layout` 6/6 (on the new build). `test_flicker_metrics` reports 11 errors that are
  pre-existing and unrelated: its format-2/3 fixtures with `fields=3, 7 records` are ambiguous among
  the three legacy layouts in `resolve_layout`, a path this change does not touch. The exe-driven
  checks (`test_lighting_components`, `test_region_capture`, `test_diagnostic_resources`,
  `test_gdf_controls`, `test_taa_modes`, `test_correction_gates`) have not been run on this build.


## Back-face fraction at the probes — pre-registration (2026-09-18, before capture; H-63 follow-on)

H-63 left two readings for the 601 probes the cache classifies "inside" (GDF distance < 5 cm) while
their DXR rays travel 104-481 cm: a fat field at the probes, or probes inside geometry whose rays
leave through back faces. The DDGI trace culls nothing (`RayQuery`, `RAY_FLAG_NONE`), so the facing of
the committed triangle separates them.

Instrument, diagnostic only: `hitInfo.z` = `CommittedTriangleFrontFace()`, diagnostic record 0.w = the
back-face fraction over the HIT rays, printed as `hitBackFraction=` in `DDGI transport: probe=`. No
trace, shading or atlas change. Capture: the DDGI condition of study 60 (Camera C, real lighting, final
gather off), 2 repeats, output `61-probe-backface`; the cache's `fieldDistance` per probe from the same
process (identical to the SDF process's on 60, max |diff| 0.0).

* **B0 (instrument changed nothing):** all 4 096 probe lines carry the field; `skyFraction` and
  `hitCount` equal 60/DDGI_r0 exactly and `hitDist` within 1e-3 cm on every probe.
* **B1 (reading, fixed now):** over the GDF-invalid probes (fieldDistance < 5) the median back-face
  fraction, and over the GDF-valid probes the same as the control.
  INSIDE if invalid median >= 0.5 and valid median <= 0.1; FREE SPACE (the field is fat at the probes)
  if invalid median <= 0.1 and valid median <= 0.1; MIXED otherwise, or if the control fails.
  Reported alongside: the split of the invalid probes into >= 0.5 / <= 0.1 / between, and the sixteen
  probes named in H-63 individually.
* **Caveat stated now:** a probe behind a single-sided wall sees back faces only toward that wall; a
  fraction near 0.3-0.5 with hitDist far above |fieldDistance| would say "behind an open shell", which
  the rule "distance < 5 cm -> inside" does not distinguish from "inside a solid". That case is
  reported as MIXED, not decided.
* **C:** repeats agree on B1's medians to 1e-3.

## Back-face fraction at the probes — result (2026-09-18, H-64)

Study `scripts/run_probe_backface.py`, output `.screenshots/flicker-elimination/61-probe-backface/`
(`probe_backface.json`, complete, 2 repeats identical). Instrument: `hitInfo.z` =
`CommittedTriangleFrontFace()`, diagnostic record 0.w, `hitBackFraction=` in the per-probe line.

* **B0:** 4 096 lines carry the field; `skyFraction` and `hitCount` identical to 60/DDGI_r0 on every
  probe, `hitDist` max |diff| 0.00 cm — the instrument changed nothing in the trace.
* **B1 — MIXED (control failed), by the pre-registered rule.** Invalid probes (n = 601): back-face
  median 0.781, split >= 0.5: 325, <= 0.1: 263, between: 13. Valid probes (n = 3 493): median 1.000,
  3 330 of them >= 0.5. The control's premise — "valid probes are in free space and see front faces" —
  was wrong for this grid: 3 072 of the 4 096 probes lie outside the building (cells x < 5, x > 12,
  z < 7 or z > 14) and look at Sponza's inward-facing surfaces from behind. The study did not decide;
  the pre-registered reading stands as MIXED.

### Post-hoc calibration (not pre-registered; reported with its assumption)

* Outside the building footprint (3 072 probes): back fraction 1.000 at every quantile p10..p90.
  Interior, clear of surfaces (field distance >= 100 cm) and enclosed (sky fraction 0), n = 18: back
  fraction 0.000 (p90 0.031), mean hit distance 5.3-7.6 m. The distribution over the 601 invalid
  probes is bimodal (13 between 0.1 and 0.5). Raster and DXR run at the same D3D12 default winding
  (`frontCounterClockwise` never set, no `TRIANGLE_FRONT_COUNTERCLOCKWISE` instance flag).
* **Assumption stated:** consistent winding across the scene's meshes. The bimodality and the two
  calibration sets support it and do not prove it. Under it, back ~ 0 reads "interior free space" and
  back ~ 1 "beyond a surface" (outside, in a wall cavity, under a floor, above a ceiling).
* Under that reading, of the 601 probes the cache discards, **263 (44 %) are interior free-space
  probes** with mean hit distances of 2-5 m, and 325 (54 %) are beyond a surface. Around the ROIs
  (corner probes of the black pixels): shadow_wall 7 interior discarded, 4 beyond-surface discarded,
  and ONE valid probe — 2250 (+13 cm, back 1.000, hit distance 464 cm) — which carries 89 % of the
  ROI's supported pixels; centre's supported pixels rest on 2265 (+14 cm, back 1.000, 504 cm);
  lit_wall's on 2777 / 2776 (+13 cm, back 1.000, 370 / 414 cm). **On the cache path the walls are
  black where the interior probes were discarded and, where lit, lit by probes beyond a surface.**
  The interior probes next to the walls — 2507, 2252, 2268, 2508, 2523, 2524, 2235, 2491, 2793,
  2810 — read front faces on >= 90 % of their rays and are discarded at field distances of -12 to
  -66 cm.

### Code facts (independent of the assumption)

* `global_sdf_composite_cs.hlsl` stores `unsigned two-sided mesh distance - surfaceBias - globalBias`:
  `surfaceBias` is one mesh-SDF voxel for two-sided meshes (`r.GI.MeshSdf.TwoSided = 1` in these
  captures), `globalBias = clipmapExtent / resolution` = 2500 / 64 = 39 cm on clipmap 0 (156 cm on
  clipmap 1). A negative value therefore means "within roughly half a metre of any surface, on either
  side", not "inside a solid"; the field has no side.
* The cache's validity rule (`radiance_cache_trace_cs.hlsl` ~339: `SampleGdfSurface(probePos).r <
  kSurfaceEpsilon` (5 cm) -> SH zero, validity 0) cites Majercik 2021 §6.1, which classifies a probe
  by the back-face ratio of its own rays. A field-distance threshold cannot separate a probe 30 cm in
  front of a wall from one 30 cm behind it, and accepts either once it is ~50 cm away.
* DDGI side (study 60): the same eight probes are accepted at 0.90-0.95 total weight with <= 1
  rejected, the beyond-surface ones included. Candidate mechanism, NOT established: the 16x16 depth
  atlas over 32 rays per frame blurs a thin wall's short depth into a wide cone, so the Chebyshev
  bound stays near 1. That belongs to the reference scene.

### What item 3 (the reference scene) must now contain

A thin single wall, lit on one side, dark on the other; probes at ~0.3 m and ~1.5 m on both sides so
that the interior / beyond classification is known by construction; the cache's validity rule and the
DDGI's Chebyshev test evaluated per side. A candidate rule passes only if the dark side stays dark
(no probe from the lit side contributes) and the lit side keeps its lighting (its own near probes are
not discarded). No rule is changed here.


## Review of 2026-09-21 — corrections (appended; nothing above is rewritten)

* **H-63 is sustained within its scope only:** Camera C, final gather off, the QA project
  (`sponza-lighting-camera-c.uzproject`, which leaves `r.GI.GDF.Resolution` at 64 and
  `r.GI.MeshSdf.Resolution` at 32). There, the black pixels have no accepted probe — a concrete
  defect of the lighting query. It does not yet speak for the project configuration or the gather.
* **H-64 is inconclusive.** The control failed, so "44 % of the discarded probes are interior free
  space" and "the walls are lit by probes beyond a surface" depend on reading a back-face hit as
  "beyond a surface". They stay hypotheses until a reference with known sides tests that reading.
* **The field description in H-64 was wrong.** `global_sdf_composite_cs.hlsl` subtracts HALF a mesh
  voxel (`surfaceBias = max(local half-extent) / mesh resolution`, then scaled by `distanceScale`;
  `global_distance_field.cpp` ~254) plus HALF a global voxel (`clipmapExtent / resolution`, applied only
  to two-sided meshes). "One mesh voxel" was wrong. The 39 cm is clipmap 0 at `r.GI.GDF.Resolution` 64,
  i.e. this test's configuration; `sponza.uzproject` sets 256 (9.8 cm) and a mesh resolution of 128.
* **The tests cited did not validate the running renderer.** `test_region_format`,
  `test_criteria_logic` and `test_binary_layout` check formats and logic; the exe-driven checks
  (`test_lighting_components`, `test_region_capture`, `test_diagnostic_resources`, `test_gdf_controls`,
  `test_taa_modes`, `test_correction_gates`) were not run on that build, and `test_flicker_metrics`
  still fails.

### Decision (the reviewer's, recorded)

Stay in followup-01; no new lighting features, no new study sequence. One delivery:
1. a reference with known geometry and sides, including probes inside a wall and in free space;
2. show where the current classification errs against it;
3. a candidate correction behind a switch, the current behaviour kept for comparison;
4. verify that the candidate recovers lighting without passing through walls; then Sponza still and
   in motion.
Removing the rejection is not a candidate: a brighter wall alone does not show correct lighting.

## Probe-side reference and candidate — pre-registration (2026-09-21, before the candidate exists)

**Reference.** `scripts/probe_side_reference.py` holds the geometry, the truth and the generator
(`--generate` writes `sample/content/{scenes,meshes/reference}/probe-side-reference-*` and
`scripts/probe-side-reference-*.uzproject`). At `r.GI.RadianceCache.GridExtent` 2000 the probes lie on
world-fixed planes (j + 1/2) S, S = 266.67 cm (`RadianceCache::Update` snaps the centre to multiples of
S), so geometry placed against the planes puts probes at known distances from known faces, and every
camera sees the same probes. Every box is its own mesh at true size with an identity transform (a
scaled unit cube gets the Gershgorin-conservative field of non-uniform scale — metres of false
"surface" along a slab's long axes, another defect). Sun from +x, 35 degrees.
* SOLID: three sealed rooms. Every boundary but the east wall is a 100 cm slab centred on a probe
  plane (ground 266.67 cm, 3 x 3 tiles), so those probes are inside solid by construction and the east
  wall is the only boundary with free probes on both sides. CORE: the plane x = 400 inside a 100 cm
  wall, 50 cm from each face. NEAR_OUT: 10 cm outside the lit face. NEAR_IN: 10 cm inside the dark
  face. Sealed interiors: the true irradiance from outside light is zero.
* SHELL: one room of a single-sided box facing inward (Sponza's case); planes 30 cm behind its east,
  side and roof faces, 30 cm in front of its west face. Sides follow the faces (the renderer's and
  Majercik 2021 section 6.1's convention).
* Configurations: QA (GDF 64, mesh SDF 32 — the H-63/H-64 configuration) and PROJECT (GDF 256, mesh
  SDF 128 — `sponza.uzproject`).

**Disclosure.** Before this text, three smoke captures (QA, current rule only) validated the layout
and labelling: NEAR_OUT's lit face 100 % unsupported and black while CORE's and NEAR_IN's read 0.417 /
0.418; 10 free probes at 10 cm discarded; 78 probes inside solid accepted; the sealed shell interior
at 0.026 (east face) and 0.055 (floor). The candidate had not been written.

**Candidate** (`r.GI.RadianceCache.ProbeClassification` = 1; 0 = the current rule, default):
1. Side at the probe: the nearest mesh triangle over the scene's instances (the baker's BVH and face
   orientation, i.e. the vertex normals) gives a signed distance d (< 0 = behind the nearest face) and
   the unit direction n from that point to the probe.
2. d < 0 -> invalid (Majercik 2021 6.1's back-face criterion, taken at the probe instead of over its
   rays).
3. d >= 0 but the biased field at the probe < 5 cm (the probes the current rule discards) -> the trace
   origin moves along n until the field is >= 10 cm (the clearance shadow rays already use), at most
   half a spacing; failing that, invalid. The SH stays the grid probe's.
4. A relocated probe carries a guard: every reader of the cache (lighting query, gather/reflection
   hits, multi-bounce) rejects it for a receiver behind the plane of its nearest face
   (dot(p - (P - n d), n) < -5 cm). This replaces, for exactly the probes the band discarded, the
   protection the band gave by accident: a recovered probe near a wall must not light the other side.
5. Nothing else changes (facing weight, trilinear weights, renormalisation, hysteresis).

**Protocol.** config {QA, PROJECT} x rule {0, 1} x 6 cameras x 2 repeats, same binary; BASE as study 60
(final gather, reflections, SSGI, GTAO, auto exposure off; `ProbesPerFrame` 4096; TAA on), 640 x 360,
210 frames, the whole frame dumped at 200-201 (`components`). Probe-level oracle: the DXR writer
(`r.RayTracing` 1, `r.RayTracing.GI` 1, `r.GI.DDGI.Diagnostic` 1) per scene and config, its
`hitBackFraction` per probe. Output `.screenshots/flicker-elimination/62-probe-side-reference/`.

**Readings fixed now.** Lref = mean of CORE's and NEAR_IN's lit faces (their lit-side probes are 197-217
cm away, untouched by either rule) under the same configuration and rule.
* R1 (where the current rule errs), reported per configuration and distance bin: E1 free probes
  discarded, E2 probes inside solid accepted, E3 probes behind the shell accepted.
* R2 (the control H-64 lacked): median DXR back-face fraction on probes inside solid >= 0.9 and on free
  probes of SOLID <= 0.1. If it fails, the face reading stays unvalidated and is not used on Sponza.
* R3 (candidate classification): inside -> invalid on 100 %; free -> valid on >= 99 %; behind with the
  shell as the nearest surface -> invalid on 100 %. Predicted and reported, not a gate: behind probes
  whose nearest surface is the ground stay valid (the rule reads the nearest face only).
* P1 (recovers lighting): candidate, NEAR_OUT's lit face: unsupported <= 1 % and |mean / Lref - 1| <= 0.10,
  in both configurations.
* P2 (no collateral): candidate, CORE's and NEAR_IN's lit faces within 5 % of the current rule.
* P3 (no crossing added): on every sealed label (each room's interior, the shell's interior seen from
  both cameras) the candidate's mean <= the current rule's + max(3 x repeat spread, 1e-3 x Lref).
* P4 (reported): the dark means of both rules against Lref, with the valid other-side probes that
  reach them — crossing the classification does not govern (far probes, no visibility in the query)
  is named as such, not attributed to the candidate.
* C: repeats agree on every label mean within 1 %.
Pass = R3, P1, P2, P3 in both configurations. Only then Sponza: still (Camera C, the H-63 setup and
the project configuration with the gather on) and in motion (per-frame ROI series; the candidate must
not add steps: p99 |frame-to-frame change| per ROI <= 1.1 x the current rule's, and no larger step at
the logged recentres).

### Candidate revised after its own smoke (2026-09-21, before the full study; disclosed)

The pre-registered candidate (side of the NEAREST face) was built and smoked once (QA, three
cameras). Mode 0 on the new binary reproduces the old binary exactly (field distance identical on
every probe, label means identical), so the shared-include refactor of the baker changed nothing.
Mode 1 recovered NEAR_OUT's lit face (0.435 against 0.419/0.418), darkened the sealed room to exactly
0 and the shell interior from 0.026-0.075 to ~5e-7 — but misread 22 of the 256 probes inside solid as
"in front": the nearest face of a probe inside overlapping solids (ground tiles abutting, walls
embedded in the ground, an east wall running inside a side wall) is often an EMBEDDED face seen from
its front; 2 of those were relocated and accepted. Sponza's partitions abut the same way, so the
rule is not kept.

Revision, one change: the classification is Majercik 2021 section 6.1's own criterion — the
back-face fraction of 64 fixed rays (a Fibonacci set, so the answer does not vary from frame to
frame) cast from the probe against the same baked triangles and orientations; inside when more than
a quarter of the rays that hit see back faces (the fractions are bimodal: H-64 found 13 of 601
between 0.1 and 0.5). The nearest face is kept for what it is right about: the direction the trace
origin moves and the guard plane. Everything else, and every reading above, is unchanged. Smoke of
the revision (QA): the 256 probes inside solid invalid (4 of them because their relocation failed
rather than by the rays), the shell interior 0 on every label.

Known before the full study: 4 of the 10 free probes at 10 cm fail relocation in QA — the column
y = +-400 next to the end faces of the side walls, where the field just beyond a mesh's SDF volume is
the conservative lower bound (distance minus displacement) and, filtered at 78 cm voxels, never
reaches the 10 cm clearance within half a spacing. They stay invalid (the current behaviour).

## Probe-side reference — result (2026-09-21, H-65 / H-66)

Study `scripts/run_probe_side_reference.py`, output `.screenshots/flicker-elimination/62-probe-side-reference/study/`
(`probe_side_reference.json`, complete; 2 configurations x 2 rules x 6 cameras x 2 repeats + 4 DXR
oracle runs; C: repeats agree on every label). Montage `probe_side_reference_montage.jpg` (log10
diffuse_indirect, current | candidate).

**Verdict by the pre-registered rule: FAIL** (R3 fails in both configurations). P1, P2, P3 pass in both.

**R1 — where the current rule errs (H-65).**
| | QA (GDF 64 / mesh 32) | PROJECT (GDF 256 / mesh 128) |
|---|---|---|
| free probes discarded (all 10 cm from a face) | 10 of 10 | 10 of 10 |
| probes inside solid accepted | 78 of 270 (the deepest, >= 100 cm) | 256 of 270 (everything deeper than ~25 cm) |
| probes behind the shell accepted, within 50 cm | 8 of 18 | 18 of 18 |
Consequences in pixels, current rule: NEAR_OUT's lit face 100 % unsupported (black) in both
configurations; the sealed shell interior at 18 % (QA) and 54 % (PROJECT) of the lit faces' level;
NEAR_IN's room at 1.2 % — the exterior probe 197 cm outside is the only surviving corner at the wall's
base once the near interior probes are discarded, and the renormalisation gives it full weight.

**R2 — the control H-64 lacked: FAIL, and informative.** DXR's `CommittedTriangleFrontFace()` is
INVERTED relative to the faces as oriented by the vertex normals: median back-face fraction 0.03 on
probes inside solid, 1.00 on free probes (SOLID; SHELL the same way round). The reference's meshes
follow `MeshBuilder::CreateCube`'s winding, which the baker reorients on every triangle (768 of 768),
as it does on Sponza (6171 of 6180, 36900 of 36900 ...), and the TLAS instances do not set
FRONT_COUNTERCLOCKWISE. By the pre-registered rule the DXR face reading stays unvalidated, and H-64's
"back = beyond a surface" may be the wrong way round on Sponza; Sponza is checked separately (S1o).
The candidate's classifier does not use DXR: it reads the baker's vertex-oriented faces, which R3
checks against the truth.

**R3 — candidate classification: FAIL as registered.**
* Inside solid -> invalid: QA 100 % (4 of them only because their relocation failed); PROJECT 98.5 %:
  4 probes inside the ground, 10 cm in front of the east wall's EMBEDDED base (walls reach into the
  ground to seal the rooms), read back-face fractions 0.19-0.23 — the embedded front face and the
  coplanar tile seam at x = 500 are front hits — fall under 0.25, are relocated (15.6 cm) and accepted.
* Free -> valid: QA 99.95 % (4 of the 10 near probes fail relocation: the field dip beyond the side
  walls' SDF volume, stated before the study), PROJECT 100 %.
* "Behind with the shell as the nearest surface -> invalid on 100 %": 8 % in both. The criterion was
  written for the nearest-face rule and counts probes metres above and beside the shell room whose rays
  mostly see the ground. Behind probes within 100 cm of the shell (the ones that share cells with the
  interior): 20 of 20 invalid in both configurations. Reported as failed; the criterion did not model
  the revised candidate.

**P1 (recovers lighting): PASS.** NEAR_OUT's lit face 1.038 x Lref (QA), 1.022 x Lref (PROJECT),
unsupported 0 % (current: 0 and 100 %).
**P2: PASS.** CORE / NEAR_IN lit faces 1.000 / 1.000 (QA), 1.046 / 1.046 (PROJECT). In PROJECT they
brighten because the current rule accepts the dark probes inside the ground below them (E2) — P2's
premise that those faces were untouched by either rule did not hold there.
**P3 (no crossing added): PASS.** Candidate / current, as fractions of Lref: CORE room 0 / 9e-5 (QA),
0 / 1.3e-3 (PROJECT); NEAR_OUT room 0 / 1.6e-4, 0 / 1.4e-3; NEAR_IN room 1.7e-4 / 1.2e-2,
1.5e-4 / 1.3e-2; shell (east view) 0 / 0.18, 9e-8 / 0.54; shell (west view) 2.1e-4 / 0.28,
2.2e-4 / 0.44. What the candidate leaves: exterior probes 197-257 cm from a wall that share a cell
with the 10-30 cm strip at its base (NEAR_IN, the shell's west side) — far probes, no visibility in
the query, present under both rules.

### Deviation, declared before running Sponza (2026-09-21)

The pre-registration said Sponza only after a pass. The candidate did not pass R3. Sponza is run
anyway, as EXPLORATORY, because the user's step 4 asks for it and its outcome is needed for the
decision: whether the candidate recovers Sponza's black walls, whether the classifier and the DXR flag
agree there (the H-64 question), and whether it adds steps in motion. None of it changes the reference
verdict above; no Sponza number is a verdict on the candidate.

## Sponza with the candidate — EXPLORATORY (2026-09-21, declared deviation; not a verdict)

Study `scripts/run_probe_side_sponza.py`, output `.screenshots/flicker-elimination/63-probe-side-sponza/`
(`probe_side_sponza.json`). Same binary as the regression harness (`test_probe_classification.py` 7/7);
it differs from the reference study's binary only by a TechPanel row (F3: "Probe sides: back-face
rays" toggles the candidate live).

**Correction to the pre-registration text.** It quoted the current rule's unsupported fractions as
"20.6 / 96.0 / 1.5 % in H-63". Those are H-62's black-pixel shares (study 59). H-63's are 13.6 / 65.4 /
1.5 % (6 709 / 32 157 / 723 of 49 152), and the current rule reproduces them here to the pixel.

* **S1 (QA, final gather off, Camera C, 2 repeats identical).** Unsupported, current -> candidate:
  shadow_wall 13.6 -> 10.6 %, lit_wall 65.4 -> 31.3 %, centre 1.5 -> 0.03 %. Criterion (<= 5 %) not met.
  What remains, by the probes at the corners: in-band probes the rays call free whose RELOCATION fails
  (2251, 2553, 2794: 145-190 cm without reaching 10 cm of clearance — the QA field near Sponza's large
  meshes is biased by ~90 cm and filtered at 78 cm voxels), and probes the rays call inside (0.86-1.0
  back-face fraction). "lit_wall" is the underside of a vault (normal -z), not a wall.
* **S1o (DXR oracle, candidate on).** The classifier and the DXR flag disagree on 99 % of the 4 094
  probes (agreement 1.05 %, 2.8 % in the band): the same inversion as R2 on the reference. **H-64's
  post-hoc reading is therefore reversed**: the probes the current rule discards next to the walls that
  H-64 called "interior free space" (2507, 2252, 2268, 2508, 2523, 2524, ... DXR front >= 90 %) see BACK
  faces by the vertex orientation (classifier 0.98-1.0), and the probes that carry the supported pixels
  (2250 etc., DXR back 1.000) see FRONT faces (classifier 0.000). The claim "the walls are lit by probes
  beyond a surface" is withdrawn; with the inversion it reads "the discarded probes next to these walls
  are mostly behind surfaces, and the supported pixels rest on free probes". Of the 601 probes in the
  band, the classifier calls 292 inside, 229 recovered by relocation, 80 relocation failed.
* **S2 (project configuration: GDF 256, mesh SDF 128, gather ON, reflections ON; Camera C).** The current
  rule leaves NO unsupported pixel on the three ROIs here (the band is ~28 cm, not ~95) — H-63's black
  walls are a property of the QA configuration. The candidate: shadow_wall's diffuse_indirect record
  black on 9.3 % (unsupported 0.7 %), because the probes that lit it under the current rule (2252,
  2268, 2508, 2524, field 20-31 cm, outside the smaller band) see back faces on 98-100 % of their rays
  and are now rejected, and the one front probe left (2251, relocated 33 cm) evaluates to zero towards
  the wall's normal. In the final linear output (gather on) the ROI mean is unchanged (ratio 1.0000);
  over the whole frame 1.5 % of pixels change by more than 5 % (0.6 % darker, 0.9 % brighter), mostly
  at arch edges and column bases (`S2_cameraC_project.jpg`: current | candidate | relative change).
* **S3 (motion, project configuration, the long sweep: 900 cm, recentres at frames 128, 172, 216 under
  both rules).** p99 |frame-to-frame change| candidate / current: rear_door 0.996, right_shadow 1.000,
  lit_floor 1.000, left_wall 1.000; largest step at a recentre 1.013 / 1.005 / 1.000 / 1.000; the
  candidate-minus-current series jumps by at most 1e-3 frame to frame. No step added.

**Reading (not a verdict).** Where the classification is the cause, the candidate removes the crossing
(the reference's shell and rooms) and recovers near probes; on Sponza it turns the QA configuration's
black walls into partly supported ones, but relocation fails often in that coarse field, and in the
project configuration it changes little that is visible. The pre-registered reference gate failed (R3);
the open defects are named: embedded/coplanar faces fooling the rays, relocation where the field is
the conservative bound, far probes across a wall (the query has no visibility).

### Checks on the final build (2026-09-21, the binary Sponza ran on)

Exe-driven, all run this time: `test_region_capture` 23/23, `test_lighting_components` 10/10,
`test_diagnostic_resources` 10/10, `test_gdf_controls` 5/5, `test_taa_modes` 12/12,
`test_correction_gates` 11/11, `test_binary_layout` 6/6, `test_probe_classification` 7/7 (new, with a
negative control). Format/logic: `test_region_format`, `test_criteria_logic` pass. GI gtests
(`*GI*:*Radiance*:*Gdf*:*MeshSdf*:*DistanceField*:*Ddgi*`) 101/101. `test_flicker_metrics` still fails
with the same 11 errors (format-2 fixtures with fields=3 / 7 records ambiguous in `resolve_layout`),
untouched by this work.

### Scope corrections to the Sponza reading (2026-09-22; the text above is left as written)

* "H-64's post-hoc reading is therefore reversed ... the probes ... see BACK faces" over-states what was
  measured. Measured: on the reference, the DXR flag is inverted relative to the vertex-oriented faces
  (R2); on Sponza, the classifier and the DXR flag disagree on 99 % of the probes (S1o). That the
  vertex orientation is the TRUE side on Sponza is established only for the reference's meshes, not
  for Sponza's open partitions. The reversed reading is a hypothesis conditional on it; what stands
  unconditionally is the withdrawal of H-64's post-hoc claims.
* "H-63's black walls are a property of the QA configuration" over-states S2. S2 changed the field
  resolutions, the final gather and the reflections at once, at three ROIs of one camera. Measured:
  at those ROIs, in the project configuration, the current rule leaves no unsupported pixel.
  Attributing that to the band size needs the project resolutions with the gather off.

## Crossing count for embedded faces — pre-registration (2026-09-23, before any capture of mode 2)

**Defect addressed.** R3 failed in PROJECT on 4 probes inside the ground, 10 cm in front of the east
wall's embedded base: their rays' FIRST hits are the embedded front face and the coplanar tile seam, so
the back-face fraction read 0.19-0.23 (< 0.25). A first hit cannot tell "inside the ground, looking at a
face buried in it" from "in free space looking at a wall".

**Candidate, mode 2** (`r.GI.RadianceCache.ProbeClassification` = 2; 0 and 1 unchanged). The classifier
(`probe_side_cs.hlsl`) also counts, per ray and PER MESH, the signed crossings along the whole ray up to
the trace distance (+1 per back-face hit = leaving that mesh's solid, -1 per front-face hit = entering).
A ray votes inside when some mesh is left more often than entered. Stored as the fraction of all 64 rays
(record [1].w). Mode 2 = mode 1, plus inside when that fraction > 0.5 (class 6 = inside by the crossings
alone). Truncation at the trace distance can only drop an exit, so it cannot turn a free probe inside.
Per mesh, so one open mesh's unmatched exit is not summed with another mesh's faces. Known risk: open
meshes (Sponza's single-sided partitions) give free probes unmatched exits; the 0.5 threshold is the
guard, and it is what Sponza must show.

**Readings (the registered R3/P1/P2/P3 of 2026-09-21 unchanged, candidate = rule 2), plus:**
* R3' — the SHELL criterion replaced as the 2026-09-21 result already recorded it should be: behind
  probes within 100 cm of the shell -> invalid on 100 %. Reported beside the registered R3, which is
  expected to fail again on the shell criterion (the far behind probes see mostly the ground).
* X1 — mode 2 vs mode 1 on the reference: the probes whose validity differs are ONLY class 6, and none
  of them is a free probe by the truth.
* X2 — Sponza (QA and PROJECT, Camera C, one capture each): count of class-6 probes and their
  crossing fractions; each class-6 probe is inspected against its field distance and position. More
  than 1 % of the probes the mode-1 candidate calls valid inside the building (cells x 5..12, y 3..15,
  z 8..13) turning class 6 = the open-mesh risk is real, and mode 2 is not proposed.
Pass for mode 2 = R3 (with R3' for the shell), P1, P2, P3 in both configurations, X1, X2.

## Crossing count — result (2026-09-23, H-69)

Build 2026-09-23 11:03 (the classifier's back-face fraction is bit-identical to the 2026-09-21 build on
every probe of every reference capture). Reference: `run_probe_side_reference.py --candidate 2`, output
`.screenshots/flicker-elimination/63-probe-side-crossings/study/`; Sponza:
`scripts/run_probe_crossings_sponza.py`, output `.../63-probe-side-crossings/sponza/`.

**Reference: passes with R3' (the registered R3 fails only on the shell criterion, as predicted).**

| | QA | PROJECT |
|---|---|---|
| inside solid -> invalid | 100 % (was 100 %, 4 of them by relocation failure) | **100 %** (was 98.5 %) |
| free -> valid | 99.95 % (the 4 relocation failures of 2026-09-21, unchanged) | 100 % |
| class 6 (inside by the crossings alone) | 4, all inside by the truth (the former relocation failures) | 4, **exactly R3's four probes**, all inside |
| behind the shell within 100 cm -> invalid (R3') | 20/20 | 20/20 |
| registered shell criterion | 8 % (fail, as 2026-09-21) | 8 % (fail) |
| P1 NEAR_OUT lit face / Lref, unsupported | 1.038, 0 % | 1.038, 0 % |
| P2 CORE / NEAR_IN vs current | 1.000 / 1.000 | 1.046 / 1.046 (same premise failure as before) |
| P3, C | pass, pass | pass, pass |

X1 PASS: against the 2026-09-21 mode-1 captures, probe by probe, validity differs only on class 6
(PROJECT, 4 per camera) and nowhere on a free probe; in QA validity is identical and the 4 probes that
were invalid as "relocation failed" are now invalid as class 6.

**Sponza: X2 FAILS, decisively.** Camera C, one capture per configuration, rule 2:

| | QA | PROJECT |
|---|---|---|
| building probes (cells x 5..12, y 3..15, z 8..13) | 624 | 624 |
| class 3 (inside by the back-face fraction, i.e. mode 1 already) | 408 | 408 |
| mode-1-valid in the building that turn class 6 | **216 of 216** | **216 of 216** |
| class 6 overall | 227 | 227 |

The class-6 probes are in open space: PROJECT field distance median 64 cm, p90 121 cm, max 151 cm (QA's coarser, biased field: median 1.2, max 75), and in front
of their nearest face (median +85 cm). Their first hits are all front faces (back-face fraction 0) yet
84-100 % of their rays leave some mesh without having entered it. So on Sponza the per-mesh crossing
count is not a solid-membership test: its meshes are not closed solids with consistent orientation
(open partitions; the orientation question of H-67 is still unresolved). Mode 2 turns off every probe
mode 1 keeps inside the building, which would black out the cache there. **By the pre-registered rule
mode 2 is not proposed.** It stays behind the switch (`r.GI.RadianceCache.ProbeClassification` = 2),
default unchanged (0).

**Observation, not examined:** mode 1 already calls 408 of the 624 building probes inside, many of them
70-240 cm from any surface and 88 cm (median) BEHIND their nearest face. A probe that far from every
surface is unlikely to be inside a solid, which points at the same thing as the class-6 misfire: on
Sponza the baked face orientation is not a trustworthy side. This is H-67's conditional ("vertex
orientation is the true side on Sponza") now with evidence against it, and it limits mode 1 on Sponza
as much as mode 2. It has to be settled before either candidate means anything there.

**What 6.4 needs next:** a side test that does not depend on the meshes being closed or consistently
oriented — e.g. visibility-based (a probe is free when it can see the camera-reachable space / the
sky-connected region: flood fill over the GDF from a known free seed), or orientation repaired at bake
time. Either is new work; nothing here changes defaults.

## Reachability from the camera — pre-registration (2026-09-24, mode 3)

**Why.** H-69 showed that on Sponza no test built on mesh faces can say which side is air: the
meshes are neither closed nor consistently oriented. Mode 3 uses only the distance field and the camera.

**Candidate** (`r.GI.RadianceCache.ProbeClassification` = 3; `probe_reach_cs.hlsl`). A 128^3 air grid
over the probe grid (voxel = spacing / 8 = 33.3 cm at GridExtent 2000): air where the biased field at
the voxel centre >= half a voxel. Seeds = air voxels within 2.5 voxels of the camera; 6-connected flood
fill (`r.GI.RadianceCache.ReachPasses` dispatches, default 64, each up to 16 steps in an 8^3 tile).
Per probe: climb the field gradient until the clearance is half a voxel + half a voxel diagonal (45.5
cm), at most half a spacing; valid when the voxel reached is flooded. A valid probe inside the 5 cm band
is traced from the end of its climb and carries a guard plane. Without a seed the climb alone decides.
Class codes 7 unreachable, 8 valid, 9 valid and traced from the climb.

**The meaning changes, disclosed:** valid = in air connected to the camera, not "free". A sealed room
the camera is not in is invalid on purpose (no surface in it is visible), so for mode 3 the truth is
per camera: SOLID room cameras reach their room's interior; EXT reaches every free probe outside the
rooms; both SHELL cameras are inside the shell and reach its interior only.

**Disclosure.** Before this text one smoke (PROJECT, SOLID/EXT, SOLID/NEAR_OUT, SHELL/INT_E) ran:
shader compiles; EXT inside 256/256 invalid, free 3826/3840 valid (14 invalid, not yet attributed);
NEAR_OUT 4 free valid; SHELL 4 interior valid, 3952 behind invalid. The fill re-ran 3-6 times, all
during loading (mesh set changes).

**Readings (per camera, both configurations):**
* R3m inside -> invalid 100 %; free in the camera's component -> valid >= 99 %; free outside it ->
  invalid >= 99 %. Mismatches are listed with position, field and climb length.
* P1, P2, P3, C as registered on 2026-09-21 with candidate = 3.
* C3 convergence: `ReachPasses` 64 vs 256 give identical classes (SOLID/EXT and Sponza PROJECT).
* Sponza (Camera C): X2m among building probes with PROJECT field >= 50 cm, >= 95 % valid;
  S1 (the H-63 setup, QA, gather off) unsupported <= 5 % per ROI, as registered for the Sponza study;
  S3 (motion, long sweep) p99 |frame-to-frame change| per ROI <= 1.1 x the current rule's and no
  larger step at the logged recentres; plus the frame cost of the fill at a recentre, reported.
Pass = R3m, P1, P2, P3, C3 in both configurations, then X2m, S1, S3.

### Mode 3, first full run (2026-09-24, `64-probe-reach/study`) — a defect of the candidate, fixed, re-run

R3m passed for every camera in both configurations (QA EXT 99.3 %: 26 probes 133 cm under the ground
slab, field 28 cm, climb stopped; PROJECT 100 %); P2, P3, C passed (sealed rooms and the shell interior
0, against up to 0.54 x Lref under the current rule). **P1 FAILED: NEAR_OUT's lit face 100 %
unsupported**, although its probes (10 cm outside the face) are correctly valid. Cause: the guard
plane was put at "field at the probe" behind the probe; the biased field reads <= 0 there, so the plane
sat on the probe and rejected the probe's own wall. Fix (one change, `probe_reach_cs.hlsl`): the plane
goes to the minimum of the field along -climb direction within 80 cm — the surface, whatever the bias.
Re-run with the same criteria into `64-probe-reach/study-v2`; the first run's report is kept.

### Mode 3 — reference result after the fix (2026-09-24, `64-probe-reach/study-v2`): PASS

| | QA | PROJECT |
|---|---|---|
| R3m, per camera (6 cameras) | all pass; EXT 99.3 % of its 3826 reachable probes valid (26 below the ground slab: field 28 cm, climb stopped) | all 100 %, 0 mismatches |
| P1 NEAR_OUT lit face / Lref, unsupported | 0.946, 0 % | 1.008, 0 % |
| P2 CORE / NEAR_IN | 1.000 / 1.000 | 1.046 / 1.046 (premise failure as before) |
| P3 sealed rooms, shell interior (x Lref) | **0 everywhere** (current rule: up to 0.28) | **0 everywhere** (current: up to 0.54) |
| C | pass | pass |

### Mode 3 on Sponza's probes (2026-09-24, `64-probe-reach/sponza-probes`, `sponza-dump`, `sponza-dxr`)

* **C3 PASS**: 64 and 256 flood dispatches give identical classes on every probe.
* **X2m FAILS as registered**: of 367 building probes with field >= 50 cm, 31.9 % valid (mode 0: 100 %).
  Building: mode 0 keeps 493 of 624, mode 3 174 (450 class 7). Whole grid: 3461 valid under both,
  329 mode 0 only, 145 mode 3 only.
* **Why — the criterion's premise was wrong, not the fill.** New diagnostic
  `r.GI.RadianceCache.ReachDump` logs the air grid (`slices.jpg`): the unreached air sits between the
  building and a closed outer outline present at every height, plus the attic volumes. The DXR writer
  (real triangles, 32 rays per probe) gives **sky fraction exactly 0 on all 304 probes in unreached
  air** (every ray hits geometry), against median 0.84 and 97 % with sky on reached probes. Those
  volumes are enclosed by Sponza's own geometry; no surface in them is visible from the camera, so
  invalidating them is what mode 3 is meant to do. "Field >= 50 cm" meant "far from a surface", not
  "reachable". The verdict stays FAIL as registered; what decides the candidate is what the camera
  sees: S1/S2 (supported pixels) and S3 (motion), run next.

### Mode 3 on Sponza — S1, S2, S3 (2026-09-24, `64-probe-reach/sponza-study`)

* **S3 (motion, long sweep, project configuration): PASS.** Per ROI, p99 |frame-to-frame change| and
  the largest step at the three recentres (frames 128/172/216 under both rules) are equal to the
  current rule's within 1.02x; mode 3 minus mode 0 stays within 2e-4 and jumps by at most 1.7e-4
  frame to frame. Frame cost (manifests, frames 200.., the 216 recentre included): mean 15.4 ms vs
  15.9 ms, max 31.1 vs 34.0; RadianceCache_Trace 0.46 vs 0.75 ms (fewer probes traced). No cost
  of the fill is visible at this resolution of measurement.
* **S1 (the H-63 setup: QA configuration, gather off): FAIL** (criterion <= 5 % unsupported per ROI).
  shadow_wall 13.65 % (unchanged), lit_wall 65.4 -> **55.5 %** (mean 1e-5 -> 2.4e-4), centre 1.5 -> 0.5 %.
  Mode 1 reached 31.3 % on lit_wall. In the coarse QA field the climb (target 45.5 cm, at most half a
  spacing) fails for many wall-side probes, and a wall whose surrounding cell corners are all inside the
  wall or behind it keeps no support.
* **S2 (project configuration, gather on): support moves, the image does not.** shadow_wall 0 -> 9.3 %
  unsupported (the same 9.3 % as mode 1), lit_wall and centre 0 %; ROI means identical (2.5e-4,
  1.4e-4, 2.2e-4). The probes that supported shadow_wall under mode 0 (2252, 2268, 2508, 2524, x = 933)
  lie in the enclosed void behind the wall (DXR sky fraction 0); the visible-side corner (2251, field
  -14.8 cm) is buried in the wall (27 of 27 neighbouring voxels solid) and invalid under both rules. So
  mode 0's support there was a leak through the wall carrying ~0 radiance. Full frame
  (`S2_cameraC_project.jpg`): visually identical, faint brighter specks at column bases and arch edges.

**Reading.** Mode 3 is the first candidate that is right about what is air on both the reference
(R3m 100 % PROJECT, P1-P3 pass, sealed volumes exactly 0) and Sponza (unreached volumes confirmed
enclosed by DXR), costs nothing measurable, and adds no motion step. What it does not do is give a
wall face a probe when every corner of its cell is inside the wall or behind it — that needs probe
RELOCATION (move an invalid probe to reached air within half a spacing and let the consumer use the
moved position, as DDGI does), a consumer-side change. Not enabled by default.

## Probe relocation into reached air — pre-registration (2026-09-25, mode 3 + `r.GI.RadianceCache.ReachRelocate` = 1)

**Why.** H-70: mode 3 is right about which air is reachable, but a wall face whose cell corners are all
inside the wall or behind it keeps no probe (S1 lit_wall 55 % unsupported).

**Candidate.** In mode 3's decision pass, a probe that ends invalid moves to the nearest reached air
voxel centre within half a spacing (class 10). It is traced there; its guard record switches to a
second encoding (w < 0: xyz = offset from the grid position, plane normal = offset direction, plane at
the field minimum on the segment it crossed). All three readers (deferred lighting, reflection hits,
multi-bounce history) weight and guard it at the moved position; trilinear weights stay on the grid
(Majercik 2021 6.2). Default off; mode 3 without it is unchanged (w < 0 never written).
The log now carries each probe's trace-origin offset (`originOffset=`).

**Readings.**
* R3r (reference, per camera): every VALID probe (classes 8, 9, 10) has its trace origin in the air
  component the camera is in (truth) — >= 99 %, mismatches listed. Relocated counts reported.
* P1, P2, P3, C as registered (candidate = mode 3 + relocation). P3 is the leak test: sealed rooms and
  the shell interior must not get brighter than under the current rule.
* Sponza S1 (<= 5 % unsupported per ROI), S3 (motion, as registered); S2 reported with the image.
Pass = R3r, P1, P2, P3, C in both configurations, then S1, S3.

### Relocation result, and a second defect found on Sponza (2026-09-25)

Reference (`65-probe-relocate/study`): R3r 100 % (every valid probe traced from air the camera
reaches; PROJECT relocates 102 probes for EXT, 100 of them out of solids); P1 0.975 / 1.004; P3 all
sealed labels 0; C pass; **P2 FAILS in PROJECT: 1.0515 / 1.0492** against the 5 % band (mode 3 without
relocation: 1.046, the premise failure already recorded; relocation adds ~0.5 %). QA passes everything.
Sponza (`65-probe-relocate/sponza-study`): S1 lit_wall 65.4 -> **32.4 %** unsupported (mode 3 alone
55.5 %), shadow_wall 13.7 -> 11.8 %, centre 0.5 % — FAIL (<= 5 %); S2 shadow_wall still 9.3 %; S3 PASS
(steps unchanged, lit_floor mean +2.2 %, frame 15.3 vs 15.8 ms).

**Why S2 did not move:** those pixels (a -y face at y 678) front a pocket of air that the air grid calls
unreached; the camera's view ray to them runs through ~145 cm the grid calls solid (x 567-668) — the
biased two-sided field (bias ~19 cm here + threshold 16.7 cm) seals an opening that is really open.
Measured over the frame (8 rectangles, 1.27 M visible pixels): of the air voxels 1.5 / 2.5 voxels in
front of visible points, 99.22 / 99.37 % were reached (6251 / 6610 pixels in front of unreached air).
**Fix, behind `r.GI.RadianceCache.ReachVisibleSeeds`:** air voxels 1.5-4.5 voxels back along the view
ray from every 4th visible pixel (G-buffer world position) seed the flood; solid voxels are never carved
(an air voxel's centre is >= threshold + bias from every surface, so it is on the ray's side of any
wall). After: 100.00 / 99.98 % (22 / 257 pixels). A first build used `linear` as a parameter name (an
HLSL modifier) and failed to compile at runtime; the harness refused to measure, as designed.
Next: reference and Sponza with relocation + visible seeds, same criteria.

### Relocation + visible seeds — result (2026-09-25, `66-probe-seeds/study`, `sponza-study-2`)

| | mode 0 | mode 3 | + relocation | + relocation + visible seeds |
|---|---|---|---|---|
| reference R3r / P1 (PROJECT) | - | (R3m 100 %) / 1.008 | 100 % / 1.004 | 100 % / 1.004 |
| reference P2 CORE (PROJECT, band 5 %) | - | 1.046 | **1.0515 FAIL** | **1.0515 FAIL** |
| reference P3 sealed volumes | up to 0.54 Lref | 0 | 0 | 0 |
| Sponza S1 lit_wall unsupported (QA, gather off) | 65.4 % | 55.5 % | 32.4 % | **28.9 % FAIL** |
| Sponza S1 shadow_wall / centre | 13.7 / 1.5 % | 13.7 / 0.5 % | 11.8 / 0.5 % | 11.8 / 0.5 % |
| Sponza S2 shadow_wall unsupported (PROJECT, gather on) | 0 % (leak) | 9.3 % | 9.3 % | **0.11 %** |
| Sponza S3 motion | - | pass | pass | pass (steps 1.00x, frame 16.2 vs 16.5 ms) |

QA passes everything on the reference; PROJECT fails only P2 (the premise failure plus ~0.5 % from
relocated ground probes). S2's image-level ROI means are unchanged under every variant.

**Intermittent hang, not explained.** The first Sponza run with relocation + visible seeds hung at the
start of the render loop of its first S1 capture (after preload and 35 successful fills; no error in the
log; killed by the harness at 1800 s). Not reproduced in three 60-frame runs (each flag alone and both)
nor in the full re-run (10 captures). Candidate causes not examined: the G-buffer read declared on the
cache pass, the diagnostic readback's WaitIdle. Must be found before anything here is enabled.

**Where 6.4 stands.** In the project configuration the candidate (mode 3 + relocation + visible seeds)
classifies correctly on the reference, removes every leak into sealed volumes, leaves Sponza's image
unchanged where the current rule was right, gives the arcade wall back its support, and adds no motion
step or cost. It fails two registered criteria: P2 by 0.15 points, and S1, which is defined on the coarse
QA field (GDF 64 / mesh SDF 32) where the band is fat enough that 29 % of the lit wall still has no
probe. Not enabled by default; the hang is open.
