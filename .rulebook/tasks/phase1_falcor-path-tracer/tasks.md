## Session 1: Ray Generation Shader

- [x] 1.1 Create `pathtracer_raygen.hlsl` — entry point `main` (`[shader("raygeneration")]`). Compute ray origin and direction from camera matrices in `PerFrameCB`. Apply Halton sequence jitter for anti-aliasing (sample index from accumulation frame count, dimensions 2+3).
- [x] 1.2 Define `PathPayload` struct: `float3 radiance`, `float3 throughput`, `uint bounceCount`, `bool terminated`. Define `ShadowPayload`: `bool hit`. Add to shared `pathtracer_shared.hlsli`.
- [x] 1.3 `TraceRay` call in raygen: TLAS SRV bound at `t0`. Output: write `PathPayload.radiance` to `RWTexture2D<float4> uRadiance`. Single bounce in this session (miss + closest-hit only, no recursion yet).
- [x] 1.4 Build + verify: raygen shader compiles with DXC `lib_6_6` profile. RT pipeline creates without error.

## Session 2: Closest-Hit + Miss Shaders

- [x] 2.1 Create `pathtracer_miss.hlsl` — entry `main` (`[shader("miss")]`): sample `TextureCube<float4> tEnvMap` in the ray direction, write to `PathPayload.radiance`, set `terminated = true`
- [x] 2.2 Create `pathtracer_closesthit.hlsl` — entry `main` (`[shader("closesthit")]`): read vertex attributes (position, normal, UV) from hit triangle via `BuiltInTriangleIntersectionAttributes`. Load material from instance's `materialId` via `StructuredBuffer<MaterialData>`.
- [x] 2.3 Direct lighting in closest-hit: for each directional light, cast shadow ray (`TraceRay` with `ACCEPT_FIRST_HIT_AND_END_SEARCH` flag and `ShadowPayload`). If not shadowed, evaluate `EvaluatePBR` from `brdf.hlsli`. Add to `PathPayload.radiance`.
- [x] 2.4 BRDF sampling for next bounce: call `SampleGGX(N, V, roughness, metallic, u)` from `brdf.hlsli` to generate next-bounce direction. Write to `PathPayload.throughput *= brdfWeight / pdf`. Set `bounceCount++`.
- [x] 2.5 Build + verify (stage-1 scope explicitly set by the current request): render a directional-light scene with a single surface hit, direct lighting and environment on miss. Use `run_capture` on reflection-reference frame 95 with `extra_args=["--path-trace"]` and without; verify lit surfaces, nonblack output, a nonzero versus-default delta, and a byte-identical default baseline. The original metallic-reflection/soft-shadow-quality demonstration is deferred with 3.x/4.x, not claimed by this checkbox.

### Stage-1 hardware evidence (2026-10-17)

The requested single-surface-bounce runtime gate passes on the RTX 5080.
Activation was not suppressed by project/scene override ordering. Neither the
project's ten CVars nor the scene's eight CVars names `r.RayTracing` or
`r.PathTracer.Pipeline`. Project defaults use
`Config::AddPendingCvarOverrideDefault` (front insertion, config.h:154), so
explicit CLI values apply last. The scene loads at tools/uzengine.cpp:858,
before the CLI flush at line 934. The standalone runtime's generic Config parser
stored `--path-trace` as a boolean but only `--cvar` queued CVar overrides;
main had no translation of this flag into the RT gates. `Application::ParseArgs`
is a different entry point. The new main handler now queues both gates before
renderer initialization, at the same layer as the existing `--rt-test` handler.

The existing pass creates its private DXR signature (six root DWORDs, no shared
signature change), reuses the scene TLAS, dispatches rays, and copies its radiance
UAV into the HDR scene color consumed by postprocessing. The opt-in branch now
updates atmosphere/environment before this resolve so raster sky cannot replace
traced miss pixels afterward. Missing roughness/metallic textures use scalar
material factors directly, matching gbuffer_ps rather than treating every
textureless surface as a diffuse dielectric.

