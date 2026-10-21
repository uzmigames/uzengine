## Stage 1: Opt-in directional-light DXR milestone (2026-10-17)

This bounded milestone integrates initial RIS and static-camera temporal reuse
in the existing `PipelinePathTracerPass`, not the deferred-lighting roadmap
below. Primary DXR hits supply surfaces instead of raster GBuffer hits.
The proposal's spatial reuse, pairwise bias correction, moving-camera
reprojection, general point/emissive-light sampling, light-BVH integration,
and deferred-lighting integration remain deferred.

- [x] Initial RIS: 32 directional-source candidates, streaming weighted selection,
  zero-target handling, and `W = weightSum / (M * target)`.
- [x] Temporal reuse: ping-pong buffers; exact unjittered camera/object/material/light
  signature gate; same-pixel history; normal dot >= 0.9 and scale-aware
  point-to-plane surface validation. Re-evaluate the selected history sample at
  the current hit; retain at most 19 old frame reservoirs plus the new one.
  Rescale history weight with its count; temporal M counts frames, not the 32
  initial candidates.
- [x] Build: `cd runtime && zig build -Doptimize=ReleaseFast` exited 0.
  `test_restir_di.cpp` compiled after removing stale `objectRow0/objectRow2`
  assertions and matching actual CPU strides (material 96, directional light 32).
  Both compute shaders compiled `cs_6_6` and all three DXR libraries compiled
  `lib_6_6`; no `compile failed` in fresh smoke, variance, or reset render logs.
- [x] Default safety: without either opt-in flag, reflection-reference frame 95
  is PNG-byte-identical to `E:/UzmiEngine/f005-after/frames/f_95.png`;
  decoded `max_code_delta = 0`.
- [x] Equal-sample variance proof: both captures use `--path-trace --pt-bounces 1`,
  `r.PathTracer.NEESamples=1`, TAA off, identical 960x540 directional-light
  scene with 90-degree source angle, and one shadow ray per primary hit/frame.
  ReSTIR additionally uses `--restir-di` (32 unshadowed candidate evaluations).
  On 2,954 interior surface pixels, RGB code-error variance against the NEE
  200-sample image at 5 samples is 131.181779 NEE versus 23.238415 ReSTIR:
  **82.2853% lower**. At 10/20 samples the reductions are 68.8280%/31.7836%.
  At 200 samples the mean RGB code values are 63.365832 NEE and 63.392462
  ReSTIR: **+0.04203%**; inverse-sRGB display-linear mean differs by +0.06215%.
  These are PNG measurements, not a claim of general dynamic-scene unbiasedness.
  The existing low-discrepancy NEE overtakes this temporal estimator's error
  variance at 50+ samples; stage 1 claims low-sample improvement only.
- [x] Temporal proof: prior `smoke-verified` probe 14 stayed at M=1; the subsequent
  `smoke-depth` probe plateaued at M=8. Fresh `smoke-finish` probe 14 grows
  M=1,2,...,20 and stays capped at 20. In `reset-finish`, camera movement at
  engine frame 44 resets accumulated history after 43 samples; the next
  probe-14 readback is M=1, then 2,3,... . Projection and object changes also
  invalidate history.
- [x] Layout gate: `npm run test:layouts` exited 0; Tier A 88/88, Tier B 28/28,
  Tier C 9/9. No shared graphics root or shared PerFrameCB grew. The pass-private
  root is 96 bytes / 24 DWORDs.
- [x] Regression gate: `GTEST_FILTER='-RuntimeViewportTest.*' zig build test
  -Doptimize=ReleaseFast` ran 7,844 tests: **7,824 passed**, 17 existing skips,
  and the same three pre-existing `VfxEditorSlate` failures
  (`WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`,
  `StatusBarBindingsReflectTelemetry`). Command exit 1 is solely those failures;
  delta versus the supplied 7,818-pass baseline is **+6 passes, zero new failures**.
  No device-removal/TDR markers occurred, so no transient retry was needed.
  All six `RestirDIStage1` tests passed:
  `StructuredBufferLayouts`, `StreamingRISSelectsProportionalToWeights`,
  `ZeroWeightNeverReplacesSelectedSample`,
  `InitialWeightIsUnbiasedForDiscreteCandidates`,
  `TemporalCombinationReevaluatesCurrentTarget`, and
  `HistoryCapPreservesContributionWeight`.

**Stage 1 is complete within the directional-light DXR/static-camera boundary.**
The expanded roadmap below remains separate and is not claimed complete.

Scoped changed files:

- `.rulebook/tasks/phase1_falcor-restir-di/tasks.md`
- `runtime/engine/app/application_args.cpp`
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.cpp`
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.h`
- `runtime/shaders/runtime/include/restir_di.hlsli` (new)
- `runtime/shaders/runtime/include/restir_math.hlsli` (new)
- `runtime/shaders/runtime/pathtracing/restir_initial_cs.hlsl` (new)
- `runtime/shaders/runtime/pathtracing/restir_temporal_cs.hlsl` (new)
- `runtime/shaders/runtime/pathtracing/pathtracer_closesthit.hlsl`
- `runtime/shaders/runtime/pathtracing/pathtracer_raygen.hlsl`
- `runtime/shaders/runtime/pathtracing/pathtracer_shared.hlsli`
- `runtime/tests/engine/renderer/test_restir_di.cpp` (new)
- `runtime/tests/engine/renderer/test_pipeline_path_tracer.cpp`
- `runtime/tools/uzengine.cpp`
- `scripts/rendering/test_cbuffer_layouts.py`

The private root's shader consumers are raygen, closesthit, miss (unchanged),
initial RIS, and temporal RIS. Its CPU owner is `PipelinePathTracerPass`;
the layout checker and both renderer test files cover the contract.
No shared graphics-root, shared constant-buffer, or deferred-lighting consumer
changes are required. Unrelated VFX-editor work is untouched.

Evidence outside the repository:
`E:/UzmiEngine/restir-stage1-{default,smoke,reset,variance-nee,variance-restir}-finish/`
contains the fresh PNGs, manifests, and render logs.
`restir-stage1-build-final.log`, `restir-stage1-layouts-final.log`,
`restir-stage1-gtests-finish.log`, and `restir-stage1-metrics-final.json`
are in `E:/UzmiEngine/`. The capture and PNG-analysis scripts are in
`E:/UzmiEngine/restir-stage1-evidence/`.

## Stage 2: Opt-in DXR spatial reuse (2026-10-18)

This extends the stage-1 DXR path, not the separate deferred-lighting roadmap.
The reservoir now records its source position, normal, view direction, albedo,
roughness, and metallic value (112-byte stride). Four disk-sampled neighbors
within 30 pixels are combined with the canonical temporal reservoir. Geometry
gates select domains, never samples or weights. Spatial output reuses the
initial buffer; only final shading reads it, so it cannot feed temporal history.
The pass-private root remains 96 bytes / 24 DWORDs.

The contribution invariant is `E[W_i g(Y_i)] = integral_{S_i} g(y) dy`, where
`S_i = {y : p_i(y) > 0}` and `p_i` is the unshadowed source target. For every
sample, evaluate all contributing domains and use
`m_i(y) = M_i p_i(y) / sum_j M_j p_j(y)`. These weights vanish outside `S_i`
and sum to one on the receiving support because the canonical domain is
always included. Stream `w_i = p_here(Y_i) W_i m_i(Y_i)`, select with
probability `w_i / sum_j w_j`, and finalize `W = sum_j w_j / p_here(Y)`.
Conditional expectation over that last selection is
`sum_i W_i m_i(Y_i) g_here(Y_i)`; taking expectation gives
`integral g_here(y) sum_i m_i(y) dy = integral g_here(y) dy`.
No second division by total M is permitted. Null reservoirs remain contributing
domains. Temporal reuse uses the same correction; capping M changes only the
MIS preference, not W. Correlation between source samples does not change the
linearity argument for fixed domains.

Emitter positions are stored in a common area measure, so the identity shift
has Jacobian 1; cosine/distance-squared belongs in the receiving contribution,
not a second Jacobian. The existing NEE/BSDF balance partition is evaluated at
each receiving surface, preserving the complementary emitter-hit contribution.
Visibility is evaluated once, after spatial selection, in final DXR shading.
The base BRDF is unchanged.

