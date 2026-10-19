## Session 1: Emissive Triangle Extraction + BVH Construction

- [x] 1.1 Extract emissive triangles from the gathered draw list in `LightBVH::Gather()` (`light_bvh.cpp`), retaining uploaded LOD0 positions/UVs only when enabled before mesh upload. Cache using a hash of transforms, geometry identities, emission, UVs, textures, and directional lights.
- [x] 1.2 Build 30-bit centroid Morton codes and stable-sort emitters in `light_bvh.cpp`.
- [x] 1.3 Build a balanced binary hierarchy from Morton order, aggregating AABBs and emission power. Stage 1 uses conservative full-sphere cones; tighter cones and SAH are deferred.
- [x] 1.4 Upload pass-private structured buffers for nodes, emitters, and configuration through triple-buffered upload allocations in `pipeline_path_tracer_pass.cpp`.
- [x] 1.5 Verify 100-triangle root power and parent bounds/power in CPU tests; GPU reference scene engages 108 triangles and 215 nodes. See the stage-1 evidence below.

## Session 2: GPU Traversal HLSL + Integration

- [x] 2.1 Implement `include/light_bvh.hlsli` with stack-free, single-path traversal bounded to 32 levels and three independent selection/triangle variates.
- [x] 2.2 Split the selection variate by child power, inverse squared distance, and clamped receiver cosine; preserve nonzero support and handle directional/mixed nodes conservatively.
- [x] 2.3 Sample triangle area uniformly using square-root barycentrics. The joint area PDF is `P(select triangle) / triangleArea`, not the reciprocal of that probability.
- [x] 2.4 Evaluate the same selection PDF by parent traversal. NEE and BSDF emitter hits use matching area-to-solid-angle conversion and complementary balance MIS weights, including the NEE sample count. Exact draw/primitive identities survive Morton sorting.
- [ ] 2.5 Wire into `restir_initial_cs.hlsl` as the candidate generator when `HAS_LIGHT_BVH=1` define is set. Build + verify: scene with 100 emissive cubes — sampling probability distribution concentrates correctly in front-facing emitters (validate via readback histogram of selected triangle IDs).

## Session 3: Validation + Demo

- [x] 3.1 Rebuild from `LightBVH::Gather()` when its transport-input hash changes; unchanged gathered data avoids rescanning triangles.
- [ ] 3.2 Demo: place 100 emissive cubes in a grid at varying intensities. Render with `--restir-di` + light BVH. Verify: brighter cubes receive proportionally more samples (histogram check). Compare convergence rate vs uniform sampling via 100-sample screenshot comparison.
- [ ] 3.3 Build + verify: `--screenshot-stdout` after 64 samples shows correct multi-emitter lighting. No visible sampling bias (no dark patches near bright emitters). GPU timestamp (`--gpu-profile`) confirms traversal cost is sub-millisecond for 100 emitters.
- [x] 3.4 Stage-1 path-traced emissive demo: equal-budget BVH on/off captures pass the mean gate at frames 210 and 2050 while retaining the low-sample variance improvement. ReSTIR demo/readback and isolated traversal timing in 3.2/3.3 remain deferred.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 1.1 Document the implementation, corrected visibility/MIS contracts, and measured evidence below.
- [x] 1.2 Add nine LightBVH tests, including shared CPU/HLSL PDF conversion and complementary MIS energy conservation.
- [x] 1.3 Run build, layouts, render gates, and the full non-viewport gtests; all LightBVH tests pass with no new suite failures (the three existing VFX failures remain).

## Stage-1 verification, 2026-10-18: COMPLETE

Stage 1 is the opt-in Morton light hierarchy integrated into path-tracer NEE.
Its equal-sample rendered-mean gate now passes. The original ReSTIR, tighter-cone,
SAH, and isolated traversal-timing work is not included in this completion.

- Root cause: `pathtracer_closesthit.hlsl` offset the shadow origin to
  `P + 0.05*N` but retained direction and endpoint distance measured from `P`.
  For parallel receiver/emitter planes, the sampled emitter is intersected at
  `d - 0.05/Lz`, before the old `TMax = d - 0.1` whenever `Lz < 0.5`.
  The sampled light falsely occluded itself, removing grazing illumination.
  Visibility direction and distance now come from the actual offset origin
  to `chosen.samplePos`; the BRDF and Jacobian retain the shading vertex.