- `cd runtime && zig build -Doptimize=ReleaseFast`: exit 0; 147 embedded shaders verified.
- `E:/UzmiEngine/pt-stage1-verified-trace/run.log`: all three libraries compiled `lib_6_6`;
  `RT-PSO CreateStateObject succeeded` at line 425; `DispatchRays 1920x1080,
  5 TLAS instances ... 1 directional lights, env SRV=24669` at line 539.
  Neither traced nor default capture logs contain `compile failed`.
- Both `run_capture` invocations requested 100 frames, captured frame 95, and exited 0
  (the manifests include four additional preload frames).
  Only the traced invocation had `extra_args=["--path-trace"]`.
  `pt-stage1-verified-trace/frames/f_95.png` versus
  `pt-stage1-verified-default/frames/f_95.png`:
  maximum RGB code delta 251, mean absolute RGB delta 56.843917, all 2,073,600
  pixels differ. The traced image has 915,813 nonblack pixels.
- An independent third `run_capture` uses explicit `--cvar r.RayTracing=1`
  and `--cvar r.PathTracer.Pipeline=1`, without `--path-trace`.
  `pt-stage1-verified-cvars/frames/f_95.png` is PNG byte-identical to the flag
  capture (maximum code delta 0). Both manifests record gates 1/1; the default
  records 0/0. This proves both the pass gate read and CLI override survival.
- Read both final PNGs: the traced image shows a bright white sunlit pillar face,
  blue/orange sunlit block tops and a blue-to-light horizon environment on miss.
  The smooth metallic floor and unlit faces are black without the deferred
  indirect surface bounce; the floor is no longer incorrectly shaded as diffuse
  white. The default retains its blue reflective floor. Environment/inter-object
  metallic reflections and soft-shadow image quality are NOT claimed.
- Default versus `E:/UzmiEngine/f005-after/frames/f_95.png`: maximum code delta 0;
  PNG files also byte-identical.
- `npm run test:layouts`: exit 0; 167 declarations, 106 mapped, 61 explicit exclusions,
  no unmapped declarations. Includes the path-tracer PerFrameCB prefix, six-DWORD
  root block, 96-byte material stride and 32-byte directional-light stride.
- `PipelinePathTracerStage1` tests: `MaterialStructuredBufferLayout`,
  `MaterialDefaultsDoNotSampleMissingDescriptors`, `PipelineBudgetIncludesNestedShadowRay`,
  `DirectionalLightStructuredBufferLayout`: all four pass.
- Required full gtest command (`GTEST_FILTER='-RuntimeViewportTest.*' zig build test
  -Doptimize=ReleaseFast`): 7,811 pass, 17 skip, three pre-existing VfxEditorSlate
  failures; pass count is baseline 7,807 plus these four tests.
  The requested single rerun reproduced exactly 7,811 pass, 17 skip and the same
  failures (`WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`,
  `StatusBarBindingsReflectTelemetry`); no TDR occurred in either run.
  Logs: `E:/UzmiEngine/pt-stage1-verified-gtests.log` and
  `E:/UzmiEngine/pt-stage1-verified-gtests-rerun.log`. Both commands exit 1 solely for
  those existing failures. LSP diagnostics were unavailable because clangd is
  not installed; the actual compiler build passes. No engine, test_runtime or
  zig processes remained after verification.

Item 2.5 above records the current request's explicit stage-1 verification scope.
Its original broader metallic-reflection/soft-shadow-quality demonstration remains
deferred: metallic environment/inter-object reflections need the next-surface
bounce, and soft-shadow quality is not established by this small-source-angle
reference capture. No reflection or convergence claim is inferred from a
nonblack/different image. Halton uses frame index until 3.x adds an accumulation
frame count. Sessions 3.x, 4.x and documentation were deferred at stage 1;
the stage-2 implementation and remaining gates are recorded below.

## Session 3: Accumulation Pass