- [x] Build and shader dispatch: `cd runtime && zig build
  -Doptimize=ReleaseFast` exited 0; 152 embedded shaders verified.
  Both fresh spatial `run.log` files report `restir_spatial_cs.hlsl` compiled
  `cs_6_6`, PSO ready, and `spatial (4 neighbors, source-target MIS) dispatched`.
  Neither spatial log contains `compile failed` or device-removal markers.
- [x] Directional and 108-emissive-light 200-sample means match plain NEE
  within 0.5% in both RGB-code and inverse-sRGB display-linear measurements.
  PNGs were decoded and visually inspected. On 2,954 directional interior
  pixels, NEE/spatial RGB-code means are 63.365832 / 63.415933:
  **+0.050102 codes, +0.07907%**; display-linear means are
  0.065501660 / 0.065589760: **+0.13450%**.
  On 33,757 emissive interior pixels, NEE/spatial RGB-code means are
  39.821726 / 39.707468: **-0.114258 codes, -0.28692%**;
  display-linear means are 0.030004265 / 0.030102934: **+0.32885%**.
  The emissive log confirms 108 lights / 215 BVH nodes. These finite PNG
  measurements corroborate the invariant above, not an assertion of exact
  finite-sample equality or a general moving-camera proof.
- [x] Equal-low-spp spatial error variance is below temporal-only. With
  1 NEE shadow sample per primary hit, TAA off, identical cameras, 1 bounce
  for directional and 2 for emissive, RGB-code error variance against each
  scene's 200-sample NEE reference is:

  | Scene | Samples | Temporal-only | Spatial | Reduction |
  | --- | ---: | ---: | ---: | ---: |
  | Directional | 5 | 24.281800 | 10.611854 | 56.2971% |
  | Directional | 10 | 13.996161 | 6.349300 | 54.6354% |
  | Directional | 20 | 7.428180 | 3.247283 | 56.2843% |
  | 108 emissive lights | 5 | 1108.582738 | 467.697679 | 57.8112% |
  | 108 emissive lights | 10 | 1010.621970 | 278.513533 | 72.4414% |
  | 108 emissive lights | 20 | 959.672761 | 177.158966 | 81.5396% |

  Temporal-only is the same new binary with `r.ReSTIR.Spatial=0`, including
  source-support correction in temporal reuse. Spatial also beats the
  previously recorded stage-1 directional 5-sample variance, 23.238415.
  Masks use only the NEE reference (mean code 15..180, eroded at +/-4 pixels,
  excluding a 5-pixel image border), not estimator differences. This is
  spatial error variance, the same metric used for stage 1, not repeated-seed
  ensemble variance.
- [x] All five `RestirDIStage2` gtests pass:
  `ProvenanceLayoutPreservesRootBudget`,
  `MISPartitionsUnityAcrossDifferentSupports`,
  `ExactEnumerationRemovesBiasedCombineDrift`,
  `RecursiveTemporalReuseRetainsContributionInvariant`, and
  `CorrelatedSpatialInputsRemainUnbiased`.
  Exact enumeration gives integral **7** with correction and **6** without it
  (-14.2857% negative-control support-loss drift). Every supported basis
  function integrates to 1; five-domain MIS weights sum to 1. No random
  trials, sleeps, or convergence-dependent assertions are used.
- [x] Default safety: neither `--restir-di` nor `--path-trace` was supplied.
  Reflection-reference frame 95 is PNG-byte-identical to
  `E:/UzmiEngine/f005-after/frames/f_95.png`; **max_code_delta = 0**.
  `PipelinePathRoot` is still **24 DWORDs**. `pathtracer_shared.hlsli`,
  including the base BRDF, has no diff.
- [x] Layout gate: `npm run test:layouts` exited **0**; Tier A **88/88**,
  Tier B **32/32**, Tier C **12/12**, including the spatial root and
  112-byte reservoir provenance contract.
- [x] Regression gate: `GTEST_FILTER='-RuntimeViewportTest.*' zig build test
  -Doptimize=ReleaseFast` ran **7,882 tests**, **7,862 passed**, 17 skipped,
  and the same three existing `VfxEditorSlate` failures
  (`WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`,
  `StatusBarBindingsReflectTelemetry`). Command exit **1** is solely those
  failures. Delta versus 7,857 passing baseline: **+5 passes, zero new
  failures**. Test execution took 40.341 seconds. No TDR/device-removal
  markers occurred, so no retry was needed.

**Stage 2 is complete within the opt-in static-camera DXR/base-material
boundary above.** The deferred-lighting, moving-camera, and other expanded
roadmap items remain separate. Builds/tests were bounded at 175 seconds,
captures at 170 seconds. All engine/test/Zig processes were stopped; no commit
or archive was made.

Evidence outside the repository:
`E:/UzmiEngine/restir-stage2-{directional,emissive}-{nee,temporal,spatial}-verify/`
and `restir-stage2-default-nee-verify/` contain PNGs, manifests, and logs.
`restir-stage2-build-finish.log`, `restir-stage2-layouts-finish2.log`,
`restir-stage2-gtests-verify.log`, and `restir-stage2-metrics-verify.json`
are in `E:/UzmiEngine/`. The bounded runner and PNG analysis scripts are in
`E:/UzmiEngine/restir-stage2-evidence/`. The inherited capture helper's invalid
comma-separated screenshot list was corrected to the supported `5-200` range;
the initial failed invocation produced no evidence used above.

Complete stage-2 changed-file list (including the inherited, finished WIP):

- `.rulebook/tasks/phase1_falcor-restir-di/tasks.md`
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.cpp`
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.h`
- `runtime/shaders/runtime/include/restir_di.hlsli`
- `runtime/shaders/runtime/include/restir_math.hlsli`
- `runtime/shaders/runtime/pathtracing/pathtracer_closesthit.hlsl`
- `runtime/shaders/runtime/pathtracing/restir_initial_cs.hlsl`
- `runtime/shaders/runtime/pathtracing/restir_temporal_cs.hlsl`
- `runtime/shaders/runtime/pathtracing/restir_spatial_cs.hlsl` (new)
- `runtime/tests/engine/renderer/test_restir_di.cpp`
- `runtime/tests/engine/renderer/test_restir_di_spatial.cpp` (new)
- `scripts/rendering/test_cbuffer_layouts.py`

Unrelated VFX-editor and other shared-worktree changes were not touched.

## Stage 3: DXR ReSTIR / light-BVH integration (2026-10-19): OCCLUSION GATE CLOSED

- [x] DXR ReSTIR-BVH occlusion acceptance: visibility-aware reuse preserves
  the estimator invariant and passes the 0.5% converged-mean tolerance on
  both scenes. Occlusion error variance is **46.37721% / 43.02730% /
  37.69728% lower than BVH-only** at 5 / 10 / 20 frames, and lower than
  uniform ReSTIR. See the visibility-aware follow-up below. The earlier
  unshadowed measurements remain historical evidence, not the current gate.
  This closes the requested static-camera occlusion gate, not the expanded
  deferred-lighting/moving-camera roadmap or an equal-GPU-cost claim.

The committed shader already called `SampleLightBVH`, stored its joint area
PDF, and streamed `target / sourcePdf`; its stage-2 emissive captures enabled
the BVH. This increment makes that contract shared/testable with
`RestirCandidateWeight`, corrects the PDF field documentation, and identifies
the combined dispatch in `run.log`. Candidate selection is stratified across
32 BVH-CDF intervals only for importance traversal. Their equally weighted
mixture is the original BVH density, so the pooled RIS weight still divides
by `selectionPdf / triangleArea`, not by an extra stratum factor. Directional
sources retain their discrete selection PDF. The target, temporal/spatial
correction, 112-byte reservoir, material contracts, and 24-DWORD root are
unchanged. BVH-off retains exactly the committed stage-2 generator.