- MIS: secondary emission is no longer blanket-zeroed. Both techniques use
  `p_light = P(select triangle) * distanceSquared / (area * abs(cosEmitter))`
  at the previous shading vertex and complementary balance weights with
  `neeSamples * p_light`. The private payload carries the previous vertex,
  normal, and BSDF density; material/emitter records identify the exact draw
  and primitive. Shared CPU/HLSL math has a conservation regression test.
  MIS alone left a -16.4673% mean discrepancy; fixing visibility eliminated it.
- Scene: `scripts/rendering/light-bvh-emissive.uzproject` and `.uzscene`;
  nine emissive mesh panels, 108 triangles, `r.TonemapOperator=0`, bloom off.
  Final captures use `--path-trace`, `r.LightBVH=1` versus `0`,
  `r.PathTracer.MaxBounces=2`, four NEE samples, roulette off, and identical
  2060-frame budgets with screenshots at both frame 210 and frame 2050.
- GPU engagement: `E:/UzmiEngine/lightbvh-mis-proof/visible-on/run.log:514`
  records `LightBVH: Morton build 108 lights, 215 nodes, root power=663552`;
  line 518 records `DispatchRays 640x360`, 11 TLAS instances and shadow rays;
  line 559 reaches 200 accumulated samples. Neither final capture log contains
  `compile failed`; both engine runs exit 0.
- Unbiased-mean gate (whole-image RGB PNG means in linear passthrough display):
  frame 210 on/off = `0.04158375885076253 / 0.04140063316993464`,
  delta `+0.4423257975%`; frame 2050 =
  `0.04158531340777052 / 0.041588660811546836`,
  delta `-0.0080488376%`. Before the fix, the corresponding deltas were
  `-16.201% / -16.575%`. Both final frame-2050 PNGs were read: BVH-on is
  smoother, with restored peripheral/foreground illumination rather than
  systematic darkening.
- Retained noise win: frame-10 residual variance against each method's own
  frame-2050 image is `0.00017806692923729` on versus
  `0.020336453840647712` off, a `99.1243953807%` reduction.
- Render evidence: `E:/UzmiEngine/lightbvh-mis-proof/visible-{on,off}/`
  contains logs, manifests, and PNGs; `metrics-final.json` records means and
  variance. `prove.py` reproduces captures, metrics, and the default gate.
- Default safety on the final binary: flag-free `reflection-reference`
  frame 95 is byte-identical to `E:/UzmiEngine/f005-after/frames/f_95.png`;
  `max_code_delta=0`, engine exit 0. Evidence:
  `E:/UzmiEngine/lightbvh-mis-proof/final-default/`.
- `cd runtime && zig build -Doptimize=ReleaseFast`: exit 0, 149 shader
  integrity checks. `npm run test:layouts`: exit 0, including the 100-byte
  private material stride and light-BVH structured buffers.
- Final `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast`:
  7853 tests, 7833 passed, 17 skipped, the same three pre-existing failures,
  exit 1. This adds one passing MIS regression to the supplied 7832-pass
  light-BVH WIP baseline (+9 versus the original 7824 baseline).
  All nine LightBVH tests pass. Two-light chi-square `0.000008333`,
  hierarchical chi-square `0.000186499`, estimator mean `5.519579143`
  versus target `5.519578771`, variance `0.008685727` versus uniform
  `27.023226314`. Evidence: `E:/UzmiEngine/lightbvh-mis-proof/gtests-final.log`.
  Unchanged failures: `VfxEditorSlate.WorkbenchHasTwoColumns`,
  `VfxEditorSlate.TitleBarReflectsEmitterName`,
  `VfxEditorSlate.StatusBarBindingsReflectTelemetry`. No TDR retry was needed.
- Build/capture/test invocations were bounded to 180 seconds. C++ LSP
  diagnostics were unavailable because clangd is not installed; build and
  runtime shader compilation supplied the compiler verification.
- Deferred: SAH construction/optimization, tighter normal cones,
  ReSTIR-BVH candidate generation/integration, GPU selection-histogram
  readback, and isolated traversal timing. No commit or archive was made.

## Stage-2 verification, 2026-10-18: COMPLETE