- [x] 3.1 Persistent float32 accumulated mean and Kahan correction UAVs. The running-average resolve is fused into the existing raygen, rather than adding a separate accumulation CS/PSO and an intermediate sample texture. `pathtracer_math.hlsli::PathAccumulate` is shared with the CPU tests. The existing half-float radiance output now contains the accumulated mean. Evidence: convergence table and running-mean/compensation tests below.
- [x] 3.2 Reset on any exact unjittered view/projection change, scene dirty signal, material/transform/light/environment input change, resize, skipped frame, settings change or explicit reset generation. The first sample overwrites both UAVs without reading old history, equivalent to clearing history before the new mean; no stale/NaN values survive. The generic camera dirty bit is excluded because the renderer sets it unconditionally for timeParams; actual camera changes are detected from the unjittered matrix. View, FOV and scene-change GPU reset evidence is below.
- [x] 3.3 Extend the existing `PipelinePathTracerPass` to own the two render-resolution UAVs, sample counter and input signature. Import both resources through tracked render-graph state, including UAV memory ordering. The existing HDR resolve copies the displayed accumulated mean; there is no second path-tracer class or shared PerFrameCB change. Evidence: successful build, 200-sample dispatch log, default byte-match and private-root layout check below.
- [x] 3.4 Current stage-2 acceptance: capture a static progressive sequence and demonstrate declining inter-frame deltas, plus camera/projection/scene reset. The 210-frame invocation captures frames 5-200 and reaches 200 accumulated samples. RGB inter-frame MAE falls from 0.1781565 (5/6) to 0.0095210 (199/200); MSE falls from 0.8294210 to 0.0098955. The requested FLIP route was attempted but is NOT accepted as evidence: it reported zero for demonstrably different images and did not finish promptly. This checkbox records the current request's explicitly permitted inter-frame-delta convergence proof, not a valid FLIP/ground-truth quality comparison.

## Session 4: Multi-Bounce + NEE + Demo

- [x] 4.1 Iterative surface `TraceRay` loop in raygen, as permitted by the stage-2 request. Default 4 surface vertices, configurable 1-16 through the existing `r.PathTracer.MaxBounces` CVar and pass-private root constants, not a shared PerFrameCB field. The 56-byte payload returns the next origin/direction; DXR recursion depth remains 2 for nested shadow rays. GGX VNDF/cosine diffuse mixture sampling evaluates the combined PDF and reuses engine BRDF/sampling functions. Evidence: one/four-bounce captures, payload/bounce-budget tests and dispatch log below.
- [x] 4.2 Russian roulette uses `clamp(max(throughput), 0.05, 1)` with reciprocal survival weighting. Reuse `r.PathTracer.RussianRoulette` (default true) and `r.PathTracer.RussianRouletteMinBounce` (default 2); both are included in accumulation invalidation. Evidence: shared scalar survival/unbiasedness test and logged RR settings below.
- [x] 4.3 NEE executes at every surface vertex, with four visibility samples per scene directional light, accumulated through current path throughput. Scene directional radiance is integrated source intensity, so normalized source/visibility sampling carries a 1/4 sample weight; no MIS is applied to these directional lights. Evidence: closest-hit code, lit indirect surfaces and successful multi-bounce/shadow dispatch below.
- [x] 4.4 Preserve stage-1 `--path-trace` activation and HDR pass selection; its output now resolves the persistent accumulated mean. Add `--pt-bounces <N>` before CVar flush and consume the existing bounce/RR CVars. Evidence: manifests and logs record bounces 1 versus 4; the no-flag raster capture is PNG-byte-identical to frame 95 of the required baseline.
- [x] 4.5 Cornell red/green indirect-light demo passes the current acceptance gate after independently confirming and correcting the shared TLAS affine conversion. Frame 200 shows separate walls and both colored indirect tints on white surfaces; matched one-bounce controls are black in those same regions. Quantified hardware evidence is below. This is the requested geometry/color-bleed gate, not an independent FLIP/ground-truth accuracy claim.

### Stage-2 implementation and hardware evidence (2026-10-17)

