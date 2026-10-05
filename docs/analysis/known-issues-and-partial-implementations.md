<!-- doc-meta -->
title: Known Issues & Partial Implementations
date: 2026-04-07
status: living-document
<!-- end-doc-meta -->

# Known Issues & Partial Implementations

Living registry of systems/features that are **partially implemented**,
**disabled by default**, or **known to have bugs**. Everything listed here
has either been worked on and paused, or was added speculatively and not
integrated. The goal is to prevent rediscovery cost — before touching any
of these, read this doc and the linked code locations.

**Ordering**: user-visible impact first, then size of remaining work.
**Status legend**:
- `OFF` — implemented code path exists, disabled via CVar (documented below)
- `PARTIAL` — builds and runs but known incorrect / incomplete
- `STUB` — scaffolding only, never integrated / tested
- `BUG` — enabled but produces wrong output

---

## Anti-Aliasing / Temporal / Upscalers

### `r.TAA` — `OFF` (BUG)
**File**: [runtime/engine/renderer/frame/frame_setup.cpp:42](../../runtime/engine/renderer/frame/frame_setup.cpp)
**Default**: `false`

TAA causes visible mesh trembling / vertex wobble. Multiple tuning sessions
on jitter sequences, clamping, and history rejection did NOT resolve it.
Suspected root cause: motion vector generation or history reprojection bug
introduced by an unrelated change. Needs a ground-up audit.

**Workaround**: leave disabled. FXAA is on by default for edge smoothing.

**Memory reference**: MEMORY.md documents the bug as
`project_taa_jitter_trembling_bug.md` (2026-04-01), attributed to commit
`24c4d801` (decals).

---

### `r.TSR` — `PARTIAL`
**File**: [runtime/engine/renderer/passes/postprocess/tsr_upscaler.cpp](../../runtime/engine/renderer/passes/postprocess/tsr_upscaler.cpp)

Temporal Super Resolution (UE5-style) is implemented with 3 passes
(dilate-velocity, reject-shading, update-history), but known to produce
artifacts. Not recommended over DLSS/FSR2 for now. Needs a tuning pass
once TAA is fixed (TSR and TAA share the temporal infrastructure).

---

### `r.DLSS.Quality` — `PARTIAL`
**Files**: [dlss_upscaler.cpp](../../runtime/engine/renderer/passes/postprocess/dlss_upscaler.cpp),
`extension/native` (NGX wrapper)
**Status**: upscale path works; Ray Reconstruction + DLSS-G (frame generation)
are NOT integrated.

Base DLSS 2/3 upscale via NGX ("NVSDK_NGX_D3D12_CreateFeature") is functional
after the 2026-03-25 fix (raw vtable access + exposure default 10.0). Remaining gaps:
- **Ray Reconstruction (DLSS 3.5)** not integrated
- **DLSS-G / DLSS Multi Frame Gen** not integrated
- **Motion vector coverage**: some passes don't feed motion vectors, so
  DLSS occasionally ghost-smears — same root as the TAA bug
- **No runtime fallback**: if NGX fails, the pass silently returns low-res

---

### `r.FSR2` — `OFF`
**File**: [fsr2_upscaler.cpp:76](../../runtime/engine/renderer/passes/postprocess/fsr2_upscaler.cpp)

FSR 2.x temporal upscaler is scaffolded but never enabled by default. Build
path compiles, integration untested at runtime.

### `r.FSR` — `OFF`
**File**: [fsr_pass.cpp:24](../../runtime/engine/renderer/passes/postprocess/fsr_pass.cpp)

FSR 1.0 spatial upscaler (EASU + RCAS). Works but not wired into any
default pipeline. Useful for old hardware; low priority.

---

### `r.SMAA` — `OFF`
**File**: [smaa_pass.cpp:33](../../runtime/engine/renderer/passes/postprocess/smaa_pass.cpp)

SMAA (edge + blend + resolve) shaders exist and compile, but the pass is
off by default. Needs validation vs. FXAA (which is the current AA path)
and a quality comparison.

---

### Static TAA residual — `MODELLED (2026-09-10), STILL OPEN`
The static flicker's *amplitude* is described by the TAA accumulation's steady-state ripple: the
accumulation is a first-order IIR filter (`r.TAA.CurrentFrameWeight` = 0.04) whose input is
periodic in the jitter cycle, and such a filter settles into a ripple rather than converging.
Predicted from the measured input ripple and the blend weight, with no fitted parameters, it lands
within 1.5x at jitter periods 4, 8 and 16 and reproduces the ordering; with the sub-pixel offset
removed the residual falls four orders of magnitude. Ledger H-27, evidence in
`.screenshots/flicker-elimination/15-isolate-clamp-bounds/`.

This is an **approximate amplitude model, not a verdict on the image.** The shader's accumulator is
not a constant-coefficient IIR (its effective weight ranges 0.039–0.300), the prediction applies the
fundamental's gain to the whole variance, and neither phase nor per-pixel time series were
reproduced. It bounds the remedies without declaring the visible behaviour acceptable — that
judgement has not been made.

Consequences for anyone tuning TAA here:

* the residual is `input_ripple x |H|`, so reducing it means making the reconstruction less
  jitter-dependent (measured, caps at −35 %, and it bleeds light into black pixels), lowering the
  blend weight (lag, which the motion gate rejects), or shortening the jitter cycle (less
  anti-aliasing);
* the clamp *bounds* are rejected in steady state on a static camera (H-25b): freezing them moves
  the oscillation by −3.4 %, below this ROI's decision threshold, while the error against the
  correctly resampled image grows 14.3 %. Averaging the bounds over a jitter cycle, or filtering
  them slowly, are different interventions and were not tested;
* the history fetch is **not** eliminated (H-26): its immediate perturbation is small, but no run
  substituted an exact history read while accumulating, so an effect that builds up over frames
  remains possible;
* `r.TAA.JitterScale=0` is a *control*, not a fix: it removes the residual by removing the
  anti-aliasing.

The shadowed walls and the behaviour in motion are outside all of this and remain open.

### TAA reconstruction HDR weighting - `MEASURED, NOT CHANGED (2026-09-09)`
The plus filter in `taa_resolve_cs.hlsl` weights each tap by `1 / (Y * exposure + 4)`
(Karis 2014). It is a demonstrated source of part of the static flicker on bright features
(ledger H-11, H-12): it makes the filtered colour depend on where the jitter put the samples,
and it loses energy on thin bright features.

`r.TAA.ReconstructionHdrWeightExponent` (default 1.0 = production, 0 = no weighting) was added
to study it and is **opt-in**: it failed four of the seven preregistered acceptance gates, so
the default was not changed. Measured at exponent 0, pose W window, 3 repeats:

* static residual -35.4 % (the gate asked for -50 %; the whole exponent range was swept
  and this is the ceiling);
* 3x closer to the correctly resampled reference, with the bright side's energy deficit nearly
  eliminated - the gain is real, and it is not blur;
* but recovery after a camera cut goes 16 -> 60 frames, and a dark
  ROI containing a small bright feature (pose A `rear_door`) gets
  +120 % residual and
  +79 % energy;
* pixels whose correct value is ~0 receive about 6x more light.

The weighting is a variance/bleed trade, not a defect to delete. The next experiment is a
reversible-tonemap reconstruction (ledger H-24). `r.TAA.ClampMode=3` (AABB only) is now rejected
as an alternative as well: on its own it moves recovery after a cut from 16 to
48 frames.

### Stale unit tests - `PRE-EXISTING, DIAGNOSED (2026-09-09)`
Two tests in `runtime/tests/` fail against committed production values and are unrelated to the
flicker work. Diagnosed rather than filtered out:

* `BuildPerFrameCB_StructSize.PerFrameCB_Is768Bytes` expects `sizeof(PerFrameCB) == 768`. Commit
  `29c83ba5` grew the struct to 1024 bytes (4 x 256) and
  `runtime/engine/renderer/core/params/constant_buffers.h` carries its own
  `static_assert(sizeof(PerFrameCB) == 1024)`, with fields in use out to offset 1008. The
  invariant that matters - D3D12's 256-byte constant-buffer alignment - is already enforced
  there; the test hardcodes the old size. Suggested fix: assert `sizeof(PerFrameCB) % 256 == 0`.
