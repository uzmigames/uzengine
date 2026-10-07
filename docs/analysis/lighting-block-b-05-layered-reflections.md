# Lighting block B, item 5 — layered reflections (SSR → SDF → IBL)

Rulebook task `phase0_lighting-block-b-05-layered-reflections` (proposal, design, spec
`layered-reflections`). Branch `feat/sponza-gi-parity`, 2026-10-07. The parecer's "SSR em camadas":
reflections composed as SSR → SDF probe reflections → IBL instead of the additive post-TAA composite
(no BRDF weight, no Fresnel, added on top of the IBL specular).

## Before

- The screen-space reflections produced nothing. `r.SSR 1` and `r.SSR 0` rendered bit-identical
  frames of `lighting_reflection_test` (a chrome floor under spheres and coloured walls; 2.07 M
  pixels, max difference 0.0), and the pass-inspector dump of `ssr.trace` was zero in every pixel,
  there and in Sponza (`ssr.resolve` / `ssr.temporal` in Sponza carried an alpha of ~0.97 with no
  colour: the denoisers' output of nothing). The known-issues entry said "produces visible noise":
  it produced nothing visible at all.
- The pyramid was the first cause. A diagnostic build made the trace write what it samples: the
  Hi-Z mips read as zero for every non-sky pixel while the full-resolution depth was right; a
  second diagnostic that added 0.25 in the pyramid shader came back through the trace as 0.25
  (mip 0) and 0.50 (mip 1), so the pyramid's UAV writes and the trace's reads work: the pyramid's
  input read as zero. It was dispatched right after the depth prepass; the GBuffer pass clears the
  depth-stencil before drawing (`gbuffer_pass.cpp`, `ClearDepthStencil`), so at the pyramid's
  position the depth is not the frame's. Dispatched after the GBuffer pass, the mips equal the depth
  (the same diagnostic: mip quantiles 0.36 / 0.51 / 0.74 × 10⁻² for both mips and the depth).
- With the pyramid fixed the trace hit (29.6 k pixels of the mirror floor) but the lighting still saw nothing.
  The render graph orders passes only through the resources they declare (a chain through each resource's successive accessors, then Kahn's sort with a FIFO queue). `SSR_Temporal` declared only its velocity read, so it was scheduled right after the velocity pass, before the trace and the resolve: it read last frame's resolved texture, the spatial denoise (chained on depth, after the resolve) read the temporal's stale history and wrote zeros into the resolved texture, which the next frame's temporal read back. With the denoise off the lighting saw the raw resolve; with it on, nothing, from frame 2 on. Fixed by declaring the depth read the temporal pass already performs (and the prepare-indirect pass's, which had no edge at all).
  With the chain in order the trace hit the boxes (the CPU replica placed the hits on them) and the lighting blended 29.6 k pixels, yet the floor stayed black: every hit colour was near zero. The trace samples last frame's colour from `m_ssrPrevSceneColor`, filled by `SSR_CopyPrevColor` after TAA; that pass declared no graph resource either, so it ran first in the frame and copied the TAA history before the frame touched it, with barriers assuming a state it was not in: the copy carried nothing usable. Declaring its read of the HDR colour (and the legacy composite's) chains both behind TAA.
  The smoke view was a confusion, not a cause: a diagnostic wrote the roughness and the fade before the
  early-outs and showed every non-sky pixel of the smoke view at roughness 1.0 (the checker floor and a
  wall; the camera of `lighting_reflection_test` looks along +X and sees none of its glossy objects), so
  the march never ran there, as it never runs on Sponza's stone (roughness above the 0.35 cut-off). The
  mirror reference scene is where the march is measured.
- The layering was half-wired: with `r.GI.Reflections 1` the deferred lighting lerps the SSR into
  the SDF-traced radiance before the BRDF; with the IBL path the SSR is ignored there and
  `SSR_Composite` adds `ssr.rgb × confidence² × intensity` onto the TAA output. `r.SSR` is off by
  default and Sponza's project sets `r.SSR 0`; `r.SSR.MaxRoughness`, `NumSteps`, `StartMipLevel`,
  `SlopeToleranceScale` and `Composite` are each registered twice (block A.8 list).
- Side finding, not fixed here: the GBuffer pass discards the depth prepass's output by clearing the
  depth-stencil, so the prepass (0.38 ms in Sponza at 1080p) buys no early-Z for the GBuffer.

## What changed

- The Hi-Z pyramid is built after the GBuffer pass (`frame_geometry.cpp`); GTAO (optional input),
  SSGI and the two-phase GPU culling read the same mips.
- `deferred_lighting_ps.hlsl`: the SSR is sampled once; the lerp (confidence-weighted) applies to the reflected radiance of
  whichever layer is active, the SDF gather or the prefiltered cubemap, before the DFG / Fresnel /
  multi-scatter terms, and on the environment-MIS path (smooth metals, where the MIS estimate replaces the
  split-sum specular) the hit times the single-scatter Fresnel term is blended over the MIS specular by
  confidence; the lighting pass binds the SSR texture only while SSR is active (never a stale
  one); `r.SSR.Composite` defaults to 0 in both registrations and the legacy additive post-TAA composite
  runs only when it is set (the double-count control of R2), no longer tied to the SDF flag.