- [x] S2.1 Replace balanced Morton median splits with importance SAOH splits:
  exact prefix/suffix sweeps of centroid and normal axes minimize
  `power * AABB surface area * orientation cost` (Conty Estevez & Kulla 2018,
  Eq. 1; Falcor `LightBVHBuilder::evalSAOH`). Child capacity constrains depth
  to the existing 32-level GPU traversal budget. `r.LightBVH.Morton=1` retains
  the exact stage-1 build/full-sphere estimate as a diagnostic reference.
- [x] S2.2 Compute enclosing spherical normal cones by cap union, including
  containment and antipodal axes. The existing axis/cosine fields encode
  theta_o; theta_e is fixed at pi/2 for diffuse triangles, as in Falcor.
  Traversal subtracts cone spread and AABB subtended angle from the normal
  angle. The transport contract is two-sided, so it bounds both +/- normal
  cones using absolute dot; it must not cull the emitting back side.
  Positive support floors and unchanged branch-product PDFs preserve MIS.
  Directional/mixed nodes retain the conservative power-only sentinel.
  Node/light/config strides remain 64/112/16 bytes; no CB/layout changes.
- [x] S2.3 Still-unbiased rendered-mean gate on the 108-triangle scene:
  `E:/UzmiEngine/lightbvh-stage2-proof/{sah,morton,off}/` holds final PNGs,
  manifests, and `run.log`; all three captures exit 0 with identical
  2060-frame budgets, two bounces, four NEE samples, and roulette off.
  At frame 210 (about 200 accumulated samples), whole-image RGB PNG means
  are SAOH `0.041575486791938995` vs plain `0.04140063316993464`,
  delta `+0.4223452846%`. At frame 2050, means are
  `0.04158352056100218` vs `0.041588660811546836`,
  delta `-0.0123597405%`. Both methods' frame-210 and frame-2050 PNGs
  were read: illumination is restored across the floor/back wall, without
  systematic darkening. The larger plain-sampling noise converges away.
- [x] S2.4 Lower rendered variance than stage 1 at equal low sample count:
  frame-10 residual variance against each method's own frame-2050 image is
  SAOH `0.0001622469389302276` vs Morton `0.00017806692923728998`,
  delta `-8.8842944475%`. The Morton diagnostic reproduces the committed
  stage-1 mean and variance exactly. Both low-sample PNGs were read.
  `E:/UzmiEngine/lightbvh-stage2-proof/prove.py analyze` reproduces metrics.
  No additional GPU scene was needed. The pure CPU directional-cluster
  test additionally measures variance `0.014058907816` vs
  `0.0312519755837`, delta `-55.014339%`, with normalized PDFs and exact
  inverse-probability expectation.
- [x] S2.5 Build and shader engagement: `cd runtime &&
  zig build -Doptimize=ReleaseFast` exits 0, 152 shader integrity checks.
  `sah/run.log:518` records
  `LightBVH: SAOH/cone build 108 lights, 215 nodes, root power=663552`;
  line 520 records the uploaded stochastic GPU hierarchy, followed by
  `DispatchRays 640x360`, 11 TLAS instances. No stage-2 capture log contains
  `compile failed` or device-removal diagnostics. The unchanged pass logger
  also emits its old "Morton build" label; the new builder log identifies
  the actual selected algorithm.
- [x] S2.6 Opt-in/default safety: flag-free reflection-reference frame 95 is
  byte-identical to `E:/UzmiEngine/f005-after/frames/f_95.png`,
  `max_code_delta=0`, engine exit 0. Evidence:
  `E:/UzmiEngine/lightbvh-stage2-proof/default/`.
  The base BRDF (D7) and VFX editor WIP are untouched.
