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