Only the opt-in pipeline pass owns the new resources. Its root CB grows from
24 to 48 bytes (6 to 12 DWORDs); the private root signature derives its size from
`PipelinePathRoot`. Consumers are `pipeline_path_tracer_pass.cpp`,
`pathtracer_shared.hlsli`, and the raygen/closest-hit/miss libraries including
that header. The miss source itself is unchanged. The shared graphics signature
and all PerFrameCB declarations stay unchanged. The existing Tier B layout entry
is annotated for the 12-DWORD contract and reflects the actual C++/DXC size.

`pathtracer_math.hlsli` contains the production scalar compensated-mean,
roulette-survival and surface-budget functions compiled by HLSL and included
by the C++ regression tests. Diffuse directions use the engine cosine sampler;
specular directions use GGX VNDF. Both BRDF densities are evaluated for the
mixture weight. Direct-light samples use bounce-specific dimensions, as do
BRDF and roulette draws. Halton's sample index advances with accumulated samples.
No recursive surface rays or extra global signature slots were introduced.

#### Build and runtime

- `cd runtime && zig build -Doptimize=ReleaseFast`: exit 0.
  `E:/UzmiEngine/pt-stage2-build.log` records 147 embedded shaders verified.
- `npm run test:layouts`: exit 0, 167 declarations, 106 mapped, 61 explicit
  exclusions; Tier A 88/88, Tier B 28/28, Tier C 6/6. The path root is DXC 48
  bytes / 12 DWORDs, C++ sizeof 48. Log:
  `E:/UzmiEngine/pt-stage2-layouts-final.log`.
- `E:/UzmiEngine/pt-stage2-hidden-trace/run.log`: three `lib_6_6` libraries
  compile at lines 424-426, `RT-PSO CreateStateObject succeeded` at line 427,
  `DispatchRays 1920x1080, 5 TLAS instances, 4 surface bounces + shadow rays`
  at line 544, and `accumulation samples=200, maxBounces=4, RR=1 from 2`
  at line 750. No `compile failed` occurs in the accepted trace, single-bounce,
  default or reset capture logs.
- Captures use the repository's `flicker.runner.run_capture`, `timeout=180`,
  and finite frame counts. The convergence run requests 210 frames and renders
  214 including four preload frames. The one-bounce and default runs request
  100 and render 104. All accepted capture commands exit 0.
- Hidden HWNDs avoid desktop resize interference during long capture sequences.
  An initial visible trace was resized by the desktop and removed the device;
  it is not evidence. A hidden default launch also differs slightly from the
  visible baseline (maximum delta 14), so the accepted default verification
  reproduces the original visible-window launch flags, not `--hidden`.

#### Progressive convergence (RGB code units, same process/history)

Capture directory: `E:/UzmiEngine/pt-stage2-hidden-trace/frames`.
Full metrics: `E:/UzmiEngine/pt-stage2-evidence/convergence.json`.

| Frame pair | Mean absolute RGB delta | Mean squared RGB delta |
|---|---:|---:|
| 5 / 6 | 0.1781565 | 0.8294210 |
| 15 / 16 | 0.0614344 | 0.1022925 |
| 31 / 32 | 0.0389598 | 0.0423815 |
| 63 / 64 | 0.0204688 | 0.0218605 |
| 95 / 96 | 0.0154786 | 0.0164254 |
| 127 / 128 | 0.0128892 | 0.0135955 |
| 199 / 200 | 0.0095210 | 0.0098955 |

The early-to-late inter-frame MAE drops 18.7 times and MSE 83.8 times.
Against the 200-frame mean (a convergence reference, NOT independent ground
truth), frame 5 MSE is 40.5891398 versus 1.2368104 at frame 95.
Read the images: the noisy pillar/block faces become smoother, while the
metallic floor retains the environment and colored-object reflections.

The engine's `--flip-ref/--flip-test` mode was attempted on frames 200 and 5.
It printed `mean_flip=0,max_flip=0` despite the measured/visible difference,
then failed to finish promptly and was terminated. No FLIP pass or accuracy
claim is based on that result. Log:
`E:/UzmiEngine/pt-stage2-evidence/flip-early.log`.

