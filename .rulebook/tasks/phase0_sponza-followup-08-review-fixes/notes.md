# Notes — phase0_sponza-followup-08-review-fixes

2026-09-08. Source of the items: an external static review of the uncommitted Sponza-parity
diff. Every claim was re-derived from the code before changing anything; all seven held.

## Item-by-item

| # | claim | code evidence | fix |
|---|---|---|---|
| 1 | `inputElements` dangles | `VertexLayoutResult { RHIInputElementDesc elements[kMax]; … }` — inline array; `const auto layout = VertexLayout::Standard();` local in `BuildGBufferPipelineDesc` | member `m_standardLayout`, pointer into it |
| 2 | opacity ×2 | `scene_sync.cpp`: `cmd.opacity = GraphConstantInput("Opacity")`; `gbuffer_ps.hlsl` graph branch: `alphaValue = matOpacity; alphaValue *= opacity;` | graph branch: `alphaValue = opacity` |
| 3 | twoSided ignored | `cmd.twoSided` set; `BuildGBufferPipelineDesc`: `cullMode = Back` unconditional; particle-mesh PSO honours it, graph PSO did not | `twoSided` parameter → `None`/`Back`; hash already covers it |
| 4 | black → 0.18 | `if (dot(albedo, albedo) <= 0.0) albedo = groundAlbedo` | validity = `surface.x < 1e8` (distance sentinel), colour untouched |
| 5 | Karis "correct" | followup-06 2.1 text; shader comment | reworded as biased mitigation; energy check added to 4.1 |
| 6 | bloom dims | the "simplified pipeline" block is the only live path (`return` after Tonemap); `tonemapInputSrv` starts as the HDR; bloom got `vpWidth/vpHeight` regardless | dims follow the source (`bloomFromDisplayRes`) |
| 7 | return after BeginRenderPass | tonemap callback | guard moved before the pass |

Sponza content: 0 two-sided graphs, 1 masked graph (`dirt_decal.uzmat`) without a constant
Opacity input — the fixes do not change Sponza's captures; they matter for other content and the
SM5 path.

## Verification