- `scripts/reflection_reference.py`: a 30 × 30 m chrome floor (metallic 1, roughness 0.02, base
  colour 0.95), three rough boxes and a pillar under a sun at 40°, and the same scene without the
  floor seen from the camera mirrored through the floor plane. Planar-reflection identity: the
  floor's reflection at a pixel is what the mirrored camera sees through the same pixel beyond the
  plane; the engine's up-fixed camera renders that image flipped vertically (`mirror_pixel`; measured, the first assumption was a 180° rotation), which the
  harness verifies per pixel through the dumps' world positions (R0).
- `scripts/test_layered_reflections.py` (R0-R5 and the reported classes), montages.

## Gates (`scripts/test_layered_reflections.py`)

| Gate | Criterion | Result |
|---|---|---|
| R0 | the mirrored view maps onto the reflected rays (>= 90 % of interior hits within 0.5 deg, median <= 0.2 deg; first pre-registration 99 %, the rest sit at box silhouettes) | PASS: 0.9125 of 42725 interior px (44694 hits), median angle 0.082 deg |
| R1 | SSR layer: on-screen reflected hits match F x reference (median error <= 0.15) | PASS: median error 0.150 over 44648 px |
| R1 (control) | SSR off is >= 2 x worse | PASS: ssr off 1.000 vs on 0.150 |
| R2 | no double count: the legacy composite brightens the final image >= 1.3 x over the default on the reflecting floor (screenshots, since the dump is stamped before the post-TAA composite), default dump ratio within [0.85, 1.15] | PASS: legacy / default 2.714 over 25976 px; default dump ratio 0.915 |
| R3 | temporal stability on the floor: SSR on <= 3 x off + 0.002 | PASS: on 0.00004, off 0.00001 |
| R5 | SSR passes <= 3.0 ms at 1080p (reference and Sponza) | PASS: reference 0.121 ms, Sponza 0.107 ms |
| R4 | Sponza with SSR off: direct_light identical to the committed build, colour within the same-build run-to-run envelope | PASS: direct 0.00e+00 (repeat 0.00e+00); colour vs committed 71125 px, median rel 0.090; repeat pair 89670 px, median rel 0.156; changed near a depth edge 0.6112827570710333 |


**Numbers (after).** Floor pixels: 1142581, geometry hits in the mirrored view 44694 (on the reflected ray 0.9125, median angle 0.082 deg). Layers against F x reference: SSR on: visible err 0.150 ratio 0.915 (44648 px), offscreen err 0.998 ratio 0.002 (46 px), sky err 0.996 ratio 0.004 (1097887 px); SSR off: visible err 1.000 ratio 0.000 (44648 px), offscreen err 1.000 ratio 0.000 (46 px), sky err 1.000 ratio 0.000 (1097887 px); legacy composite: visible err 0.056 ratio 0.952 (44648 px), offscreen err 0.998 ratio 0.002 (46 px), sky err 0.996 ratio 0.004 (1097887 px); SSR on + SDF layer: visible err 0.150 ratio 0.915 (44648 px), offscreen err 0.998 ratio 0.002 (46 px), sky err 0.996 ratio 0.004 (1097887 px). Temporal |delta| on the floor: on 0.00004, off 0.00001. SSR passes: reference 0.121 ms, Sponza 0.107 ms; Sponza with SSR on changes 0.00 % of the pixels by > 2 % (DeferredLighting 11.18 -> 11.31 ms). Sponza with SSR off against the committed build: direct_light max |diff| 0.0e+00; colour: 71125 px changed (median relative 0.090) against 89670 px (median 0.156) between two runs of this build, 61 % of them at depth edges: the frame's colour is not reproducible run to run at edges while its direct light is. Harness 7/7.

**Intermediate state (Hi-Z and chain order fixed, the previous-colour copy still floating, legacy composite):** SSR on: visible err 0.996 ratio 0.004 (44648 px), offscreen err 0.999 ratio 0.001 (46 px), sky err 0.996 ratio 0.004 (1097887 px); SSR off: visible err 1.000 ratio 0.000 (44648 px), offscreen err 1.000 ratio 0.000 (46 px), sky err 1.000 ratio 0.000 (1097887 px); harness 2/7 (R0, R1, R1, R2, R4 failing).

## Still open

- Side finding: the environment-MIS specular is black on smooth metals in every scene without an HDRI (the chrome sphere of `chrome_test` renders as a black disc; the mirror floor shows nothing of the sky with SSR off) although the env-map sampler builds its alias table from a sky cubemap with a positive luminance; its history, cubemap or temporal path is wrong. Own item.
- The SDF reflection layer has no denoiser and the reflection capture probes of the phase2 SSR task
  (4.2) do not exist: off-screen reflections fall to the SDF gather or the prefiltered cubemap.
- `r.SSR` stays off by default and Sponza's project keeps `r.SSR 0`: a look decision for the user
  (montage attached); Sponza's stone is rougher than `r.SSR.MaxRoughness` (0.35) anyway.
- The depth prepass is discarded by the GBuffer clear (side finding, own item).
- The `r.SSR.*` CVars registered twice (block A.8 list).