#### Indirect-lighting control

Compare frame 95 in `pt-stage2-hidden-single` (`--pt-bounces 1`) versus
`pt-stage2-hidden-trace` (`--pt-bounces 4`):

- maximum RGB code delta 255; full-image MAE 49.3723720;
  1,172,994 pixels differ.
- Floor ROI `[200,650) - [1700,1000)` contains 525,000 pixels, all differing.
  Its one-bounce mean RGB is `[0,0,0]`; four-bounce mean is
  `[42.0785829,77.4889790,116.0258381]`, MAE 78.5311333.
- The one-bounce metallic floor/unlit faces are black. Four bounces show a
  blue sky reflection, the white pillar's reflected streak, colored block
  reflections, and blue/green/red indirect illumination on formerly unlit faces.
  This is a visible indirect transport proof, not merely a nonblack frame.

Metrics: `E:/UzmiEngine/pt-stage2-evidence/bounces.json`.
The shipped Cornell one/four-bounce captures also differ (max 249, MAE
69.0872590) and show green bounce light, but their wrong geometry invalidates
item 4.5, as described above. Captures are in
`pt-stage2-hidden-cornell`, `pt-stage2-hidden-cornell-single` and
`pt-stage2-hidden-cornell-default`; these are diagnostic artifacts only.

#### Reset and default safety

- `E:/UzmiEngine/pt-stage2-hidden-reset/run.log`: camera position/target change
  at frame 44 resets 43 accumulated samples to 1 (lines 513-515); FOV change at
  frame 64 resets 20 to 1 (517-519); scene transform change at frame 84 resets
  20 to 1 (521-525). Each stationary segment resumes accumulation to at least
  16 samples. The command renders 124 frames including preload and exits 0.
  The verification project/scene/script are outside the repository under
  `E:/UzmiEngine/pt-stage2-evidence/`.
- `E:/UzmiEngine/pt-stage2-baseline-default/frames/f_95.png` versus
  `E:/UzmiEngine/f005-after/frames/f_95.png`: maximum code delta 0,
  MAE 0, zero differing pixels, PNG bytes identical. Neither `--path-trace`
  nor bounce overrides are passed. Metrics:
  `E:/UzmiEngine/pt-stage2-evidence/default-baseline-flags.json`.

#### Regression tests and remaining work

The required `GTEST_FILTER='-RuntimeViewportTest.*' zig build test
-Doptimize=ReleaseFast` completes in the 180-second bound. It runs 7,836 tests:
7,816 pass (stage-1 baseline 7,811 + five new tests), 17 skip, and the exact
same three pre-existing VfxEditorSlate failures remain:
`WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`,
`StatusBarBindingsReflectTelemetry`. Command exit 1 is solely those failures;
there was no TDR. Log: `E:/UzmiEngine/pt-stage2-gtests.log`.

All five `PipelinePathTracerStage2` tests pass:

- `PrivateRootContainsHistoryAndTransportSettings`
- `RunningMeanAndResetIgnoreOldHistory`
- `CompensatedMeanRetainsSmallUpdates`
- `RouletteSurvivalIsBoundedAndUnbiased`
- `BounceBudgetStopsAtConfiguredSurfaceCount`

The four stage-1 tests also remain passing; their payload-size assertion now
accounts for the next-ray origin/direction. Clangd is not installed, so LSP
diagnostics are unavailable; the actual C++ build, DXC libraries and tests ran.
Final process cleanup exits 0 and reports `NO_ENGINE_TEST_ZIG_PROCESSES`;
no uzengine, test_runtime or zig process remains. No commit or archive was
created. At the end of that stage, item 4.5 and the documentation tail remained
open. The follow-up below supersedes that status using the current request's
explicit Cornell geometry/color-bleed acceptance gate.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 1.1 Document stage-1/stage-2 implementation, CLI activation, private RT PSO/signature, shared TLAS, history/reset, multi-bounce/NEE/RR and actual CVar scope in `docs/specs/rendering/pipeline-path-tracer.md`.
- [x] 1.2 Existing nine pipeline tests cover stages 1/2. Add two `TLASInstanceTransform` CPU tests exercising the production conversion: nonzero translation column and rotated/nonuniformly scaled world points.
- [x] 1.3 Layout tests pass; the required full gtest run has 7,818 pass (baseline 7,816 plus two TLAS tests), 17 skip, and only the same three pre-existing VfxEditorSlate failures. Both new tests and all nine pipeline tests pass; the full suite is not claimed all-green.