* `PerFrameCB_IBL.PrefilteredMipLevelsDefaultIsFive` expects 5. Commit `6985ffac` ("IBL rough
  metal excessive reflections") deliberately raised the prefiltered chain to 8 in
  `IBLBaker::kPrefilteredMipLevels` and in all three CB structs, which agree with each other.
  Suggested fix: expect `IBLBaker::kPrefilteredMipLevels` instead of a literal.

Neither test was changed here: they belong to the IBL and constant-buffer areas, not to this
programme.

## Global Illumination

### `r.SSGI` — `OFF` (BUG)
**File**: [ssgi_renderer.cpp:22](../../runtime/engine/renderer/gi/ssgi_renderer.cpp)
**Shaders**: [shaders/runtime/ssgi/*.hlsl](../../runtime/shaders/runtime/ssgi/)

SSGI was scaffolded (trace, spatial denoise, temporal accum, upsample) but
never functionally validated. The `ssgi_trace_cs.hlsl` has a hardcoded
`expectedLinearDepth = lerp(linearRayDepth, linearRayDepth + 5000.0 * t, t)`
with no physical basis — 5km depth assumption in a cm-unit engine. Result:
screen-space rays never register hits on architectural-scale scenes,
`ssgi_ao` writes constant 1.0, `ssgi_result` stays black. Thickness CVar
bumped to 50 cm in the recent session but that's a band-aid.

**Recommendation**: rewrite the march with proper view-space hit-distance
accumulation, or replace entirely with Brixelizer GI which works.

---

### `r.BrixelizerGI` — `OFF` → `ON (via scene CVars)`
**File**: [brixelizer_gi_pass.cpp:50](../../runtime/engine/renderer/passes/gi/brixelizer_gi_pass.cpp)

AMD FidelityFX Brixelizer GI (sparse SDF voxel GI + screen-space probes).
Implementation is **functional** — produces diffuse GI visible in Sponza.
Current gaps:
- **Specular GI output** produces values that saturate the pass-dump
  visualization (HDR magnitude > 10, Reinhard clamps to white). Unclear
  if the magnitudes are physically correct or if the shader is scaling
  them wrong.
- **Cascade tuning** uses the default base voxel size 2.0 cm × 4 cascades
  which works for Sponza but hasn't been profiled on larger scenes.
- **LOD-aware re-registration** uses a simple delete-all + re-create-all
  per-frame approach. Works for static scenes but is suboptimal for
  dynamic geometry.

---

### `r.RadianceCache` — `OFF` by default, `ON` for Sponza (project CVar) — `FUNCTIONAL` since 2026-09-08
**File**: [radiance_cache.cpp](../../runtime/engine/renderer/gi/radiance_cache.cpp)
**Shader**: [radiance_cache_trace_cs.hlsl](../../runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl)
**Docs**: [33-sdf-gi-final-gather.md](../engine/12-rendering/33-sdf-gi-final-gather.md), [sponza-gi-correction-2026-09-06.md](rendering/sponza-gi-correction-2026-09-06.md)

16³ L2 SH probe grid traced through the global distance field, with SDF
reflections (`r.GI.Reflections`, SSR lerped in before the BRDF) and the
world-space final gather (`r.GI.FinalGather`) layered on top. Until the
SponzaTest port (`phase0_sponza-parity-03/04`) every trace/bake/composite
shader under `runtime/shaders/runtime/gi/` was an **empty stub** (32 stub
files, 26 referenced) and `r.RadianceCache` could not actually be disabled —
the earlier "implemented but disabled because Brixelizer supersedes it"
description was wrong on both counts. Sponza enables it from
`sample/projects/sponza.uzproject`; `--cvar` beats the project file since
task 05. Brixelizer GI is the alternative path; they are not meant to run together.

Unresolved (carried over from SponzaTest's handoff and reproduced here on 2026-09-08):
- 1 of 32 world-reflection rays leaks through the enclosed-box fixture
  (`scripts/test_sdf_surface_origin.ps1`, max abs error 0.009328).
- An extreme grazing reflection exhausts the 256-step budget
  (`scripts/test_sdf_dynamic_trace.ps1`, 14/15).
- ~~Sponza LOD2/LOD3 decal triangles still carry the flipped normals~~ —
  **fixed 2026-09-08** (`phase0_sponza-followup-04`). Cause: `GenerateAutoLODs`
  (`runtime/engine/renderer/mesh/auto_lod.h`) ran `meshopt_simplify` with
  **positions only**, so a collapse could leave a triangle whose winding no
  longer matched its vertex normals. It now uses
  `meshopt_simplifyWithAttributes` with the normals (weight 0.5), which fixes
  every future import. Baked LODs in the shipped assets were repaired in place
  by `scripts/test_decal_lod_normals.ps1 -Repair` (winding swap only, vertex
  data untouched). The decal went from 23 inconsistent triangles at LOD2 and 26
  at LOD3 to zero. Across the 145 Sponza meshes the audit classes are 56 clean,
  43 simplifier artifacts (repaired) and 46 whose **LOD0** is itself
  inconsistent — authored or smoothed normals that legitimately disagree with
  some faces, left alone. One of those, `arch_stones_01_stone_trims_01_0`, is
  100 % inconsistent at LOD0 (a whole-mesh winding inversion) and still needs
  the treatment the decal got.
- No Surface Cache and no complete emissive transport (only the VFX → DDGI
  emissive feed); no Lumen parity is claimed.
- Falcor PSNR ≥ 35 dB parity unverified (no Falcor install on either machine).
- `--diag` reads `gi.gdf_cascade0` / `gi.gdf_raw_cascade0` as zero in **both**
  repositories, even after the 3D readback `D3D12_BOX` fix.
- ~~Stone materials read as metallic~~ — **fixed 2026-09-08**
  (`phase0_sponza-followup-05`). The graphs wire `Metallic <- <tex>.B`
  correctly (glTF metallicRoughness), but Sponza's maps carry 3-25 % in that
  channel on dielectrics, and four materials with no Metallic input at all
  (`glass`, `lamp_glass_01`, `light_bulb`, `dirt_decal`) hit
  `gbuffer_ps.hlsl:188`'s `metallic = metallicMultiplier` and rendered fully
  metallic at the neutral 1.0. 23 materials now carry
  `metallicMultiplier: 0.0`; the metallic share above 0.25 fell from 33.2 % to
  0.83 % at Camera A and 41.9 % to 3.9 % at Camera C (the remainder being
  `metal_door`, which is genuinely metallic at 0.816). `E:\SponzaTest` carries
  the same unfixed values. Guard: `scripts/audit_sponza_metallic.ps1`.
- The metallicRoughness textures themselves still carry the dirty blue
  channel; the multiplier neutralises them at material level. A clean re-export
  is the asset-side fix.

---

### DDGI (`DDGISystem`) — `PARTIAL` (dispatched; DXR-gated)
**File**: [ddgi_system.cpp](../../runtime/engine/renderer/gi/ddgi_system.cpp)
**Shader**: [ddgi_probe_update_cs.hlsl](../../runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl)

Octahedral irradiance + depth atlases, probe grid layout, bindless slots.
`ddgi_probe_update_cs.hlsl` **is** dispatched from `frame_gi_passes.cpp`
(`RadianceCache_Trace` pass) once `r.RayTracing.GI=1` and a TLAS is ready;
deferred lighting samples the atlases after `HasCompletedFirstUpdate()`.
It shares the 208-byte `RadianceCacheTraceConstants` (176 + VFX emissive
tail + grid-recentre row) with the SDF trace. SponzaTest could not validate the DXR path
(`DDGI_ProbeUpdate` pipeline failed with 0x80070057 on a Tier 1.0 GPU); the
2026-09-08 run on an RTX 5080 (Tier 1.2) is recorded in
`.rulebook/tasks/phase0_sponza-parity-06-qa-harness-validation/notes.md`.

That first validated run exposed four probe-energy defects, all fixed on
2026-09-08 (`phase0_sponza-followup-01-dxr-ddgi-energy-parity`):

1. The probe hit radiance was missing the Lambert `1/pi`, so every surface
   bounce was pi times too bright.
2. There was no shadow ray, so each hit was lit as if the sun were unoccluded.
3. Chebyshev visibility was inert: the RG16F depth atlas stored a squared
   distance in centimetres, which saturates FP16 (65504) at Sponza's scale, so
   the variance collapsed and every probe reported full visibility. Both
   moments are now stored normalised by `maxTraceDistance`.
4. `DDGISystem::DispatchUpdate` built its constants with a placeholder
   environment (sun pointing straight up at intensity 1, sky 0.6/0.7/0.9,
   no distance field), unrelated to the loaded scene. It now reads the same
   `PerFrameCB` fields and `GlobalDistanceField` clipmaps the radiance cache
   reads, and takes the hit normal and albedo from the field
   (`runtime/shaders/runtime/gi/gdf_surface.hlsli`) because inline `RayQuery`
   reports no hit attributes.

Result: Camera A 52.0/255 for both paths (was 77.1 for DXR), Camera C 91.8 vs
the SDF path's 90.1. Still open — with the final gather off the DXR path
remains brighter: the per-probe GPU comparison (`scripts/test_gi_energy_gpu.ps1`, linear mean ray radiance of both writers on the same grid) reads DDGI/SDF = 0.5748 (parity 1 ± 0.10 NOT MET); the earlier display-referred PNG proxy gave 1.21. Found on the way: with `r.RayTracing` (master) off and `r.RayTracing.GI` on, the writer ran against an empty TLAS and filled every probe with sky — the dispatch is now gated on `IsGIEnabled()` and a populated TLAS; the SDF path's 96-step sphere
trace drops energy on exhausted rays and the DDGI consumer's Chebyshev
rejection is weaker than the radiance cache consumer's, and the two have not
been separated. DDGI also uses a flat sky colour where the SDF writer samples
the sky cubemap, and hardcodes 32 rays per probe.

Regression guard: `scripts/test_ddgi_energy.ps1` (16 source-level checks).

**Indirect-path noise (2026-09-08, `phase0_sponza-followup-06`).** With `r.TAA` off
(the engine default), the stochastic passes — SDF reflections (4 GGX rays/pixel),
world-space final gather, GTAO — show per-pixel noise; on a dark wall beside a
window it reads as white specks. Root cause of the specks: reflection rays that
escape through the opening return sky radiance, 0-or-1 per pixel, with no filter
on that path. Three estimator defects were fixed along the way (plain mean →
Karis weight; below-horizon rays renormalised away → weight kept; radiance-cache
probe selection by the normal-mapped normal → geometric normal), and the Sponza
project now sets `r.TAA: 1` (Camera A isolated bright pixels 944 → 244). A second report after enabling TAA — the whole scene flashing near-white
while flying — was the radiance cache readiness gate dropping every indirect
path for one frame each time the camera crossed a probe cell (~267 cm);
`HasProducedResult()` is no longer gated on the recentre flag (frames at a
crossing 18.9 → 236.3 → 15.4 before, 18.9 → 15.6 → 12.8 after). A third
report — a blink at one point while rising (z ≈ 267 cm, the probe spacing) —
was the recentre itself: lighting was handed the *current* grid origin with the
buffer completed *last* frame (one frame indexed one cell off → flash), and the
retrace ran with first-write semantics (no history, no multi-bounce feedback →
a plateau decaying over ~40 frames). Vertical sweep at the user's pose, frames
131→132→133: 83.4 → 95.3 → 88.0 before, 83.4 → 83.9 → 84.1 after
(`scripts/sponza-vertical-camera.*`). Lighting now indexes the published buffer
with `GetCompletedGridOrigin()`, and the recentre scrolls the probe history
through an integer grid shift (`historyShift*` row, 208-byte trace constants);
only the first write and a scene-revision change reset it. Open: a
spatial/temporal filter for the SDF reflection path; the `shadows`,
`lighting`, `lightingonly` and `ao` debug views render nothing usable. Guards:
`scripts/test_sdf_reflection_firefly.ps1`, `scripts/test_rc_recentre_scroll.ps1`.

---

### TAA jitter applied to the view-projection as if it were the projection — `FIXED` 2026-09-08
**File**: [frame_setup.cpp](../../runtime/engine/renderer/frame/frame_setup.cpp), [taa_jitter.h](../../runtime/engine/renderer/frame/taa_jitter.h)
**Tests**: gtest `TaaJitter.*`, `scripts/test_taa_bloom_input.ps1`; QA scenes `scripts/sponza-closeup-camera.*`, `scripts/sponza-closeup-approach.*`

The Halton sub-pixel jitter was added to `m[2][0]/[2][1]` of the combined
view-projection. That row multiplies view-space z only in a pure projection
matrix; in the view-projection it multiplies the vertex's world z, so the NDC
shift was `jitter · world_z / w` — ±7 px on a wall 40 cm from the camera at
z ≈ 576, ~0.2 px on the far atrium wall. The TAA resolve un-jitters by the
nominal (jx, jy) pixels and its history clamp then follows the displaced frame:
near geometry shook while the camera stood still ("a mesh balança quando a
câmera chega perto"). Static-camera pairs: 0.25–1.1 % of pixels changing by
> 40/255 with TAA on, 0.00 % off; a cornice-edge pixel 180 → 188 → 31 → 179.
Eliminated first: GTAO, final gather, SDF reflections, anti-flicker, variance
clip, bloom, CAS, motion blur. Fix: `ApplyTaaJitter` folds the NDC translation
through the matrix's w column so every vertex moves by exactly (jx, jy) pixels.
After: 0.19 / 0.00 %, 0.22 / 0.00 %, 0.19 / 0.00 % (before 1.06 / 0.25 %, 2.45 / 1.11 %, 1.54 / 0.70 %; TAA off 0.94–0.98 / 0.00 %) — now below the TAA-off noise floor, the cornice-edge scanline identical across frames, changed-pixel grid (threshold 4) 92 897 → ~800. SponzaTest has the same defect (its `r.TAA` is off by
default). Also fixed on the way: the TAA-path Bloom read the jittered scene
colour instead of the TAA output (not the cause — `r.Bloom=0` control identical).

---

### Static-camera flicker on high-contrast HDR edges under TAA (Sponza window bars) — `OPEN`, attributed 2026-09-09
**File**: [taa_resolve_cs.hlsl](../../runtime/shaders/runtime/postprocess/taa_resolve_cs.hlsl), [taa_upscaler.cpp](../../runtime/engine/renderer/passes/postprocess/taa_upscaler.cpp)
**Tests**: `scripts/test_region_dump.ps1`; QA scenes `scripts/sponza-flicker-w-seq.*`; packages `.screenshots/flicker-{sequence,jitter,region}/`

With the camera still, pixels on the bar/pane edges of the barred window oscillate with a
period equal to `r.TAA.JitterSequenceLength`. Per-pixel records from the lighting pass and
the resolve (`--region-dump`) show the raw sample is deterministic in the jitter index (no
stochastic component; the SDF reflection term is 0.0006 Y there and switching reflections
off leaves the linear oscillation unchanged). The residual is produced by the resolve itself
on an 8 Y : 0 Y edge: the plus filter un-jitters by weights only, so pane light enters the
adjacent bar pixel by a jitter-dependent amount (filtered 0 → 1.5 Y while the raw stays 0),
and the variance clamp then snaps the history (17 % of the flicker pixels' frames, 41 % of
their output change). On screen the visible part is the dark bars next to the panes: a
0.005–0.10 Y change rendered at auto-exposure ≈ 67 (`r.AutoExposure.MinBrightness` 0.001
with bars at 0.000 Y) is 50–100 display levels, on top of a ~94 L bloom floor. Anything that
moves the exposure changes the *count* of threshold crossings without touching the
oscillation (reflections off: 37 → 0.6 px > 40 per frame; bloom off: 39 → 1547).
`UpscalerInput::exposure` was never consumed by the resolve's HDR weights; the opt-in
`r.TAA.PreExposeWeights` halves the bar-side count but destabilises the pane side (output std
p99 0.18 → 2.49 Y), so it stays off. Candidate fixes not yet tried: partial pre-exposure,
resampling (not re-weighting) the un-jittered neighbourhood, or an exposure floor for the
interior. Keep `r.TAA.JitterSequenceLength=8`; length 1 (a constant Halton23(1) offset) is a
control, not a fix.

**Measurement status (2026-09-09, followup-09).** The capture and the metrics behind those numbers
were re-validated before any further conclusion is drawn. The instrumentation is now
`--region-dump` version 2: several rectangles per run, GPU frame stamps on every record block, a
readback that runs independently of TAA, a shutdown flush and a run manifest
(`--run-manifest`: executable hash, effective CVars, exposure, frame timing, integrity counters).
`scripts/test_region_capture.py` (23 behavioural checks) and `scripts/test_flicker_metrics.py`
(22 synthetic checks) guard it. What the validation established, and what it corrected:

* the records are linear scene radiance — a 16x exposure change leaves them bit-identical, and the
  renderer is deterministic at a given frame index (two independent runs agree exactly);
* the resolve's own blend equation, recomputed from the recorded fields, reproduces the recorded
  output to 9.2e-07 relative error, so the field meanings are verified rather than assumed;
* the linear-domain oscillation and the display-domain threshold crossings are **disjoint pixel
  sets** (bright pane side vs dark bar side of the same edge) — the two descriptions in the earlier
  package are consistent, but neither one can stand for the other;
* **auto-exposure adapts with the frame time**, so any change of cost moves the displayed image by
  about one code value; every display-referred comparison must fix the exposure;
* **bloom mixes neighbouring pixels**, so a display value is not a per-pixel function of the
  recorded radiance unless bloom is off;
* `--screenshot-frames` file `f_N` held render N+1 until 2026-09-09; older packages must be aligned
  by one frame.

Open hypotheses, their scope and the next discriminating experiment are tracked in
[flicker-hypothesis-ledger.md](flicker-hypothesis-ledger.md); evidence packages live under
`.screenshots/flicker-elimination/`.

**Mechanism, isolated 2026-09-09 (followup-10).** The window has two independent defects:

1. **An energy bias.** The converged image holds 91.9 % of the radiance of the correctly resampled
   input over the whole window rectangle; on an analytic 1.5-px bright bar the loss is 48.4 %. The
   cause is the HDR weighting (`HdrWeightY`) applied *inside the reconstruction filter*: dropping it
   (`r.TAA.ReconstructionMode=1`) takes the rectangle to 98.0 % and the bar loss to 20.6 %. The loss
   scales with contrast — the same bar at 1.0 radiance loses 16.6 % instead of 48.4 %.
2. **A temporal residual** driven by the jitter. A constant offset (`r.TAA.JitterScale=0`) removes it
   completely (0.0486 → 0.00001 radiance on the edge pixels) and leaves the energy bias untouched.
   Per stage: the raw sample is perfectly stable on the pane (0.00000) and swings 3.5 radiance at the
   bars; the jitter-weighted plus filter spreads that onto the pane (0.248); the accumulation
   attenuates everything about 25x. The variance clamp adds about 35 % of what is left — the
   neighbourhood AABB alone behaves like no clamp.

Removing the clamp is not an option: it gives the best static number and never clears invalid
history (peak error after a camera cut 10x the baseline, still 46x after 80 frames). This TAA
rejects history only when the reprojection leaves the screen, so the clamp is its only disocclusion
defence. Diagnostic CVars `r.TAA.JitterScale`, `r.TAA.ReconstructionMode`, `r.TAA.ClampMode` and
`r.TAA.BlendMode` all default to the production behaviour; the offline model that reproduces the
resolve to 2e-7 is `scripts/flicker/taa_model.py`.

**Display side, measured 2026-09-09 (followup-11).** The oscillation reaching the eye is a few per
cent of radiance amplified by the exposure an interior needs. On the identical HDR sequence, replayed
through a tonemap model validated against the engine to 1 code value, a 4x exposure change multiplies
the px > 40 count by 31x while the linear records stay bit-identical — a threshold count is never an
energy statement. Auto-exposure settles at 66.31 on pose W and is stable when still. Bloom lifts the
dark floor 2.4x and halves local contrast, and its effect on a threshold count changes sign with the
exposure. Two controls used in earlier packages turn out to substitute rather than subtract:
`r.GI.Reflections=0` falls back to the prefiltered IBL cubemap (specular radiance is initialised from
it and only overwritten by the SDF trace), making the interior 11x brighter and the exposure 16x
lower without changing the oscillation; and `r.TAA.PreExposeWeights` is a no-op at exposure 1.0 and,
at the real operating point, makes the bright side oscillate 11x more while crushing the dark side to
2 % of its radiance.

**Which pass owns each region, measured 2026-09-09 (followup-12).** The regions reported as
flickering are not all lit the same way, and two of them are not lit by the deferred pass at all.
With the forward translucent pass disabled, the barred window falls from 2.666 to 0.00006 radiance
and the right-hand shadowed arcade from 0.0539 to 0.00047: their colour is **forward translucent
geometry**, which the deferred lighting records do not describe and which no GI control moves
(every one of them stays at 1.00x). The rear door is mixed (61.5 % deferred). The deeply shadowed
walls and the sunlit floor are fully deferred, and their residuals have different sources again:
the shadowed walls are 97.5 % diffuse indirect with the final gather's per-frame sampling dominating
and GTAO contributing about 40 %, while the sunlit floor is 99 % direct light. The converged
radiance cache contributes nothing to any of them (freezing it after convergence: 1.00x).

**Moving camera: the GDF recentre is a demonstrated cause, 2026-09-09 (followup-13).** Measured with
world-space tracking (each pixel's world point reprojected into the previous frame, 88 % valid
coverage), on a trajectory that crosses one clipmap axis at a time: the motion-compensated residual
is 1.31-1.35x higher on the frames where a clipmap recentres, and the ROI mean steps by 3-6x its
ordinary local change (about 24 % of the shadowed wall's radiance and 9 % of the lit wall's), with no
consistent sign. Shifting the voxel snapping phase by half a voxel moves the events to different
frames **and the elevation follows them**; freezing the centres removes the events entirely and
leaves the residual away from them unchanged. Diagnostic CVars `r.GI.GDF.FreezeCenter` and
`r.GI.GDF.CenterPhaseOffset` (both default to production behaviour). What is *not* isolated is which
half of a recentre does it: re-voxelising the clipmap or moving the sampling origin of everything
that traces it.

### `GlobalSDF` — `FUNCTIONAL` (4 clipmaps, raw + expanded)
**File**: [global_distance_field.cpp](../../runtime/engine/renderer/gi/global_distance_field.cpp)
**Shader**: [global_sdf_composite_cs.hlsl](../../runtime/shaders/runtime/gi/global_sdf_composite_cs.hlsl), [mesh_sdf_bake_cs.hlsl](../../runtime/shaders/runtime/gi/mesh_sdf_bake_cs.hlsl)

Four clipmaps (`r.GI.GDF.Resolution`, default 256 on Sponza) composited on
the GPU from per-mesh SDFs baked at load (`mesh_sdf_baker.cpp`, BVH,
two-sided via `r.GI.MeshSdf.TwoSided`), kept as a raw R16F volume plus an
expanded RGBA16F copy. Feeds the radiance cache trace, SDF reflections, the
final gather and DFS. Scene readiness waits for pending bakes (138 on Sponza)
before the first presented frame. Brixelizer keeps its own SDF; the two are alternatives.

---

### `IrradianceProbeGrid` / `LightProbeVolume` — `PARTIAL`
**File**: [irradiance_probe_grid.cpp](../../runtime/engine/renderer/gi/irradiance_probe_grid.cpp)

ECS component + probe grid added in commit `1892a567` (Phase 0 tasks 2.1/2.2).
Infrastructure is in place but there's no probe bake pipeline yet. Probes
are spawned as ECS entities and the grid is uploaded to the GPU, but the
irradiance data is zero until a bake step (which doesn't exist).

---

## Shadows

### `r.DFS` (Distance Field Shadows) — `OFF` (STUB)

**2026-10-05 (block A.7):** the three `dfs_*_cs.hlsl` are one-line scaffolds and the lighting shader
never reads `dfsOutputSrvIndex`; the passes were dispatched for nothing. Default changed to `false`,
TechPanel row removed. The description below is the intended design, not the state of the code.

**File**: [frame_lighting.cpp:52](../../runtime/engine/renderer/frame/frame_lighting.cpp)
**Shaders**: [shaders/runtime/shadows/dfs_*.hlsl](../../runtime/shaders/runtime/shadows/)

Cone-traced distance field shadows. Works on large-scale scenes but can
produce splotchy noise on cm-scale interiors (low sample count, no temporal
accumulation). Currently ON by default; if you see splotchy AO-like noise
inside buildings, this is the first thing to disable.

### Local light (spot / point) shadows — `FIXED` 2026-10-05 (block A.1)

Until 2026-10-05 no spot or point light cast a shadow: the local atlas was cleared to 0.0 and drawn
with the CSM pipeline (`Less`), so no caster passed; spots then read the atlas with NDC used as UV and
the LESS comparison, which lit both sides of every occluder; point lights had six cube-face tiles
rendered per light and `pl.shadowIndex` was never read. Fixed with linear-distance tiles
(`shadow_depth_linear_ps.hlsl`, `SampleLocalShadow` in the lighting shader, cube-face selection).
Guard: `scripts/test_local_light_shadows.py` (6/6; before 4/6 with lit/shadow ratios 1.07 and 4.2).
Remaining: alpha-masked casters still cast solid shadows (the depth pipelines are vertex-only);
see `docs/analysis/lighting-block-a-2026-10-05.md`.

### Shadow atlas depth dump — `STUB`
The pass-dump code skips depth textures other than the GBuffer depth. The
shadow atlas is D32Float and would need a depth → R8 copy path to be
dumpable. Not critical; shadow atlas can be inspected via RenderDoc.

---

## Reflections

### `r.SSR` — `OFF` (BUG)
**File**: [frame_lighting.cpp:44](../../runtime/engine/renderer/frame/frame_lighting.cpp)
**Shaders**: [shaders/runtime/postprocess/ssr_*.hlsl](../../runtime/shaders/runtime/postprocess/)

Full SSR pipeline exists: tile classify → trace → spatial denoise → temporal
accum → resolve → composite. **Produces visible noise** that multiple tuning
sessions could NOT resolve. The resolved-history pass at one point wrote
zero output. Off by default pending a ground-up rewrite based on the
`diharaw/hybrid-rendering` reference (hit-point reprojection, curvature-adaptive
strategy, tile-based dispatch).

**Recommendation**: wait for RT reflections (inline ray query against TLAS)
to replace SSR entirely for the default path. Keep SSR as SM5 / low-end
fallback.

---

### Reflection Probes — `PARTIAL`
**File**: [reflection_probe_renderer.cpp](../../runtime/engine/renderer/probes/reflection_probe_renderer.cpp)

`ReflectionProbeRenderer` gathers `ReflectionProbeComponent` entities from
ECS, manages dirty capture via `IBLBaker`, uploads a structured buffer of
`ReflectionProbeGPU`. The **renderer side is functional** — probes are
captured and bound — but:
- No parallax correction in the deferred lighting shader
- Probe selection is single-nearest, not blended
- No priority/weight field on `ReflectionProbeComponent`

---

## Raytracing (DXR)

### `r.RayTracing` — `OFF` (SCAFFOLDING)
**Files**:
- [rt_manager.cpp](../../runtime/engine/renderer/raytracing/rt_manager.cpp)
- [blas_manager.cpp](../../runtime/engine/renderer/raytracing/blas_manager.cpp)
- [tlas_manager.cpp](../../runtime/engine/renderer/raytracing/tlas_manager.cpp)
- [shader_table_builder.cpp](../../runtime/engine/renderer/raytracing/shader_table_builder.cpp)
- [passes/raytracing/rt_uv_test_pass.cpp](../../runtime/engine/renderer/passes/raytracing/rt_uv_test_pass.cpp)

Infrastructure exists:
- `RTManager` singleton with per-frame TLAS rebuild
- `BLASManager` builds per-mesh on first draw, cached
- `ShaderTableBuilder` for hit groups / miss shaders
- Only one RT shader: `rt_uv_test_lib.hlsl` — a barycentric UV visualizer

**Sub-CVars declared but not wired:**
- `r.RayTracing.Shadows`
- `r.RayTracing.Reflections`
- `r.RayTracing.AO`
- `r.RayTracing.GI`

Each would need: a ray gen shader, hit/miss shaders, dispatch integration
into the render graph, denoiser, composite into deferred lighting.

**Path forward**: the `diharaw/hybrid-rendering` paper is the reference
architecture — start with `RTShadows` (1 visibility ray/pixel vs sun
direction → SVGF-style tile-based denoise → multiply into direct sun term),
then `RTAO`, then `RTReflections`, then `DDGI` update rays.

---

## Path Tracing

### `r.PathTracer.Enable` — `OFF` (WORKING, PARTIAL FEATURES)
**Files**:
- [passes/pathtracing/path_tracer_pass.cpp](../../runtime/engine/renderer/passes/pathtracing/path_tracer_pass.cpp)
- [shaders/runtime/pathtracing/path_tracer_ps.hlsl](../../runtime/shaders/runtime/pathtracing/path_tracer_ps.hlsl)

Shadertoy-style compute path tracer as a runtime mode toggle. Steps 1-6
working (skeleton → GBuffer first hit → 1-bounce diffuse → direct sun +
multi-sample → GGX specular → progressive accumulation with camera-cut
reset). Full design in
[docs/analysis/path-tracing/](./path-tracing/).

**Known gaps**:
- **Step 7** (ECS analytic primitives for true multi-bounce against arbitrary
  geometry) deferred — currently all bounces escape to sky
- **Step 7 hybrid** (screen-space sun shadow) was implemented then removed
  because it was view-dependent (camera-relative shadow artifacts)
- **Step 8** (Falcor PSNR parity) only has a stub script at
  `scripts/falcor/uzengine_pt_reference.py`; Mogwai execution + scene
  export to `.pyscene` not done
- **No TAA interaction safety** — user must manually `r.TAA=0` when
  enabling PT (both do temporal accumulation)
- **No DLSS/FSR/TSR safety** — must manually disable upscalers
- **CB ring stale-buffer race** (tracked separately in MEMORY.md) can cause
  intermittent artifacts in accumulation

---

## Atmosphere / Sky / Fog

### Sky IBL (cubemap capture, SH, prefiltered maps) — `FIXED` 2026-10-05 (block A.5)

`m_skyIBLDirty` was set once at construction and only ever cleared, so a moving sun kept the sky IBL
of the first frame. The sky pass now re-bakes when the sun direction (> 0.25 deg), the sky sun colour
(> 1 %) or the angular radius change, at most once per 30 passes; each bake still stalls on `WaitIdle`
(asynchronous bake is the follow-up). Guard: `scripts/test_sky_ibl_rebake.py`.


### `r.AerialPerspective` — `OFF` (PARTIAL)

**2026-10-05 (block A.4):** the CVar was registered twice (`true` in `atmosphere_renderer.cpp`, `false`
in `frame_constants.cpp`); the registry keeps the first registration, so the effective default depended
on static-initialisation order. One registration now, default `false`.

**File**: [frame_constants.cpp:76](../../runtime/engine/renderer/frame/frame_constants.cpp)

3D aerial perspective volume (Hillaire 2020) is computed each frame via
`AerialPerspectiveVolume` compute pipeline, but the **deferred lighting
shader integration is gated off** by this CVar. When enabled, the fog
color blending is incorrect — suspect transmittance LUT UV mapping or
view-space Z reconstruction bug. Needs a side-by-side comparison vs. the
Hillaire reference shader.

---

### `r.ScatteringLut` — `OFF` (EXPERIMENTAL)
**File**: [atmosphere_renderer.cpp:37](../../runtime/engine/renderer/atmosphere/atmosphere_renderer.cpp)

Bruneton 2017 3D scattering LUT sky path. Shaders were stubs and got
deleted in the recent session; the C++ pipeline factories were also
removed. The CVar is still declared for compat but the code path is
effectively dead — sky uses the Hillaire 2020 transmittance LUT +
analytic ray march.

---

### Volumetric Clouds — `PARTIAL`
**Files**:
- [cloud_pass.cpp](../../runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp)
- Shaders deleted in recent session: `cloud_noise_shape_cs`, `cloud_noise_detail_cs`, `cloud_weather_cs`

Cloud raymarch compute works (see existing screenshots), but **the noise
volume bakers were stub shaders that got deleted**. The compiled `.bin`
noise assets under `content/atmosphere/` still exist and the renderer
reads them, so the runtime cloud pass works — but regenerating the noise
volumes requires rewriting the 3 baker shaders or baking externally.

Additional gaps:
- No temporal reprojection — clouds flicker
- No cheap LOD (always full quality)
- No cloud shadowing onto the ground
- No sun godray integration from clouds

User comment: "ainda precisa de vários ajustes, não é prioridade agora".

---

### Volumetric Fog — `PARTIAL`
**Files**:
- [volumetric_fog_pass.cpp](../../runtime/engine/renderer/passes/atmosphere/volumetric_fog_pass.cpp)
- Inject / scatter compute shaders exist

Froxel volumetric fog (160×90×64 inscattering volume + temporal integration).
Builds and runs but **not composited into the final HDR** by default — the
integrated volume is computed and left unread. Needs the composite step
wired into `frame_atmosphere_passes.cpp` after height fog.

---

### `r.Fog` (Height Fog) — `ON`
Exponential height fog, Hillaire 2016 unified volumetric. Works correctly.
No known bugs. Listed here for completeness.

---

## Post-Process

### Auto-exposure knobs — `WIRED` 2026-10-05 (block A.8)

`r.AutoExposure.MidGrayTarget`, `SpeedUp`, `SpeedDown`, `LogLuminanceMin/Max` were registered and never
read (the shader hard-coded 0.18, 3, 1 EV/s, 10-90 %). They are root constants now; the defaults
reproduce the previous exposure. `r.ContactShadow.Length` now scales the contact-shadow ray (0.10 =
the previous fixed 1 % of the far plane) instead of only switching it on.

### `r.SkyLight.MinAmbient` — `DEFAULT CHANGED` 2026-10-05 (block A.2)

Default 0.25 was applied as `max(component, cvar)` to the AO clamp and the no-GI irradiance floor,
overriding every scene (Sponza authors 0.01; the shader documents 0.005-0.02). Default is 0 now (the
component value). Sponza Camera C: `lit_wall` -1.6 %, other ROIs bit-identical.


### `r.GTAO` — `OFF` (WEAK EFFECT / SCENE-DEPENDENT)
**File**: [gtao_pass.cpp:24](../../runtime/engine/renderer/passes/postprocess/gtao_pass.cpp)

GTAO pipeline (horizon-based AO + bilateral denoise + temporal accumulation
+ half-res upsample) is functional and dispatches correctly, but the
visual contribution is marginal in well-lit scenes because the directional
sun light + sky light dominate. Previously was disabled due to an
over-occlusion bug (MEMORY.md `project_ambient_light_zero_investigation`).

**Current state (post-2026-04-07 overnight session)**: the over-occlusion
bug hasn't been confirmed fixed, but enabling GTAO with intensity 0.5 /
radius 60 in Sponza produces visible crevice contact shadows. Needs
testing in more scenes, and PIXEL_DEBUG pass-dump validation that the
AO channel matches expected values.

---

### `r.DOF` — `OFF` (PARTIAL)
**File**: [frame_postprocess.cpp:27](../../runtime/engine/renderer/frame/frame_postprocess.cpp)
**Shaders**: `dof_setup_cs.hlsl`, `dof_blur_cs.hlsl`

Depth of field setup + blur compute shaders exist. Off by default, uses
fixed lens parameters, no bokeh kernel sampling, no near-field handling.
Works as a cheap gaussian blur based on circle-of-confusion — acceptable
for background blur but nothing fancy.

---

### `r.LensFlare` — `OFF`
**File**: [lens_flare_pass.cpp:30](../../runtime/engine/renderer/passes/postprocess/lens_flare_pass.cpp)
**Shader**: `lensflare_cs.hlsl`

Ghost-based lens flare compute pass. Builds, runs, visible artifacts around
bright light sources. Off by default because the ghost positions and
chromatic aberration are un-tuned.

---

### `r.MotionBlur` — `ON`
Object-space motion blur (tile max / neighbor max / reconstruction). Works
correctly. Listed for completeness.

---

### Distortion (Heat haze / refraction) — `STUB` (UNWIRED)
**Files**:
- [distortion_pass.cpp](../../runtime/engine/renderer/passes/vfx/distortion_pass.cpp)
- [distortion_resolve_pass.cpp](../../runtime/engine/renderer/passes/vfx/distortion_resolve_pass.cpp)
- Shaders: `distortion_ps.hlsl`, `distortion_haze_ps.hlsl`, `distortion_resolve_ps.hlsl`

Pass classes and shaders are all implemented, but **there is no wiring
in `frame_postprocess.cpp` or `frame_particles.cpp` that calls
`AddDistortionAccumulationPass` or `AddDistortionResolvePass`**. Scene VFX
entities that flag themselves as distortion emitters will NOT render any
refraction until the pass is added to the frame graph.

---

## Materials

### Named pins + MaterialParams — `DONE`
Recent session added UE5/Godot-style named pin serialization (`dstPin`,
`srcPin` as strings) and `MaterialParams` (baseColorTint, roughnessMul, etc).
Translator emits explicit defaults for unconnected pins. Documented in
commit `eb839ed9`.

### `specularMultiplier` — `PARTIAL`
`MaterialParams::specularMultiplier` is declared and serialized but NOT
consumed by the HLSL translator yet (stub `(void)mp.specularMultiplier`).
Needs either an extra out parameter on `ComputeMaterialOutputs()` (requires
changing the gbuffer RT layout to carry per-pixel F0 scale) or baking it
into the baseColor at authoring time for dielectrics.

### HDRI `.hdr` / `.exr` loading — `DONE`
PMREM atlas + raw equirect paths both work. No known bugs.

---

## Subsurface Scattering / Hair / ClearCoat — `STUB`
Shading models `Subsurface`, `ClearCoat`, `Hair`, `ThinTranslucent` are
declared in `MaterialShadingModel` enum and space is reserved in the
GBuffer RT2.w `customW` channel, but:
- No specialized BRDF for any of them in `deferred_lighting_ps.hlsl`
- No authoring support in the material graph compiler
- No scene-side validation

---

## Skeletal Animation — `PARTIAL`
**Files**: [engine/animation/](../../runtime/engine/animation/)

`Skeleton` struct, `BoneTransform`, `.uzskel` file I/O all exist. What's
missing:
- Animation clip / `.uzanim` loading
- State machine / blend tree
- GPU skinning compute shader (currently no bone palette upload)
- Animation-driven morph targets
- IK solvers (FABRIK, CCD)

Listed in the Phase 3 task list but never implemented. ECS has no
`SkeletalMeshComponent` yet.

---

## VFX / Particles

### Particle system — `PARTIAL` (MOSTLY FUNCTIONAL)
Spawn/update/render compute pipeline works. Mesh particle rendering
works. Trails work. Known gaps:
- **Collision** via SDF exists but only tested against Brixelizer global SDF
- **Vector fields / curl noise** helpers exist but no integration test
- **Ribbons / trails** interpolation has a minor popping artifact at
  segment transitions
- **GPU sort** works for alpha-blended particles but no frustum cull yet

### Decals — `DONE`
Deferred decal system (DBuffer) works. Decal material graph integration
works. No known issues.

---

## Lua Scripting

### Base API — `DONE`
LuaJIT integration, sandboxing, hot-reload, `uz.scene` / `uz.mesh` /
`uz.camera` / `uz.material` / `uz.light` APIs all work.

### `uz.pathtracer` — `STUB`
Listed in the path tracer milestone plan but the Lua binding file
`runtime/engine/scripting/api/lua_pathtracer.cpp` does not exist yet.
The C++ side is accessible only via CVars.

### Coroutines / DAP server / LSP IntelliSense — `NOT STARTED`
Planned in Phase 1 specs, no implementation.

---

## RHI Backends

### DX12 — `PRIMARY`
Fully functional, bindless SM6.6+, DXR support detected. All development
happens here.

### DX11 — `PARTIAL`
Files in `runtime/engine/rhi/dx11/` compile but path hasn't been validated
recently. Bindless not supported (SM5 fallback paths exist in shaders).
Used occasionally for backend parity testing; breaks regularly.

### Vulkan — `PARTIAL`
13 files in `runtime/engine/rhi/vulkan/`. SPIRV-Cross wrapper for HLSL →
SPIRV transpilation is set up. Never booted end-to-end. Listed as a
future target in the architecture docs.

### Metal — `STUB`
`metal_device.mm`, `metal_command_list.cpp`, etc. exist but are
Windows-built stubs (no macOS CI). Assume broken until validated on Mac.

---

## Other scaffolded-but-unused

### `Job System` — `STUB`
`engine/job/` directory exists. No current consumers. Renderer is 2-thread
(game thread + render thread) with no task-based parallelism.

### `Async streaming` — `STUB`
Spec exists in `docs/specs/`, no implementation.

### `Undo/Redo` — `NOT STARTED`
### `Editor gizmos` — `NOT STARTED` (beyond the base grid + outline)
### `Multi-select` — `NOT STARTED`
### `Prefab edit mode` — `NOT STARTED`
### `Navmesh` — `NOT STARTED`
### `Plugin system` — `NOT STARTED`
### `Terrain system` — `NOT STARTED`
### `Audio (XAudio2)` — `NOT STARTED` (spec only)
### `Physics (Jolt)` — `NOT STARTED` (spec only)

All of these have specs in `docs/specs/` and tasks in `.rulebook/tasks/`
but no code yet.

---

## Build / Tooling

### Runtime leaks — FIXED (2026-04-07)
`PT_Accum0` / `PT_Accum1` textures leaked at shutdown, triggering D3D12MA
assert. Fixed in commit `cb9f4028`. If similar "Unfreed committed allocations"
errors appear for other textures, grep for their creation site and ensure
`DestroyTexture` is called in the matching shutdown path.

### Shader cache — `STABLE`
DXC-based pipeline cache at `runtime/build/bin/cache/shaders/`. Clearing
it (`rm -rf`) forces full recompile on next boot — useful when shader
includes change without touching the top-level file.

### PSO disk cache — `STABLE`
Pipeline state objects are serialized to disk for fast boot on subsequent
runs.

### Pass-dump — `IMPROVED (2026-04-07)`
Recent session added `DumpChannel` for split views (gbuffer.ao, metallic,
roughness from packed RTs) and enabled gbuffer depth dump via a sqrt curve.
Volfog inscattering/integrated + Brixelizer diffuse/specular outputs are
now registered. Shadow atlas depth still skipped.

---

## Follow-up priority (personal recommendation)

1. **Fix TAA** — unblocks DLSS ghost-fix, TSR tuning, and path tracer
   accumulation coordination. Highest cross-system impact.
2. **RT Shadows via inline ray query** — `diharaw/hybrid-rendering` pattern.
   Replaces the view-dependent contact shadow march. Most visible quality
   upgrade.
3. **Rewrite SSGI march** — or formally retire it in favor of Brixelizer GI.
4. **Wire distortion passes** into frame_postprocess — zero new code,
   just graph registration. Unlocks heat haze / refraction VFX.
5. **GTAO validation + AO-map authoring path** — for when assets actually
   ship with baked AO.
6. **Skeletal animation Phase 1** — GPU skinning compute + clip playback.
   Unblocks characters in the engine.
7. **Path tracer Step 7 ECS primitives** — analytic Cornell-style test
   scene, serves as BRDF validation reference against Falcor.
8. **DLSS Ray Reconstruction + DLSS-G** — once TAA / motion vectors are
   fixed.
9. **Backend parity** — validate DX11 / Vulkan builds compile + boot.
10. **Audio + Physics** — large unblocked deliverables for the next phase.