**Build** clean (`zig build`, uzengine.exe + test_runtime 19:32; both shader edits verified embedded by `grep -a`). **gtests** `TaaJitter.*:GIGpuTest.*:DDGISystem.*:*Material*:*Pipeline*:EngineShaders.*` minus `MaterialPipeline_ParticleMesh.*`: **378 passed, 1 failed** — `UZ_MeshSortUzfxPipeline.ApplyStack_CalledTwice_DoesNotDuplicate`, which passes alone and fails inside its own suite (order-dependent VFX test in the user's in-flight area, untouched here). `MaterialPipeline_ParticleMesh.*` is excluded because its `Init(fakeDev=0x1, fakeShader=0x1)` test dereferences fake pointers and aborts the runner (its own comment says "may fail or crash"); the six tests before it pass, so the new `m_standardLayout` member does not affect construction. **Harnesses**: firefly 17/17, rc recentre 40/40, taa bloom input 13/13 (rewritten), ddgi energy 17/17 (black-albedo check added). **Regression captures**: Camera A f640 54.1 / 32.2 % dark (post-TAA reference 54.1; SponzaTest 53.1); close-up static 300→301 0.19 / 0.00 % (jitter-fix reference 0.19 / 0.00 %) — Sponza output unchanged, as expected for content with no two-sided graph and no constant-opacity mask.

## Pending items resolved in the second pass (order requested by the user)

1. **Shader-embed dependency** — no defect. A `.hlsli`-only edit reached `deferred_lighting_ps.hlsl.gen.h`
   and the exe after one build; the zig manifests of `pipeline_lifecycle.cpp` list every generated header
   with a content hash. Earlier "stale" reports = install failure with the exe running. Guard:
   `test_shader_embed_tracking.ps1`.
2. **GPU energy measurements (linear radiance).**
   - Reflections, production `GatherSdfReflections` in the fixture room, same rays Karis vs plain:
     uniform `PASS reflection energy: history=uniform1 pixels=64 rays_per_pixel=8 karis_over_plain=(0.9965,0.9949,0.9949) plain_mean=(0.7929,0.2003,0.1002) karis_mean=(0.7901,0.1993,0.0996)`; layered `PASS reflection energy: history=layers0.05/1/20 pixels=64 rays_per_pixel=8 karis_over_plain=(0.8845,0.8846,0.8846) plain_mean=(1.1941,0.3006,0.1503) karis_mean=(1.0561,0.2659,0.1330)` — energy retained 88 %, 88 %, 88 % (R,G,B).
   - DDGI vs SDF writers on Sponza, same grid, per-probe transport statistic. First run: DDGI skyFraction
     1.0 everywhere — the TLAS was empty because `r.RayTracing` (master) was off while `r.RayTracing.GI`
     was on; the dispatch gate now requires `IsGIEnabled()` and a non-empty TLAS. Rerun: `GI energy (linear, grid mean ray radiance): SDF=1.326 DDGI=0.762 ratio DDGI/SDF=0.5748 skyFraction SDF=0.746 DDGI=0.754 -> parity 1±0.10 NOT MET. Both writers now see geometry at the same rate (sky fraction 0.746 vs 0.754), so the gap is radiance, not geometry: interior probes (z=8 layer, inside the atrium) read SDF 0.002–0.009 vs DDGI 0.000 — the DDGI writer has no multi-bounce term (direct sun only, occluded indoors); exterior, sky-dominated probes read DDGI 1.0 (flat skyLightColor × intensity on miss) vs the SDF writer's sky-cubemap radiance — the sky term explains most of the 0.57`
     (ratio DDGI/SDF 0.5748, parity 1 ± 0.10 NOT MET).
3. **Fragile tests** fixed (no fake pointers; idempotence asserted on state, not on a freed address).
4. **Dead post-process code** removed (750 lines); inert CVars labelled.

## Things this task does not claim

- The DDGI/SDF comparison is now per-probe and linear (item 2 above); parity itself is NOT MET — the number is reported, not tuned.
- The Karis weight remains in place as a firefly mitigation; its energy loss is now measured (above) and it can be switched off with `r.GI.Reflections.HdrWeight=0`.
- (withdrawn) `build.zig` does track the shader embed — see item 1 above.

## Addendum 2026-09-09 — temporal instrumentation and the flicker attribution (pose W)

Code added for the analyses (no behaviour change for users): `--screenshot-frames A-B[:step]` in
`runtime/tools/uzengine.cpp` (every frame of one process as `<base>_<frame>.png`);
`r.TAA.JitterSequenceLength` is now honoured by `frame_setup.cpp` (the constant 8 was used before);
`GI recentre: frame=…` log lines for GDF clipmap and radiance-cache recentres
(`frame_gi_passes.cpp`, `RadianceCache::IsDirty()`). QA scenes `scripts/sponza-flicker-*`.
Packages: `.screenshots/flicker-sampling/` (separate-process pairs — superseded),
`.screenshots/flicker-sequence/` (one-process sequences: 4 vs 16 rays vs reflections off),
`.screenshots/flicker-jitter/` (jitter length 1/4/8/16, TAA off, GTAO off, recentre events).

Findings (per-frame series from one process, autocorrelation):
- The window-bar residual's period equals the jitter length (4 → r@4 0.98, 8 → r@8 0.96, 16 →
  r@16 0.92; confirmed with exact full-resolution counts, r@8 0.96) and disappears at length 1
  (0 px > 40) — note that length 1 is NOT zero jitter: the code always uses Halton23(1) =
  (0, −0.167) px, a constant offset with no temporal variation, so it is a control, not a fix.
  The residual is driven by the TAA jitter cycle. With reflections off the px > 40 COUNT drops
  ~60× (33 → 0.5 px/frame; a threshold count, not an amplitude), 16 rays do not reduce it
  (33 → 33); GTAO does not affect it (34).
- 2×2 subsampling (×4) over-counted sparse single-pixel changes ~2× (shadow wall px > 8: 56
  estimated vs 28 exact); the window counts matched (803 vs 818). Exact counts are quoted where
  a ROI is sparse.
- A 24-frame cycle in the whole-frame series is GTAO's temporal pattern (`frameIdx % 6` angle ×
  `(frameIdx/6) % 4` offset): present with TAA off, absent with `r.GTAO=0`.