- [x] S2.7 Tests: `npm run test:layouts` exits 0. All 14 LightBVH tests
  pass (nine existing plus five stage-2 tests):
  `SAOHCostIsMonotoneInPowerAreaAndSpread`,
  `ConeUnionBoundsBothChildrenIncludingAntipodesAndContainment`,
  `SAOHSeparatesKnownSpatialClusters`,
  `TwoSidedConeDownweightsEdgeOnNotBackFacingAndKeepsSupport`, and
  `DirectionalClusterEstimatorIsUnbiasedAndBeatsMortonVariance`.
  The existing independent PDF oracle now includes the new orientation
  estimate; its normalization, histogram, MIS, and estimator checks pass.
  `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast`
  runs 7887 tests: 7867 pass (+5 versus supplied 7862 baseline), 17 skip,
  and the same three pre-existing failures:
  `VfxEditorSlate.WorkbenchHasTwoColumns`,
  `VfxEditorSlate.TitleBarReflectsEmitterName`,
  `VfxEditorSlate.StatusBarBindingsReflectTelemetry`.
  Exit 1 solely for those failures; test execution 40.563 seconds.
  No TDR retry was needed. clangd is unavailable; the actual build and
  runtime shader compiler supply compiler validation.
- [x] S2.8 Engine/build/test commands are bounded to 180 seconds; final
  process enumeration finds no engine/test/zig processes. No commit or
  archive was made.

Deferred after stage 2: ReSTIR-BVH candidate generation/integration,
GPU selection-histogram readback, and isolated traversal timing.

## Stage 3: DXR ReSTIR / light-BVH integration (2026-10-19): OCCLUSION GATE CLOSED

- [x] DXR ReSTIR-BVH occlusion acceptance. Visibility-aware reuse passes
  the 0.5% converged-mean tolerance on both scenes and lowers occlusion
  variance against **both** BVH-only and uniform ReSTIR. At 5 / 10 / 20
  frames the BVH-only deltas are now **-46.37721% / -43.02730% /
  -37.69728%**, reversing the earlier unshadowed losses.
  See the visibility-aware follow-up below; earlier measurements are
  historical. This is the static-camera equal-low-sample gate, not an
  equal-GPU-cost or moving-camera claim.
  The expanded shader-define/histogram and demo items above are not claimed
  complete.

The committed initial shader already traversed `SampleLightBVH` and used
`target / (selectionPdf / triangleArea)`. This increment shares that exact
weight with a pure gtest, documents its area measure, identifies the combined
dispatch, and stratifies the 32 selection variates only for BVH importance
traversal. The equally weighted stratum mixture has the original BVH density.
The existing descriptor gate enables this only with both ReSTIR and
`r.LightBVH`; BVH-off keeps the committed stage-2 generator. Reservoir/root/
material contracts and the base BRDF are unchanged.

Detailed methodology, the complete seven-file change list, and acceptance
evidence are in the stage-3 section of
`../phase1_falcor-restir-di/tasks.md`. Evidence root:
`E:/UzmiEngine/restir-bvh-proof/`; replay metrics with
`prove.py analyze restored nee-uniform restored`.

- 108 lights / 215 nodes, two bounces, one NEE shadow sample/frame,
  TAA/roulette off. The ordinary NEE reference uses uniform emitter
  selection without importance traversal, ReSTIR off, 32 NEE samples/frame,
  frame 2050. Combined/reference whole-image RGB-code mean delta:
  **+0.02555%**. Reference-only interior RGB-code/display-linear mean deltas:
  **+0.07340% / +0.41005%**, within the stage-2 0.5% tolerance.
- At 5 samples, combined/uniform-ReSTIR/BVH-only error variance is
  **421.160413 / 1697.648757 / 420.023284**: **-75.19155%** versus
  uniform ReSTIR but **+0.27073%** versus BVH-only. At 10/20 samples,
  versus uniform ReSTIR is **-83.99920% / -83.53551%**, while versus
  BVH-only is **+13.26194% / +41.03204%**. Whole-image and
  own-converged-reference measurements also fail the BVH-only gate.
  Independent, selection-stratified, and additionally area-stratified
  candidates were measured; the best selection-stratified version is retained.
- BVH-off versus the pre-edit committed build: mean/max code delta **0**,
  PNG-byte-identical at frames **5, 10, 20, 200**. Flag-free default frame 95:
  PNG-byte-identical to `E:/UzmiEngine/f005-after/frames/f_95.png`, max **0**.
- Integrated `run.log` identifies BVH-sourced RIS plus temporal/spatial
  dispatch; no proof log has `compile failed` or device-removal markers.