### Shared-TLAS follow-up and Cornell acceptance (2026-10-17)

The convention bug is real, independently checked before editing. `Mat4` stores
`XMFLOAT4X4` row-major; `Mat4::Translation` delegates to `XMMatrixTranslation`,
`getTranslation()` returns `_41/_42/_43`, and point multiplication uses
`XMVector3Transform`. DXR's instance descriptor is three row-major float4 rows
acting on column vectors, so its translation is `[0][3]/[1][3]/[2][3]`.
The writer now transposes the complete affine transform, not only translation:
leaving the old 3x3 basis would still rotate instances incorrectly.
The same small production helper is called by both CPU regression tests.

`cd runtime && zig build -Doptimize=ReleaseFast` exits 0, with 147 embedded
shaders verified (`E:/UzmiEngine/pt-tlas-build.log`). The Cornell render log
records successful RT-PSO creation, three `lib_6_6` libraries, six TLAS instances,
four surface bounces, two directional lights, and 200 accumulated samples.
No `compile failed` occurs in the accepted Cornell captures.

The shipped `sample/projects/cornell_box.uzproject` and its setup script are
used unchanged. The reproducible camera override is
`--camera-pos 0,-700,250 --camera-look 0,100,250`; hidden HWND captures avoid
desktop resizing. Both runs request 210 frames, screenshot frame 200, a
180-second timeout, `r.AutoExposure=0`, `r.Exposure=1`, `--path-trace` and
`--pt-bounces 4` or `1`. Both exit 0. Accepted captures:

- `E:/UzmiEngine/pt-tlas-cornell-v2-4/frames/f_200.png`
- `E:/UzmiEngine/pt-tlas-cornell-v2-1/frames/f_200.png`

Both PNGs were read. The four-bounce image has distinct side/back/ceiling walls
and the translated sphere; the one-bounce room interior is black except for a
thin directly lit edge. This camera shows the green wall on screen-left and
red on screen-right. Half-open pixel regions on the WHITE back wall:

| Region (x0,y0,x1,y1) | Four-bounce mean RGB, 8-bit codes | One-bounce mean RGB |
| --- | --- | --- |
| Green-adjacent (835,405,900,520) | 201.318, 208.315, 193.881 | 0, 0, 0 |
| Red-adjacent (1020,405,1085,520) | 203.658, 190.575, 182.967 | 0, 0, 0 |

Green exceeds red by 6.997 codes beside the green wall; red exceeds green by
13.083 codes beside the red wall. The nearby white ceiling also carries both
tints: green-side (720,60,800,160) is (220.978,223.891,215.757), red-side
(1120,60,1200,160) is (223.477,217.218,213.224); both one-bounce regions are zero.
These are indirect-light measurements on white materials, not colored-wall
pixels or an independent reference-renderer accuracy score. No FLIP pass is
claimed; the earlier broken FLIP experiment is not reused.

`npm run test:layouts` exits 0: 167 declarations, 106 mapped, 61 explicit
exclusions; Tier A 88/88, B 28/28, C 6/6
(`E:/UzmiEngine/pt-tlas-layouts.log`). The required
`GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast`
runs 7,838 tests in 40.417 seconds: 7,818 pass, 17 skip, and exactly
`VfxEditorSlate.WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`,
`StatusBarBindingsReflectTelemetry` fail as before. Exit 1 is solely these
existing failures (`E:/UzmiEngine/pt-tlas-gtests.log`); no TDR occurred.
Clangd is unavailable, so compiler/tests supply the C++ verification.

#### Raster safety and shared consumers

