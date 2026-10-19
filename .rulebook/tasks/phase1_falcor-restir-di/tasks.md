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

## Stage 3: DXR ReSTIR / light-BVH integration (2026-10-19): INCOMPLETE

- [ ] DXR ReSTIR-BVH acceptance: source-PDF correctness, opt-out equivalence,
  and the converged-mean gate pass, but the required variance win over
  **both** constituent methods does not. What remains is reducing the combined
  estimator's equal-low-sample variance below BVH-only on the 108-light scene.
  Do not mark this integration complete from the uniform-ReSTIR win alone.

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