- Pure source-density gtest: exact integral **41**, biased missing-PDF
  control **5.3**, exact stratified-mixture integral **41**. It passes.
  Build exit **0** (152 shaders); layouts exit **0** (89/89, 33/33, 12/12).
  Non-viewport gtests: **7,889 pass (+1)**, 17 skip, only the same three
  pre-existing `VfxEditorSlate` failures, exit **1**. No TDR retry.
  Builds/tests are bounded to 175 seconds and captures to 170 seconds.
  Final exact-name process enumeration found no `uzengine.exe`,
  `test_runtime.exe`, or `zig.exe` processes. No commit or archive was made;
  unrelated WIP and scratch files are untouched.

### Occlusion follow-up, 2026-10-19: gate honestly remains open

`scripts/rendering/restir-occlusion.uzproject` / `.uzscene` places the 108
emissive triangles above four opaque overhead cell dividers, with the camera
below them. Exact segment/AABB intersection blocks **3,008 / 3,969 (75.78735%)**
emitter-center rays from a 441-point floor grid. This is a geometric diagnostic,
not a GPU visibility histogram.

All five serialized captures in `E:/UzmiEngine/restir-occlusion-proof-v2/`
exit **0**, without `compile failed` or device-removal markers.
`measure_restir_occlusion.py analyze OUTPUT` reproduces `metrics.json`.
Detailed capture commands, methodology, complete five-file change list,
and caveats are in the matching occlusion subsection of
`../phase1_falcor-restir-di/tasks.md`.

- Frame-2050 combined/plain-NEE interior RGB-code means:
  **71.513817 / 71.575993**, **-0.08687%**; inverse-sRGB display-linear:
  **0.086325471 / 0.086513478**, **-0.21732%**. Whole-image display-linear:
  **0.001195433 / 0.001194264**, **+0.09781%**. Linear checks pass the
  existing 0.5% mean tolerance; whole-image RGB-code delta **+0.65915%**
  does not. PNGs were decoded and visually read; no exact finite-sample
  or raw-HDR equality is asserted.
- Equal-low-sample spatial RGB-code error variance on 2,316 reference-only
  interior pixels (one NEE shadow sample/frame, same two-bounce camera):

  | Samples | ReSTIR x BVH | BVH only | Uniform ReSTIR | vs BVH | vs ReSTIR |
  | --- | ---: | ---: | ---: | ---: | ---: |
  | 5 | 1878.585387 | 1790.591979 | 4220.197851 | +4.91421% | -55.48585% |
  | 10 | 1144.353998 | 876.718699 | 5042.886549 | +30.52693% | -77.30756% |
  | 20 | 788.021408 | 445.431933 | 3396.884011 | +76.91175% | -76.80164% |

  Whole-image variance also loses to BVH-only. Uniform ReSTIR retains emitter
  enumeration with `r.LightBVH.Uniform=1`, without importance traversal;
  plain-NEE reference disables ReSTIR and uses 32 uniform NEE samples/frame.
- Cause: all reservoir targets and reuse are **unshadowed**; final DXR
  visibility happens only after selection and is not fed back into history.
  This implementation cannot reuse known-visible samples. Static coherence
  does not repair that; camera/object motion invalidates history. No M/radius
  tuning, new visibility mechanism, or moving-camera claim was added.
  **Stage-3 remains [ ]**, not closed by the uniform-ReSTIR improvement.
- Build **exit 0**, 152 shaders; layouts **exit 0**, 89/89 + 33/33 + 12/12.
  Non-viewport gtests: **7,890 pass**, 17 skip, only the three pre-existing
  `VfxEditorSlate` failures, **exit 1**; 43.817 seconds, no TDR retry needed.
  Logs: `E:/UzmiEngine/restir-occlusion-{build,layouts,gtests}.log`.
- Flag-free default frame 95 is PNG-byte-identical to
  `E:/UzmiEngine/f005-after/frames/f_95.png`, **max_code_delta 0**.
  Captures bounded to 170 seconds, build/tests to 175 seconds. Final cleanup
  verifies no engine/test/zig processes. The base BRDF, integration files,
  unrelated WIP and scratch files are untouched. No commit or archive.

### Visibility-aware follow-up, 2026-10-19: occlusion gate closed