- Without TAA the raw per-frame variation is 225 k px > 8 (11 % of the frame); TAA brings the
  static residual to ~1 k px > 8 and ~33 px > 40 (window bars), ~56 px > 8 on the 630 k-px shadow wall.
- In motion, GDF clipmap-0 recentres every 20 frames (19.5 cm at 1 cm/frame, logged). Event
  accounting: 13 events logged from frame 340 on; the analysis window 342..537 holds 11 (340 and
  341 are the first motion frames, excluded; 423 and 505 are x-axis voxel crossings, the rest
  y-axis). The second derivative of the ROI means is 1.9× higher at event frames (±1) and the
  three largest steps (481, 501, 521) coincide with events in all ROIs, but 8 of 11 events show no
  distinguishable step — partial support; a motion-compensated measurement is still needed.
- The `GI recentre` radiance-cache log line fired on ANY dirty state (recentre or history
  invalidation — frames 1..34 were loading invalidations); it now records `reason=`,
  `shiftProbes=` and previous/new origin; the GDF line records clipmap index and previous/new
  centre (2026-09-09).
- Mechanism inside the resolve — demonstrated with the region dump (`.screenshots/flicker-region/`,
  190×550 window rectangle, frames 300..336, 6 float4 records per pixel from the lighting PS and the
  TAA resolve, linear HDR): (1) the raw centre sample is deterministic in the jitter index (|d8| =
  0.0000 Y vs |d1| = 0.22 Y) — no stochastic component; (2) the SDF reflection term is 0.0006 Y
  (0.02 % of the raw) and `r.GI.Reflections=0` leaves the linear output std of the same pixels
  unchanged (0.140 → 0.135); (3) the panes are geometry at a constant 7.98 Y (no sky sentinel
  fired), the bars 0.000 Y; the 5 % most-oscillating pixels are 68 % pane-neighbours whose raw
  never changes but whose jitter-weighted plus filter varies 0 → 1.5 Y, and 31 % edge pixels
  whose raw toggles 7.98/0.00; the variance clamp moves the history in 17 % of their frames and
  those frames carry 41 % of the output change (sawtooth 6.1 → 8.0 → 7.7 → 7.2 → 6.5 Y with
  period 8); (4) the display-referred px > 40 pixels are dark bars next to the panes with a linear
  output change of 0.005–0.10 Y shown as L 94 → 141 / 94 → 195: auto-exposure ≈ 67
  (`r.AutoExposure.MinBrightness` 0.001 with bars/walls at 0.000 Y) makes 0.005 Y ≈ 50 levels, and
  bloom (uniform, threshold −1) sets a ~94 L floor around the window. Reflections off raises the
  bars to 0.03–0.08 Y → the metered mean rises → exposure drops ~3.5× → the same linear
  oscillation no longer crosses 40 levels (37.2 → 0.6 px/frame): that, not the reflections, was
  the earlier "60×". Bloom off drops the floor to 0.4 L and the count explodes (39 → 1547
  px > 40/frame); fixed exposure 66.84 reproduces the base (39.0).
- `UpscalerInput::exposure` was never consumed by the resolve (HDR weights ran at exposure 1.0).
  Opt-in `r.TAA.PreExposeWeights` measured: window px > 40 37.2 → 15.2 and bar jumps
  94→195 → 34→51, but pane-side pixels lose their own weight (1/(32·67+4) vs 1/4) and oscillate
  2.5 Y (output std p99 0.18 → 2.49, window px > 8 831 → 4605). Not a fix; default stays off.
- `--screenshot-frames` file N was render N+1 (request issued after `RenderFrame(N)`); shown
  pixel-by-pixel (PNG L monotone in the next frame's linear output) and fixed to name by render
  index. Earlier packages keep the old naming; their conclusions (periods, static/motion boundary)
  are unaffected.
- Guard: `scripts/test_region_dump.ps1` (13 checks: header/layout, sky sentinel, blend range, no
  leaks, jitter repeats every 8, raw sample identical at the same jitter index and different on
  adjacent frames).
- Still not demonstrated: any reflection effect on the shadowed walls (only the window rectangle
  was instrumented); the motion steps (GDF).