Default reflection-reference frame 95
(`E:/UzmiEngine/pt-tlas-default/frames/f_95.png`) versus the required
`E:/UzmiEngine/f005-after/frames/f_95.png` has maximum RGB code delta 0,
MAE 0, zero differing pixels and identical PNG bytes. This uses the original
visible-window flags, without path tracing, and exits 0.

The shared-TLAS runtime consumers checked are:

- `PipelinePathTracerPass` / DXR closest-hit: the Cornell one/four-bounce
  captures above and the headful GBV reference below.
- Legacy inline-RayQuery `PathTracer` (`r.PathTracer.Enable=1`,
  `r.RayTracing=1`): framed Cornell before/after frame 95 captures in
  `E:/UzmiEngine/pt-tlas-before-framed-inline/` and
  `E:/UzmiEngine/pt-tlas-after-framed-inline/`. Both exit 0. The before image's
  false central black cross and incorrect secondary occlusion disappear; the
  after image preserves the sphere and separately placed walls. Max code delta
  165, MAE 28.833976 (an intended correction, not image equality).
- DDGI ray-buffer trace / atlas update, consumed by deferred GI
  (`r.RayTracing=1`, `r.RayTracing.GI=1`, `r.RadianceCache=1`): matching framed
  Cornell before/after captures in `pt-tlas-before-framed-ddgi/` and
  `pt-tlas-after-framed-ddgi/`, both under `E:/UzmiEngine/`, exit 0. Geometry
  stays correctly placed; corrected probe visibility changes indirect energy
  (max delta 74, MAE 49.224701). A further 140-frame diagnostic run in
  `pt-tlas-ddgi-diagnostic/` proves actual dispatch/readback: snapshot frame 120,
  read frame 124, 4,096 probes, mean radiance 1.1832176724, sky fraction
  0.9922943115, `rayBuffers=1`, six TLAS instances and 139 skipped static rebuilds.

The SDF-only radiance-cache trace uses the GDF rather than the TLAS. The optional
RT UV debug pass was source-reviewed as another TLAS consumer but was not
runtime-tested; it is not enabled in these scenes. No claim is made about an
independent RT UV image or the alternative per-texel DDGI mode.

#### Headful GPU validation

`E:/UzmiEngine/pt-tlas-gbv-reference/` uses the shipped reflection-reference
project, `--gpu-validation --path-trace`, 40 requested frames, screenshot 35,
and a 180-second timeout. Neither `--headless` nor `--hidden` is passed.
The command exits 0 after 44 rendered frames; the log explicitly enables GPU
validation and records `DispatchRays 1920x1080`, five TLAS instances and four
surface bounces. Debug IDs 527/538/607/942 are all absent (0 each).
Only the permitted initialization ID 1422 occurs; no shader compile fails.

An initial headful Cornell pipeline run exits 0 but is NOT accepted as clean
GBV evidence: its existing reflection-probe capture emits IDs 538 and 527 for
`ProbeCaptureCubemap`, unrelated to TLAS descriptor placement. A Cornell DDGI
GBV attempt was stopped within three minutes when first-frame instrumentation
was slow; its incomplete results are not accepted. Validation switched to the
probe-free shipped reference scene rather than changing unrelated shared
resource-state infrastructure.

The TLAS-disabled headful Cornell control in
`E:/UzmiEngine/pt-tlas-gbv-cornell-raster-control/` explicitly sets
`r.RayTracing=0` and reproduces all six `ProbeCaptureCubemap` subresource errors
for both IDs 538 and 527. This isolates those barriers from the changed TLAS
writer: the control does not build or use a TLAS.
The bounded control exits 0, captures frame 5 and completes 13 frames including
preload. No commit or archive was created; the five changed files are the TLAS
writer/header, `test_tlas_transform.cpp`, the implementation document and this
checklist. No Cornell scene or user VFX-editor files were changed.
Final process cleanup exits 0 and reports `NO_ENGINE_TEST_ZIG_PROCESSES`.
All 21 checklist items are complete under the acceptance scope documented above.