The existing uncommitted reservoir implementation was retained and proved.
Initial selected-sample visibility rejection keeps `E[V W g(Y)] =
integral_visible g`, without renormalization. Temporal/spatial reuse uses
`t_i = V_i p_hat_i` in every source/receiving target and MIS denominator:
`m_i = M_i t_i / sum_j M_j t_j`, streamed weight
`t_here(Y_i) W_i m_i(Y_i)`, final `W = sum(w) / t_here(Y)`.
Conditional selection cancels the receiving target; source expectations
sum to the visible integral because the weights partition unity on its
support. Null sources retain M/domains; cross-domain visibility is traced
at that domain, never borrowed from the neighbor's cached bit.

The reservoir is **116 bytes**, visibility offset **112**; allocation,
readback, CPU/HLSL declarations, and reflected layout agree. The existing
TLAS table is bound for compute shadow queries. **PipelinePathRoot remains
24 DWORDs**; default rendering and the base BRDF are untouched.

Seven serialized captures in `E:/UzmiEngine/restir-visible-proof/` exit
**0**, with no `compile failed` or device-removal markers. `verify.py analyze`
reproduces `metrics.json`; the committed occlusion measurement script remains
unchanged. Both scenes use 640x360, two bounces, roulette/TAA off. The
ordinary uniform-emitter NEE reference disables ReSTIR and uses 32 NEE
samples/frame through frame 2050. PNGs were decoded and visually read.

Combined / plain-NEE frame-2050 interior display-linear means:

- Occlusion: **0.086832760 / 0.086513478**, **+0.36905%**;
  RGB-code **71.696891 / 71.575993**, **+0.16891%** (2,316 pixels).
- 108-light emissive: **0.028176307 / 0.028157012**, **+0.06853%**;
  RGB-code **38.165931 / 38.150637**, **+0.04009%** (37,618 pixels).
- Whole-image display-linear deltas: **+0.30470% / +0.03359%**.
  All combined mean checks pass the existing **0.5%** tolerance. Finite
  inverse-sRGB PNG means corroborate the invariant, not exact finite-sample
  or raw-HDR equality.

At **75.78735%** geometric occlusion, equal-low-frame RGB-code error variance:

| Frames | ReSTIR x BVH | BVH only | Uniform ReSTIR | vs BVH | vs ReSTIR |
| --- | ---: | ---: | ---: | ---: | ---: |
| 5 | 960.165335 | 1790.591979 | 4066.222807 | -46.37721% | -76.38680% |
| 10 | 499.490358 | 876.718699 | 4927.537876 | -43.02730% | -89.86329% |
| 20 | 277.516212 | 445.431933 | 3294.179770 | -37.69728% | -91.57556% |

Whole-image variance also wins. The 50/200-frame BVH-only deltas are
**-30.61583% / -11.48953%**. Uniform ReSTIR keeps emissive enumeration but
disables importance traversal (`r.LightBVH.Uniform=1`), and receives the
same visibility correction in this comparison. Its frame-2050 interior
linear delta remains +1.33599%; no mean-gate pass is claimed for that noisy
comparator. Combined also beats the historical pre-fix plain-ReSTIR
variances. This is spatial error variance, not ensemble variance or equal
GPU cost; the extra visibility rays are not free.

Build **exit 0** (152 shaders); layouts **exit 0** (89/89 + 33/33 + 12/12).
Both new visibility gtests pass: differing-support basis expectations
**1, 0, 1**, inconsistent-denominator control **1/172** instead of 1;
pre-fix/fixed mean **1** with variance **>6 / 2**. Default frame 95 is
PNG-byte-identical to `f005-after/frames/f_95.png`, **max_code_delta 0**.
Logs are `E:/UzmiEngine/restir-visible-{build,layouts,gtests}.log`.
The full requested non-viewport gtest run executes **7,912 tests**:
**7,892 pass (+2)**, **17 skip**, and only the three pre-existing failures
`VfxEditorSlate.WorkbenchHasTwoColumns`,
`VfxEditorSlate.TitleBarReflectsEmitterName`, and
`VfxEditorSlate.StatusBarBindingsReflectTelemetry`. **Exit 1** reflects
only those failures; execution **43.755 seconds**. No TDR or retry occurred.
Captures are bounded to 170 seconds and build/tests to 175 seconds.
Final exact-name enumeration found no engine/test/Zig processes.
No commit or archive was made.

The complete 13-file scoped change list, detailed estimator argument,
mask definition, all measurements, and verification are in the matching
visibility-aware subsection of `../phase1_falcor-restir-di/tasks.md`.
