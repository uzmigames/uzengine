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