Evidence is outside the repository in `E:/UzmiEngine/restir-bvh-proof/`.
`prove.py analyze restored nee-uniform restored` reproduces the final metrics
in `metrics-final.json`. Captures use `light-bvh-emissive`, 108 lights /
215 nodes, 640x360, two bounces, roulette/TAA off, and one NEE shadow sample
per frame for all low-sample comparisons. `uniform/` is ReSTIR with uniform
emitter selection (`r.LightBVH.Uniform=1`), without hierarchy traversal;
literal `r.LightBVH=0` is tested separately because it also removes emissive
candidate enumeration in the committed engine. `bvh/` disables ReSTIR.
`nee-uniform/` is the converged ordinary uniform-emitter NEE reference:
ReSTIR off, no importance traversal, 32 NEE samples/frame, frame 2050.

- Converged PNG means: combined/reference whole-image RGB code means
  **10.608559 / 10.605849**, delta **+0.02555%**. On 36,621 reference-only
  interior pixels, RGB-code means are **38.208214 / 38.180188**
  (**+0.07340%**); inverse-sRGB display-linear means are
  **0.028306252 / 0.028190655** (**+0.41005%**). Both interior mean deltas
  are within the stage-2 0.5% tolerance. PNGs were decoded and visually read;
  these finite measurements corroborate the estimator invariant, not exact
  finite-sample equality.
- Equal-low-sample RGB-code error variance against that reference:

  | Samples | Combined | Uniform ReSTIR | BVH only | vs uniform ReSTIR | vs BVH only |
  | --- | ---: | ---: | ---: | ---: | ---: |
  | 5 | 421.160413 | 1697.648757 | 420.023284 | -75.19155% | **+0.27073%** |
  | 10 | 242.721635 | 1516.934011 | 214.301138 | -83.99920% | **+13.26194%** |
  | 20 | 154.781422 | 940.092518 | 109.749122 | -83.53551% | **+41.03204%** |

  The reference-only mask is mean code 15..180, eroded at +/-4 pixels,
  excluding a five-pixel border. This is spatial error variance, not an
  ensemble variance claim. Whole-image and own-converged-reference metrics
  also fail the BVH-only comparison. Three strategies were measured:
  independent candidates (`combined/`), selection-stratified (`stratified/`,
  retained and recaptured as `restored/`), and selection plus independently
  shifted triangle-area stratification (`joint/`). None beat BVH-only.
- `baseline-off/` was captured from a clean build of the committed source
  before edits. `off-restored/` has mean delta **0**, max code delta **0**,
  and identical PNG bytes at frames **5, 10, 20, 200**. Flag-free
  `default-restored/frames/f_95.png` is byte-identical to
  `E:/UzmiEngine/f005-after/frames/f_95.png`, max code delta **0**.
- Integrated logs identify `initial BVH-sourced RIS (joint source PDF)`,
  temporal and spatial dispatch. No proof capture contains `compile failed`
  or device-removal markers. Engine captures exit 0.
- `RestirDIStage3.BVHSourceDensityPreservesUnbiasedRISWeight` passes.
  Exact enumeration gives area integral **41**, versus **5.3** for the
  negative control omitting the source PDF. Exact stratum/CDF intersections
  also give **41**, proving the mixture-density normalization.
- Build exits **0**, 152 shader integrity checks. `npm run test:layouts`
  exits **0**: Tier A **89/89**, B **33/33**, C **12/12**.
  `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast`
  runs **7,909** tests: **7,889 pass** (**+1** versus 7,888), 17 skip,
  and only the three pre-existing `VfxEditorSlate` failures:
  `WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`,
  `StatusBarBindingsReflectTelemetry`. Exit **1** is solely those failures;
  final test execution **37.069 seconds**. No TDR retry was needed.
  Logs: `E:/UzmiEngine/restir-bvh-{build-final,layouts,gtests-final}.log`.
  clangd is unavailable; the actual build and runtime DXC provide compiler
  validation. Each build/test is bounded to 175 seconds, each capture to 170.

Scoped changed files:

- `.rulebook/tasks/phase1_falcor-restir-di/tasks.md`
- `.rulebook/tasks/phase1_falcor-light-bvh/tasks.md`
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.cpp`
- `runtime/shaders/runtime/include/restir_di.hlsli`
- `runtime/shaders/runtime/include/restir_math.hlsli`
- `runtime/shaders/runtime/pathtracing/restir_initial_cs.hlsl`
- `runtime/tests/engine/renderer/test_restir_di_bvh.cpp` (new)

The BRDF, GI/multi-bounce ReSTIR roadmap, unrelated WIP, and scratch files
are untouched. Final exact-name process enumeration found no `uzengine.exe`,
`test_runtime.exe`, or `zig.exe` processes. No commit or archive was made.

### Occlusion follow-up, 2026-10-19: gate honestly remains open

New scene: `scripts/rendering/restir-occlusion.uzproject` / `.uzscene`.
It retains nine emissive cubes (108 triangles / 215 BVH nodes), a floor and
back wall, and adds four opaque, dark cell dividers: x = +/-75 and
y = +/-75, extending from z = 150 to 320. The fixed camera is below them.
Exact segment/AABB intersection from 441 floor-grid points to the nine
emitter centers finds 3,008 / 3,969 segments blocked (**75.78735%**).
This diagnostic measures emitter-center geometry, not a GPU shadow-ray histogram.

Reproduce with `python scripts/rendering/measure_restir_occlusion.py capture
OUTPUT MODE`, where MODE is `combined`, `bvh`, `uniform`, `reference`, or
`default`; use a new OUTPUT directory and run serially. Then run
`python scripts/rendering/measure_restir_occlusion.py analyze OUTPUT`.
Evidence: `E:/UzmiEngine/restir-occlusion-proof-v2/`, including `metrics.json`,
all five manifests, `run.log` files, and PNGs. Each capture exited **0** and
was bounded to 170 seconds. No capture log contains `compile failed` or
device-removal markers. The initial attempt in `restir-occlusion-proof/`
exited 1 before rendering because the CLI does not accept comma-separated
screenshot frames; the retained script uses the supported range syntax.

The three strategies have identical 640x360 cameras, two bounces, roulette
and TAA off, and one NEE shadow sample per primary hit per frame.
`uniform` means plain ReSTIR with `r.LightBVH.Uniform=1`: emitter enumeration
is retained, but there is no hierarchy importance traversal. Literal
`r.LightBVH=0` removes emissive candidate enumeration and is not an equivalent
emissive-ReSTIR comparator. The ordinary plain-NEE reference disables ReSTIR,
uses uniform emitter selection without importance traversal, and averages
32 NEE samples per frame through frame 2050. It is not the BVH-only reference.

PNG reference, combined high-sample, and both low-sample comparison images
were decoded and visually read. The reference-only interior mask is the
previous study's mean-code 15..180 mask, eroded at +/-4 pixels with a
five-pixel border excluded; it contains **2,316** pixels. At frame 2050:

- Combined/reference interior RGB-code means: **71.513817 / 71.575993**,
  delta **-0.08687%**; inverse-sRGB display-linear means:
  **0.086325471 / 0.086513478**, delta **-0.21732%**.
- Whole-image display-linear means: **0.001195433 / 0.001194264**,
  delta **+0.09781%**. Both linear checks meet the existing 0.5% tolerance
  and support the unbiased estimator contract; finite PNGs are not a proof
  of exact equality or raw-HDR energy equality.
- Whole-image RGB-code means: **1.403168 / 1.393980**, delta **+0.65915%**.
  This code-space check does not meet 0.5%; it is reported rather than hidden.
  The almost-black background makes code-space quantization/noise significant.

Equal-low-sample spatial RGB-code error variance against the same ordinary
NEE reference (not repeated-seed ensemble variance):

| Samples | ReSTIR x BVH | BVH only | Uniform ReSTIR | Combined vs BVH | Combined vs ReSTIR |
| --- | ---: | ---: | ---: | ---: | ---: |
| 5 | 1878.585387 | 1790.591979 | 4220.197851 | +4.91421% | -55.48585% |
| 10 | 1144.353998 | 876.718699 | 5042.886549 | +30.52693% | -77.30756% |
| 20 | 788.021408 | 445.431933 | 3396.884011 | +76.91175% | -76.80164% |

Whole-image error variance also loses to BVH-only: combined/BVH/uniform at
5 samples is **26.640894 / 24.229555 / 67.659046**; at 10 samples,
**16.238490 / 12.073689 / 68.719992**; at 20 samples,
**11.015423 / 6.095419 / 44.155189**.

Why occlusion does not close this gate: `RestirContribution` is unshadowed.
Neither initial, temporal, nor spatial selection evaluates visibility, and
the reservoir does not store the outcome of the final shadow ray.
`pathtracer_closesthit.hlsl` traces that ray only after reservoir selection.
Consequently reuse cannot favor samples discovered to be visible; it keeps
reusing high unshadowed-contribution samples even behind these dividers.
Temporal correlation can persist while ordinary BVH NEE draws fresh samples.
The static camera already provides temporal coherence; moving the camera or
objects would reset this implementation's history via the transport signature,
not demonstrate reprojection. Changing candidate count/radius cannot add the
missing visibility feedback. No integration tuning or moving-scene claim was
made, and **the Stage-3 gate remains [ ]**.

Verification on the RTX 5080:

- `cd runtime && zig build -Doptimize=ReleaseFast`: **exit 0**, 152 shader
  integrity checks; log `E:/UzmiEngine/restir-occlusion-build.log`.
- Default `reflection-reference`, no `--path-trace`, frame 95:
  PNG bytes identical to `E:/UzmiEngine/f005-after/frames/f_95.png`,
  **max_code_delta = 0**; `default/` capture exited 0.
- `npm run test:layouts`: **exit 0**, A 89/89, B 33/33, C 12/12;
  `E:/UzmiEngine/restir-occlusion-layouts.log`.
- `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast`:
  **7,910 tests, 7,890 passed, 17 skipped**, matching the supplied pass
  baseline. Exit **1** solely for the three existing VFX failures:
  `VfxEditorSlate.WorkbenchHasTwoColumns`,
  `VfxEditorSlate.TitleBarReflectsEmitterName`,
  `VfxEditorSlate.StatusBarBindingsReflectTelemetry`. Test execution
  **43.817 seconds**; no TDR and no retry needed.
  Log: `E:/UzmiEngine/restir-occlusion-gtests.log`.
- Build/tests bounded to 175 seconds. Final process cleanup verifies no
  `uzengine`, `test_runtime`, or `zig` processes. Python LSP diagnostics are
  clean. No engine/shader/base-BRDF changes, no commit, and no archive.

Complete changed-file list for this follow-up (earlier Stage-3 list is historical):

- `.rulebook/tasks/phase1_falcor-restir-di/tasks.md`
- `.rulebook/tasks/phase1_falcor-light-bvh/tasks.md`
- `scripts/rendering/restir-occlusion.uzproject` (new)
- `scripts/rendering/restir-occlusion.uzscene` (new)
- `scripts/rendering/measure_restir_occlusion.py` (new)

### Visibility-aware follow-up, 2026-10-19: occlusion gate closed

The inherited uncommitted implementation was inspected and retained, rather
than restarted. It was already wired end to end; this follow-up supplies the
missing GPU, layout, and regression proof. Initial RIS still samples 32
unshadowed candidates with the existing joint source PDF and stratification.
One inline shadow query rejects its selected sample by multiplying `W` and
`weightSum` by visibility, without renormalizing or dropping its domain/M.
Temporal and spatial reuse use visible targets in **every** source/receiving
evaluation and GRIS denominator. Only own-source visibility is cached;
cross-domain visibility is traced at the corresponding surface. The ray
origin, opaque flags, mask, and emitter endpoint match final DXR shading.
Spatial output remains separate from temporal history.

Unbiasedness: unshadowed initial RIS has `E[W g(Y)] = integral g`.
Selected-sample rejection therefore gives
`E[V W g(Y)] = integral_visible g`. Write `t_i = V_i * p_hat_i` and
`m_i(y) = M_i t_i(y) / sum_j M_j t_j(y)`. Streaming
`w_i = t_here(Y_i) W_i m_i(Y_i)` and finalizing
`W = sum_i w_i / t_here(Y)` cancels the receiving target in conditional
selection expectation. Source expectations then sum to
`integral_visible g * sum_i m_i = integral_visible g`: the canonical source
covers the receiving support, the MIS weights partition unity there, and a
source weight vanishes outside its visible support. Null sources retain M
and their domains in the denominator. The history cap changes only the MIS
preference. Correlated spatial inputs do not require independence for this
sum of expectations. The complementary existing BSDF/emitter MIS term is
unchanged; no base-BRDF modification was made.

The visibility float grows the structured reservoir from 112 to **116 bytes**
at offset **112**. CPU/HLSL declarations, allocation/readback strides,
layout entry, and consumers agree. **PipelinePathRoot stays 96 bytes /
24 DWORDs**. The compute dispatch now binds the existing TLAS root table.
The mechanism remains behind opt-in ReSTIR plus the light-BVH descriptor;
BVH-off retains the stage-2 generator and visibility factor 1.

Evidence: `E:/UzmiEngine/restir-visible-proof/`. All seven serialized captures
exit **0**; no `run.log` contains `compile failed` or device-removal markers.
PNGs were decoded and visually read. Replay the occlusion study with the
committed `measure_restir_occlusion.py`; the external `verify.py` captures
the emissive reference/combined pair and `verify.py analyze` reproduces
`metrics.json`. Both scenes use 640x360, two bounces, TAA/roulette off,
one NEE sample/frame for tested methods, and ordinary uniform-emitter NEE
with ReSTIR off and 32 NEE samples/frame for the frame-2050 reference.

Converged frame-2050 combined / plain-NEE means:

| Scene | Interior pixels | RGB-code means | Code delta | Inverse-sRGB display-linear means | Linear delta |
| --- | ---: | --- | ---: | --- | ---: |
| Occlusion | 2,316 | 71.696891 / 71.575993 | +0.16891% | 0.086832760 / 0.086513478 | **+0.36905%** |
| 108-light emissive | 37,618 | 38.165931 / 38.150637 | +0.04009% | 0.028176307 / 0.028157012 | **+0.06853%** |

Whole-image linear means are respectively **0.001197903 / 0.001194264**
(**+0.30470%**) and **0.006995627 / 0.006993278** (**+0.03359%**).
Whole-image RGB-code deltas are **+0.21857% / +0.00106%**. All combined
mean checks satisfy the existing 0.5% tolerance. These finite 8-bit PNG
measurements corroborate the analytic invariant; they do not assert exact
finite-sample equality or raw-HDR equality. The reference-only mask remains
mean code 15..180, eroded at +/-4 pixels with a five-pixel border excluded.

On the 75.78735%-occluded scene, equal-low-frame spatial RGB-code error
variance against the same plain-NEE reference is:

| Frames | ReSTIR x BVH | BVH only | Uniform ReSTIR | vs BVH | vs ReSTIR |
| --- | ---: | ---: | ---: | ---: | ---: |
| 5 | 960.165335 | 1790.591979 | 4066.222807 | **-46.37721%** | **-76.38680%** |
| 10 | 499.490358 | 876.718699 | 4927.537876 | **-43.02730%** | **-89.86329%** |
| 20 | 277.516212 | 445.431933 | 3294.179770 | **-37.69728%** | **-91.57556%** |
| 50 | 133.829715 | 192.882209 | 1665.658494 | -30.61583% | -91.96536% |
| 200 | 43.774719 | 49.457106 | 436.285368 | -11.48953% | -89.96649% |

This reverses the prior **+4.91421% / +30.52693% / +76.91175%** losses
against BVH-only at 5 / 10 / 20 frames. Whole-image variances also win:
combined/BVH/uniform are **13.386430 / 24.229555 / 64.468249**,
**7.073240 / 12.073689 / 66.661658**, and
**3.884402 / 6.095419 / 42.393022** at those frame counts.
Uniform ReSTIR retains emitter enumeration with `r.LightBVH.Uniform=1`,
without hierarchy importance traversal, and receives the same visibility
correction in this comparison. Its frame-2050 interior linear mean delta
is still **+1.33599%**, so no converged-mean pass is claimed for that noisy
comparator. The combined estimator also beats the historical pre-fix plain
ReSTIR variances (4220.197851 / 5042.886549 / 3396.884011).
This is spatial error variance, not repeated-seed ensemble variance.
Equal frame count is **not equal GPU cost**: visibility queries add work;
the accepted gate is the explicitly permitted equal-low-sample comparison.

Verification:

- Build `cd runtime && zig build -Doptimize=ReleaseFast`: **exit 0**,
  152 shader integrity checks (`E:/UzmiEngine/restir-visible-build.log`).
- `npm run test:layouts`: **exit 0**, Tier A **89/89**, B **33/33**,
  C **12/12**, including DXC reflection of the 116-byte reservoir and
  24-DWORD root (`E:/UzmiEngine/restir-visible-layouts.log`).
- Flag-free `reflection-reference` frame 95 is **PNG-byte-identical** to
  `E:/UzmiEngine/f005-after/frames/f_95.png`, **max_code_delta = 0**.
- `VisibilityReusePreservesSupportAndRejectsOccludedHistory` passes exact
  enumeration of differing visible supports: basis expectations **1, 0, 1**.
  The inconsistent unshadowed-denominator control yields **1/172** instead
  of 1 for the first visible basis. `VisibilityReuseReducesVarianceAgainstUnshadowedControl`
  preserves mean **1** in both modes and reduces exact variance to **2**
  from **greater than 6** for the pre-fix final-shadow-only control.
- `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast`:
  **7,912 tests, 7,892 passed (+2 versus 7,890), 17 skipped**, and only
  the three pre-existing `VfxEditorSlate` failures:
  `WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`,
  `StatusBarBindingsReflectTelemetry`. **Exit 1** is solely those failures;
  test execution **43.755 seconds**. No TDR/device-removal markers occurred,
  so no retry was needed (`E:/UzmiEngine/restir-visible-gtests.log`).
- Python layout diagnostics are clean. clangd is unavailable; the real
  engine build, runtime DXC, and gtests supply compiler validation.
- Captures are bounded to 170 seconds, build/test commands to 175 seconds.
  Final exact-name enumeration found **no `uzengine`, `test_runtime`, or
  `zig` processes**. No commit or archive was made.

Complete scoped changed-file list, including the inherited WIP:

- `.rulebook/tasks/phase1_falcor-restir-di/tasks.md`
- `.rulebook/tasks/phase1_falcor-light-bvh/tasks.md`
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.cpp`
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.h`
- `runtime/shaders/runtime/include/restir_di.hlsli`
- `runtime/shaders/runtime/include/restir_math.hlsli`
- `runtime/shaders/runtime/pathtracing/restir_initial_cs.hlsl`
- `runtime/shaders/runtime/pathtracing/restir_temporal_cs.hlsl`
- `runtime/shaders/runtime/pathtracing/restir_spatial_cs.hlsl`
- `runtime/tests/engine/renderer/test_restir_di.cpp`
- `runtime/tests/engine/renderer/test_restir_di_bvh.cpp`
- `runtime/tests/engine/renderer/test_restir_di_spatial.cpp`
- `scripts/rendering/test_cbuffer_layouts.py`

The base BRDF, unrelated VFX-editor work, and ignored scratch files were
not touched. The evidence helper and generated captures/logs are outside
the repository.

## Stage 4: Opt-in one-bounce GI-ReSTIR, stage 1 (2026-10-20)

Reservoir resampling of the path tracer's INDIRECT term: the first indirect
(secondary) vertex becomes a reservoir sample with temporal and spatial reuse
under the stage-3 bias-correction discipline. Opt-in `r.PathTracer.GIReSTIR`
(CLI `--gi-restir` on both entry points), read at path-tracer init; sub-CVars
`r.PathTracer.GIReSTIR.Temporal` (default on) and `.Spatial` (default on).
Multi-bounce path reuse / reconnection stays deferred; bounces beyond the
second vertex keep the plain estimator ("partial radiance").

Technique. At a receiving primary surface `S = (O, n1, v1, material)` with
`O` the continuation ray origin, the one-bounce term in area measure is
`I(S) = integral f1(S,w) cos1 * L_o(x2 -> S) * V(S,x2) cos2 / d^2 dA(x2)`.
The sample `y` is the secondary vertex plus everything its exitant radiance
needs toward ANY receiver: position, shading normal, base material,
unweighted emission + BVH emitter id (the receiver re-applies its own
BSDF/NEE balance), and the single NEE light sample taken at `x2`
(direction, `emission * geometry / pdf * visibility`, solid-angle pdf), so
`L_o(x2 -> S)` is re-shaded with the vertex BSDF toward each receiver
(`restir_gi.hlsli: GIRadiance / GIContribution`). The canonical candidate is
the path tracer's own BSDF continuation with `W = d^2 / (p_w cos2) = 1/p_A`
(the generating density in area measure); a missed/unsupported continuation
is a NULL sample that still owns its domain (`M = 1`), and its plain
estimate is added directly. Temporal reuse (same pixel, plane/normal
validated, 20-frame M cap) and spatial reuse (4 neighbors, 30-px disk) use
the committed stage-3 math: targets `p_hat_i(y) = lum(g_i(y)) * V_i(y)` with
exact visibility at EVERY domain (inline ray from the source's own
continuation origin, same TMin/TMax/flags as the generating ray), cached
visibility only for a sample's own source, generalized balance MIS
`m_i = M_i p_hat_i / sum_j M_j p_hat_j`, `w_i = p_hat_here W_i m_i`,
`W = sum w / p_hat_here(Y)`, identity shift on scene points (Jacobian 1 in
area measure). A resolve pass adds `GIContribution * W` to the partial
radiance and runs the same Kahan accumulation as the raygen.

Unbiasedness (E[estimator] = the one-bounce integral). The NEE variates at
`x2` are part of `y`; their law depends only on `(x2, n2)`, never on the
receiver, so the extended-space density is `p_A(x2) p(xi | x2)` and
`integral g_S(y) p(xi|x2) dxi dA = I(S)` exactly (the stored NEE sample is
an unbiased estimate of the vertex's direct term for every receiver because
the vertex BSDF is re-evaluated toward that receiver). The canonical
candidate therefore satisfies `E[W g_S(Y)] = I_hit(S)` on the support of
surface hits; the null outcome keeps `M = 1` so no domain is ever randomly
absent (dropping reuse on a miss would scale the history terms by the hit
rate: the gtest negative control shows basis 0 at `1 - miss * m_history`).
With visibility inside every target, `m_i` vanishes outside the visible
support of domain `i`, partitions unity on the union, and the canonical
pixel covers the receiving support, so by GRIS (Lin et al. 2022)
`E[g_here(Y) W] = sum_i integral_supp_i g_here m_i = I_hit(S)`. The plain
fallback covers the complement (environment hits, hair/stack vertices,
grazing/touching vertices), so the total expectation is `I(S)`; the M cap
only changes the MIS preference. No base-BRDF change (D7 untouched);
`PipelinePathRoot` stays 96 bytes / 24 DWORDs; the GI block is a private
8-DWORD `RestirGIRoot` (b4); the GI reservoir is 160 bytes (88-byte sample,
`visibility` at 156). Gate: NEESamples 1, >= 2 bounces, no roulette before
the second vertex, no NRD/volume/layered, BVH or <= 1 directional light.

- [x] Build: `cd runtime && zig build -Doptimize=ReleaseFast` **exit 0**
  (`E:/UzmiEngine/gi-restir-stage1/build2.log`, 156 shader integrity checks);
  all three GI compute shaders compile cs_6_6 at runtime; no `run.log`
  contains `compile failed` or device-removal markers.
- [x] Scene `scripts/rendering/gi-restir-indirect.uzscene/.uzproject`: open
  Cornell-like box, black environment, one sun through the open front; the
  ceiling and upper walls (top 42% of the frame) are lit by the bounce only.
  Harness `scripts/rendering/measure_restir_gi.py` (capture + analyze);
  640x360, 2 bounces (plus a 4-bounce pair), one NEE sample, roulette off,
  2,055 frames, PNGs every 5 frames. Evidence `E:/UzmiEngine/gi-restir-stage1*`
  (`metrics.json` = `analyze` of `gi-restir-stage1-new`; `metrics-x3.json` =
  the sun-x3 pair in `gi-restir-stage1-bright3`; `evidence_montage.png`).
- [x] Canonical consistency: `--gi-restir` with temporal+spatial off
  reproduces the plain estimator sample for sample: frames 5/50/500 max
  code delta **1** (mean 4.3e-6 .. 1.4e-6 code), frame 2050 **max 0**.
- [x] Unbiased converged mean (frame 2050, 98,385 interior pixels, the plain
  path tracer as reference; RGB-code means, this output curve being linear in
  radiance up to clipping: a x3 sun gives x3.00 code): temporal **+0.00064%**
  (one-bounce rows +0.0051%, whole image +0.0002%); temporal+spatial (default)
  **-0.00080%** (rows -0.0046%, whole +0.0045%); spatial only -0.0023% (rows
  -0.0136%); 4-bounce temporal vs 4-bounce plain **+0.00030%** (rows +0.0005%);
  sun-x3 variant (rows at code 112 instead of 37, floor ~0.02%): default vs
  plain **+0.00044%** (rows **+0.0016%**, 0.0017 code). No-reuse is exactly 0.
  Inverse-sRGB "linear" means are NOT the right statistic here: a convex decode
  of two images with different per-pixel noise inflates the noisier (plain)
  mean (Jensen), which read as a spurious -0.12% for the smoother GI images in
  the dark rows (-0.16% at x3 where the code-space delta is +0.0016%); the
  plain reference's own first/second-half inverse-sRGB means differ by -0.22%
  there. All deltas are far inside the stage-3 0.5% tolerance; these are
  finite 8-bit measurements corroborating the analytic invariant.
- [x] Variance win at equal low frame counts (interior error variance in
  RGB code against the frame-2050 plain reference):

| Frames | Plain | Temporal only | Spatial only | Temporal+spatial (default) |
| --- | ---: | ---: | ---: | ---: |
| 5 | 337.681 | 516.772 (+53.0%) | 81.183 (-76.0%) | **59.492 (-82.4%)** |
| 10 | 165.948 | 286.728 (+72.8%) | 40.957 (-75.3%) | **27.010 (-83.7%)** |
| 20 | 82.782 | 152.254 (+83.9%) | 20.415 (-75.3%) | **12.738 (-84.6%)** |
| 50 | 32.533 | 57.495 (+76.7%) | 8.462 (-74.0%) | **4.779 (-85.3%)** |
| 200 | 7.708 | 9.955 (+29.1%) | 2.521 (-67.3%) | **1.277 (-83.4%)** |

  One-bounce rows only (plain / default): 788.2 / 132.4, 387.6 / 59.5,
  193.8 / 27.9, 76.4 / 10.5, 18.0 / 2.9; whole image 208.9 / 44.4,
  103.3 / 20.9, 51.1 / 9.8, 20.1 / 3.6, 4.77 / 0.92. Temporal reuse alone
  LOSES on the accumulated mean: with one candidate per frame it is a
  recursive average of the pixel's own samples (the running mean for the
  first 20 frames, then an EMA), so accumulating its outputs over-weights
  early samples; the analytic prediction is +54% / +68% / +90% at 5 / 10 /
  20 frames against the measured +53% / +73% / +84%. It carries no new
  information for an accumulated image (its per-frame output is the
  low-noise signal a real-time denoiser consumes); the spatial pass adds the
  neighbors' independent samples, which is why it is on by default.
  Equal frame count is not equal GPU cost (visibility queries add work).
- [x] Default safety: flag-free `reflection-reference` frame 95 is
  PNG-byte-identical to `E:/UzmiEngine/f005-after/frames/f_95.png`,
  **max_code_delta 0** (committed exe and final build).
- [x] `--path-trace` with GI off == committed path tracer: the DXR libraries
  compile the GI code only when the CVar is on at init (`UZ_GI_RESTIR`), so
  the plain libraries are preprocessed from the committed text; all **410/410**
  captured frames of the plain run are byte-identical to the committed
  build's capture (max 0). Without that gate the recompiled libraries differed
  in 3/410 frames by one pixel at one code (a driver-level fma-fusion effect;
  two runs of the same binary agree 410/410, so the engine is deterministic).
- [x] gtest `runtime/tests/engine/renderer/test_restir_gi.cpp`
  (`RestirGIStage1.*`, 6 tests): layouts (88/160/32 bytes, root 24 DWORDs),
  canonical W reproduces the plain estimator, temporal combine stays exact
  with a 25% null canonical for M = 1 and 19, negative controls (uniform
  1/(M+1) weights integrate 6 instead of 7; skipping reuse on a null canonical
  gives basis 0 = 1 - 0.25 * 0.9), recursive history over four frames, and
  equal-mean exact variance halving. All six pass (`E:/UzmiEngine/gi-restir-stage1/gtests.log`).
- [x] Layout gate: `npm run test:layouts` **exit 0**; Tier A **90/90**,
  Tier B **36/36** (+2: `PathTracerRoot` in `restir_gi_temporal_cs`,
  `RestirGIRoot`), Tier C **14/14** (+2: `GISample`, `GIReservoir`);
  inventory 0 unmapped. `test_cvar_registrations.py` 3/3.
- [x] Regression gate: `GTEST_FILTER='-RuntimeViewportTest.*' zig build test
  -Doptimize=ReleaseFast`: **7,945 tests, 7,925 passed (+6 new), 17 skipped**,
  and only the three pre-existing `VfxEditorSlate` failures
  (`WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`,
  `StatusBarBindingsReflectTelemetry`); **exit 1** is solely those; 44.006 s;
  `RestirDIStage1/2/3`, `LightBVH*`, `RTReflectionStage1/2/3` all green; no
  TDR/device-removal markers, so no re-run was needed.

Changed files (every GI reservoir consumer):

- `runtime/shaders/runtime/include/restir_gi.hlsli` (new: `RestirGIRoot` b4,
  `GISample`, `GIReservoir`, `GIRadiance`, `GIContribution`, `GIVisibility`,
  `GIVisibleTarget`)
- `runtime/shaders/runtime/pathtracing/restir_gi_temporal_cs.hlsl` (new)
- `runtime/shaders/runtime/pathtracing/restir_gi_spatial_cs.hlsl` (new)
- `runtime/shaders/runtime/pathtracing/restir_gi_resolve_cs.hlsl` (new)
- `runtime/shaders/runtime/pathtracing/pathtracer_raygen.hlsl` (`UZ_GI_RESTIR`:
  reservoir clear, partial-radiance write)
- `runtime/shaders/runtime/pathtracing/pathtracer_closesthit.hlsl`
  (`UZ_GI_RESTIR`: receiver record at bounce 0, candidate capture at bounce 1)
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.h`
  (`RestirGISample`, `RestirGIReservoir`, `RestirGIRoot` + size asserts)
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.cpp`
  (CVars, b4 root slot, define-gated DXR compile, GI PSOs/buffers, gate,
  temporal/spatial/resolve passes, logging)
- `runtime/engine/app/application_args.cpp`, `runtime/tools/uzengine.cpp`
  (`--gi-restir`)
- `runtime/tests/engine/renderer/test_restir_gi.cpp` (new)
- `scripts/rendering/test_cbuffer_layouts.py` (Tier B/C entries)
- `scripts/rendering/gi-restir-indirect.uzscene`, `.uzproject`, the sun-x3
  `gi-restir-indirect-bright.uzscene`, `.uzproject`,
  `scripts/rendering/measure_restir_gi.py` (new)
- `.rulebook/tasks/phase1_falcor-restir-di/tasks.md` (this section)

The base BRDF, docs/specs, the VFX-editor WIP and untracked scratch files
were not touched. Captures and logs live outside the repository; no commit
or archive was made.

## Phase C: real-time ReSTIR GI (2026-10-20, Lumen-parity phase C stage 1: screen-space ReSTIR for the SDF surface final gather, `r.GI.ReSTIR`, opt-in, default 0)

Proof dir: `E:/UzmiEngine/gi-restir-phasec` (harness `phasec.py` — `--tag before|before-pure|after --exe <bin> --case <name> [--suffix=-x] [--cvar k=v]`, every engine run < 15 s, bounded 180 s; `--case metrics` / `region-metrics`; `metrics.json`, `metrics_region_final.json`, `metrics_region_variants.json`, `metrics_tolsweep.json`; montages `montage_box_after.jpg`, `zoom_box_ceiling_after.jpg`, `noise_map_ceiling.jpg`, `montage_sponza.jpg`, `zoom_sponza.jpg`; build/gate logs under `E:/UzmiEngine/_phasec/`). Binaries: `before-bin/` = the Phase B binary (sha256 `7125fb15…`, c26665ed + the user's uncommitted VFX-editor WIP, the same bytes Phase B's proof recorded), `before-pure` = a clean c26665ed checkout built in the worktree `E:/UzmiEngine/_wt_c26665ed` (sha256 `52e47261…`; its `test_runtime` did not link because the committed tests include the untracked `pathtracer_furnace_test.h`, the engine installed), `after` = this binary (sha256 `fc3ec0b2…`), all ReleaseFast with the pinned zig 0.15.2.

- [x] C.1 Pipeline + wiring: `ssgi_restir_initial_cs` / `_temporal_cs` / `_spatial_cs` (+ `ssgi_restir_gi.hlsli`) created on first use by `PipelineLibrary::GetSsgiRestir*`; `SSGIRenderer::DispatchRestir` replaces `DispatchTrace` only under `r.GI.ReSTIR 1` with the world gather on (`EnsureRestirResources`: three 64 B/pixel half-resolution structured reservoirs, initial + temporal ping-pong, created lazily so OFF allocates nothing; UAV barriers order the three dispatches and fence last frame's reads; the spatial pass writes the plain trace's two textures with its barriers, so the spatial denoise, temporal accumulation, upsample and `deferred_lighting_ps` consume it unchanged). History is dropped on the gather's own conditions (reset, frame gap, field revision, mode change, resize, a frame on the plain path) and inside the block B.2 change boxes (the same `HistoryCB` at b2, `FillHistoryCB` shared with the temporal accumulation). CVars at file scope: `r.GI.ReSTIR` (0), `r.GI.ReSTIR.MaxHistory` (20 frames x candidates), `r.GI.ReSTIR.Spatial` (0), `r.GI.ReSTIR.SpatialRadius` (6 half-res px). Root block 16 of 24 DWORDs; PerFrameCB untouched.
- [x] C.2 Estimator (the unbiasedness argument): the sample is the gather's RAY DIRECTION with the radiance it returned from the reservoir's own surface; every domain is the gather's own — its hemisphere in solid angle, cosine density cos/pi, integrand L(omega) cos/pi with L = `TraceSdfSurfaceRadiance` (the trace, the hit shading, the sky read). Initial RIS: weight lum(L) (target / density, cos/pi common), `W = sum / (N p_hat)`, so the canonical estimate's luminance is exactly the plain mean `sum lum(L_i) / N` (gtest `InitialCandidatesReproduceThePlainGatherMean`). Reuse REPLAYS a direction at another surface by tracing it from there (identity shift map, Jacobian 1; cached when the two stored surfaces are bit-identical — the static camera — because the trace is deterministic); the temporal pass takes the generalized balance heuristic over M-weighted targets at both domains, the spatial pass the pairwise MIS `m_k = M_k p_k / (K M_k p_k + M_c p_c)`, `m_c = (1/K) sum_k M_c p_c / (K M_k p_k + M_c p_c)` (partitions unity, vanishes outside each source's support, 2K rays instead of K(K+1)); the canonical reservoir keeps M = N when every candidate missed. The first version reused HIT POINTS (reconnection): measured biased because this tracer registers hits on a distance-dependent expanded surface, so a neighbour's hit is not a point the receiver's rays can produce — per-pixel |delta| 0.96 % / 4.4 % / 8.2 % at radius 2 / 6 / 12 half-res px, independent of the visibility tolerance (5-156 cm sweep) and of the history cap; temporal reuse at the same surface was exact (the geometry term cancels). Replaced by direction replay (C.5). gtest `runtime/tests/engine/renderer/test_ssgi_restir_gi.cpp` (`SsgiRestirGI.*`, 8): the three passes enumerated exactly on a discrete direction space over `restir_math.hlsli` — initial, temporal (different supports, M caps 2N..20N), null canonical (M = N kept), spatial with two neighbours (pairwise) and K = 1, recursive history over four receivers, variance halves at equal mean; negative controls: uniform M/(N+M) MIS and unshadowed cross-domain targets both drift (> 1e-3).
- [x] C.3 OFF == c26665ed, byte-identical: reflection-reference frame 95 (`r.TAA 1`, the stage 1 `default` CVars) with `r.GI.ReSTIR 0` pinned vs the Phase B binary's default AND vs the clean c26665ed worktree build: `max_code_delta 0`, `png_bytes_identical true` (also with no ReSTIR CVar at all, and the two reference binaries agree byte for byte, so the VFX WIP does not touch the frame); the same with the gather ON (`r.RadianceCache 1`, `r.GI.FinalGather 1`): `ref-gather-off` vs both references max 0, bytes identical; the plain path is deterministic run to run (box f150, two OFF runs: max 0). `r.GI.ReSTIR 1` does change the image (ref-gather f95 max 9 / 38,448 px; box f60 max 3 at exposure 1).
- [x] C.4 Unbiased (`gi-restir-indirect`, 640x360 open Cornell box, gather on, TAA off, `r.Exposure 4`, 400 frames, static camera; the converged image of a run = the mean of frames 200-399 (8-bit) / 250-399 (linear HDR region dump of the lighting pass's own `diffuse_indirect`, ceiling rect 384x112, 43,003 geometry pixels — the 8-bit frames quantize once the noise is under one code: the first hit-point version read −0.23 % there as iso-brightness zebra bands, see `biasmap_box.png`)): default (temporal) **+0.008 %** luminance vs the current GI (rgb +0.013 / +0.006 / +0.012 %), per-pixel |delta| 0.32 % (the residual noise of two 150-frame means); 8-bit indirect rows (32,640 px) +0.009 %; Sponza frame 300 mean +0.008 %. Opt-in spatial 3: +0.030 %, per-pixel |delta| 1.12 %. No `--path-trace` comparison: the PT and the raster gather differ on this scene for reasons unrelated to ReSTIR (projection / sky conventions, Phase B follow-up 2), so the reference is the current real-time GI converged.
- [x] C.5 Noise win at equal budget (the same 8 rays per half-res pixel, the same denoise / accumulation chain): per-pixel temporal std of the ceiling's indirect term (HDR, 150 converged frames) **2.61 % -> 1.41 % of the mean (−47.6 %)**, per-frame error variance against the converged image −69.3 %; 8-bit: std 0.261 -> 0.133 codes (−49 %), frame error variance −21 % at f100, −24 % at f150, −47 % at f300 (`noise_map_ceiling.jpg`: the std map; `montage_box_after.jpg` / `zoom_box_ceiling_after.jpg`: f150 of each run next to both converged images). The opt-in spatial reuse (3 neighbours, radius 6) is unbiased but NOISIER once the temporal reservoir has converged (std +47.7 %, 1.97 % of the mean; 1 neighbour +20 %): replaying a neighbour's direction from this surface re-evaluates the integrand (the neighbour's good direction is a proposal, not a reusable value), and the M-weighted MIS trusts 168 candidates drawn for the neighbour's integrand; hence `r.GI.ReSTIR.Spatial` defaults 0. Its intended value — convergence after disocclusion and under motion — was not evaluated (follow-up 1).
- [x] C.6 Perf (GPU profiler, `--run-manifest`): box 640x360 SSGI pass 1.040 -> **0.987 ms** (default; a static camera replays nothing: 0 extra rays), GPU frame 2.608 -> 2.575; spatial 3: 1.972 ms (+0.93). Sponza 1920x1080 project config, 172-frame window from 150: SSGI **10.80 -> 11.57 ms (+0.78)**, GPU frame **23.84 -> 24.85 ms (+1.0)** — the TAA jitter moves the reconstructed surface every frame, so the temporal pass replays two rays per pixel; spatial 3: SSGI 18.81 ms (+8.0), frame 32.09. Reservoirs: 3 x 64 B x half-res = 10.8 MB (box) / 99.5 MB (Sponza), allocated only under `r.GI.ReSTIR 1`.
- [x] C.7 Verification: `zig build -Doptimize=ReleaseFast` (zig 0.15.2) exit 0, `Shader integrity OK (159 shaders verified)` (+3; no orphan zig kill was needed: the only hung process was a DLL-less copy of the before exe, replaced by `before-bin/` with the DLLs); the three shaders also compile standalone with the repo DXC; `npm run test:layouts` exit 0 (`all layouts agree`, Tier A **91/91** (+1 `SSGIRestirHistory` vs `HistoryCB`), Tier B **37/37** (+1 `SSGIRestirRootConstants` 64 B / 16 DWORDs), Tier C **16/16** (+1 `SSGIRestirReservoir` 64 B), inventory 0 unmapped); `GTEST_FILTER=-RuntimeViewportTest.* zig build test -Doptimize=ReleaseFast`: **7,977 ran (Phase B: 7,969), 7,957 passed (+8), 17 skipped**, only the 3 pre-existing `VfxEditorSlate.*` failures, `SsgiRestirGI.*` 8/8, 45.07 s, no TDR / device removal, no re-run needed; every capture's run.log: no `compile failed`, no device removal, `SSGI ReSTIR GI: reservoirs created` + `dispatched` on every ON run only.
- Follow-ups (not stage 1): (1) evaluate the spatial reuse where it should pay — disocclusion and camera motion (the temporal history alone restarts from N candidates) — and the hybrid of reconnection for distant hits (bias ~ expansion / distance) with replay near; (2) the temporal replay under TAA costs 2 rays per pixel because the jittered surface never matches bit for bit — a sub-pixel tolerance on the stored surface would cache it at the price of a sub-pixel reconnection error; (3) reservoir samples carry their radiance from the frame they were traced: the radiance cache's convergence reaches the output up to MaxHistory frames late, and a hit inside a B.2 change box keeps its stale radiance (the receiver, not the hit, is tested against the boxes); (4) the 8-bit screenshot path cannot measure a sub-code mean (zebra bands): sub-percent GI comparisons must use the region dump's `diffuse_indirect`.

Changed files:

- `runtime/shaders/runtime/ssgi/ssgi_restir_gi.hlsli` (new: root block, `HistoryCB` mirror, the 64 B reservoir, surfaces, cosine / replay / targets, compatibility, reprojection)
- `runtime/shaders/runtime/ssgi/ssgi_restir_initial_cs.hlsl`, `ssgi_restir_temporal_cs.hlsl`, `ssgi_restir_spatial_cs.hlsl` (new)
- `runtime/engine/renderer/core/pipeline/pipeline_gi.cpp`, `pipeline_library.h`, `pipeline_library_impl.h` (the three PSOs, lazily created)
- `runtime/engine/renderer/gi/ssgi_renderer.h` (`SSGIRestirRootConstants`, `SSGIRestirReservoir`, the reservoir members and the three methods), `ssgi_renderer.cpp` (CVars, `FillHistoryCB`, `EnsureRestirResources`, `DestroyRestirBuffers`, `DispatchRestir`, the history reset)
- `runtime/tests/engine/renderer/test_ssgi_restir_gi.cpp` (new, 8 tests)
- `scripts/rendering/test_cbuffer_layouts.py` (Tier A `SSGIRestirHistory`, Tier B `SSGIRestirRoot`, Tier C `SSGIReservoir`)
- `.rulebook/tasks/phase1_falcor-restir-di/tasks.md` (this section)

The base BRDF (D7), PCSS, area lights, reflections, docs/specs, the VFX-editor WIP, the clouds scratch and the untracked scratch files were not touched. Captures, the harness and logs live outside the repository; no commit or archive was made.

## Expanded roadmap: Reservoir Data Structure + Initial Sampling

- [ ] 1.1 Create `include/restir_di.hlsli` — `struct LightSample { uint lightIndex; float3 position; float3 normal; float3 emission; float pdf; }`. `struct Reservoir { LightSample sample; float weightSum; uint M; float W; }`. `void UpdateReservoir(inout Reservoir r, LightSample s, float weight, float u)` — streaming RIS update (Talbot et al. 2005 Eq. 3). @ref Bitterli et al. 2020
- [ ] 1.2 Create `restir_initial_cs.hlsl` — `[numthreads(8,8,1)]` entry `main`. For each pixel: read GBuffer (world pos, normal, material). Iterate N=32 candidate lights from `StructuredBuffer<LightData>`. For each candidate, compute unshadowed target function `p_hat = Le * BRDF * |cosTheta| / dist²`. Update reservoir via `UpdateReservoir`. Write final reservoir to `RWStructuredBuffer<Reservoir> uReservoirs`.
- [ ] 1.3 Compute final weight `W = (1/M) * weightSum / p_hat(selected)` and store in reservoir. Handle degenerate case (p_hat = 0 → W = 0).
- [ ] 1.4 Build + verify: initial sampling pass compiles and dispatches. Reservoir buffer populated with plausible light indices (verify via GPU readback of first 10 pixels).

## Session 2: Temporal Reuse

- [ ] 2.1 Create `restir_temporal_cs.hlsl` — for each pixel, reproject to previous frame using motion vectors (multiply by prev view-proj). Sample previous reservoir from `tPrevReservoirs` at reprojected UV.
- [ ] 2.2 Disocclusion validation: reject previous reservoir if depth difference > threshold OR normal dot < 0.9. On rejection, use only current reservoir.
- [ ] 2.3 Pairwise MIS combination: evaluate previous reservoir's selected sample at current pixel (compute `p_hat` at current shading point). Combine via `MIS weight = p_hat_current / (p_hat_current + p_hat_prev)`. Clamp `M` to `min(M_prev + M_cur, 20)` to bound temporal bias. @ref Bitterli et al. 2020 Section 4.3
- [ ] 2.4 Ping-pong reservoir buffers: `RestirDiPass` maintains `reservoirBuffer[2]` swapping each frame. Write combined reservoir to current frame's buffer.
- [ ] 2.5 Build + verify: temporal pass produces stable reservoir selection on a static scene; no flickering (verify via 10-frame screenshot comparison).

## Session 3: Spatial Reuse

- [ ] 3.1 Create `restir_spatial_cs.hlsl` — 5 iterations of spatial resampling. Each iteration: pick 1 random neighbor within 30px radius using low-discrepancy sequence. Sample neighbor's reservoir from `tReservoirs`.
- [ ] 3.2 Jacobian correction: for reusing a neighbor's light sample at current pixel, apply geometry Jacobian `|cosTheta_neighbor / cosTheta_current| * (dist_current² / dist_neighbor²)` to account for different shading point. @ref Bitterli et al. 2020 Appendix A
- [ ] 3.3 Visibility reuse: before accepting neighbor's sample, cast a shadow ray via `TraceRay` with `ACCEPT_FIRST_HIT_AND_END_SEARCH`. Reject sample if occluded (set MIS weight to 0).
- [ ] 3.4 Write final spatial-reused reservoir to output buffer for deferred lighting consumption.
- [ ] 3.5 Build + verify: spatial pass produces visibly reduced noise vs initial sampling alone. Compare screenshots (initial only vs initial+temporal+spatial).

## Session 4: Deferred Lighting Integration + Verification

- [ ] 4.1 Add `--restir-di` CLI flag: activates all 3 ReSTIR passes in engine loop before deferred lighting.
- [ ] 4.2 Update `deferred_lighting_ps.hlsl`: add `#ifdef RESTIR_DI` path — read final reservoir for current pixel, evaluate PBR shading for the selected light. Fallback to existing N-light loop when disabled.
- [ ] 4.3 `RestirDiPass` C++ class: `Init(device, width, height)` allocates reservoir ping-pong buffers. `Execute(cmd, gbufferSRVs, lightBuffer, motionVectors)` runs initial → temporal → spatial passes in sequence. Returns final reservoir SRV.
- [ ] 4.4 Scene with 1000 point lights: populate via Lua script placing lights in grid. Verify: `--restir-di` renders all lights correctly in under 10ms (GPU timestamp from `--gpu-profile`). Without `--restir-di`, same scene should be visibly slower.
- [ ] 4.5 Build + verify: screenshot comparison of 1000-light scene with and without ReSTIR. Run `--flip` to confirm ReSTIR output converges to same reference as brute-force N-light evaluation (with enough spatial iterations).

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass
