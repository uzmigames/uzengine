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
frame count. Sessions 3.x, 4.x and documentation are DEFERRED; the stage-1 CLI
activation is implemented now solely to exercise this renderer.

## Session 3: Accumulation Pass

- [ ] 3.1 Create `pathtracer_accumulate_cs.hlsl` — inputs: `Texture2D<float4> tNewSample`, `RWTexture2D<float4> uAccumulated`, `uint frameCount`. Running average: `accumulated = lerp(accumulated, newSample, 1.0 / (frameCount + 1))`. Use Kahan compensated summation to avoid float precision drift at high frame counts.
- [ ] 3.2 Camera movement detection: compare current `viewMatrix` in `PerFrameCB` vs previous frame's. If changed by more than epsilon, reset `frameCount = 0` and clear `uAccumulated` to black.
- [ ] 3.3 `PathTracerPass` C++ class: manages accumulation UAV texture (same resolution as swapchain), frame count, camera change detection. `Execute(cmd, radianceSRV) → accumulatedSRV`.
- [ ] 3.4 Build + verify: static scene — capture 100 frames progressive accumulation, compare first vs last frame with `--flip`, expect significant quality improvement (lower FLIP score on last frame vs first). Camera move resets correctly.

## Session 4: Multi-Bounce + NEE + Demo

- [ ] 4.1 Multi-bounce recursion (max 4 bounces): in closest-hit, if `bounceCount < maxBounces` and Russian roulette survives (throughput luminance > random), recursively call `TraceRay` with updated `PathPayload`. `maxBounces` configurable via `PerFrameCB` field.
- [ ] 4.2 Russian roulette: compute survival probability as `clamp(max(throughput.r, throughput.g, throughput.b), 0.05, 1.0)`. On survival, divide throughput by probability to maintain unbiased estimator.
- [ ] 4.3 Next-Event Estimation (NEE): at each hit, in addition to the bounce, explicitly sample the directional light and add its contribution weighted by `1/lightPdf` (MIS not required for directional lights, their PDF is a delta distribution).
- [ ] 4.4 `--path-trace` CLI flag: activates path tracer pass in `engine_loop.cpp`, replaces deferred lighting output with accumulation buffer output. `--pt-bounces <N>` sets max bounce count (default 4).
- [ ] 4.5 Build + verify: Cornell box scene — path-traced output with 4 bounces shows color bleeding (red/green wall color on white floor). Compare to deferred renderer via `--flip` — expect measurable difference (path tracer more accurate). Screenshot convergence after 200 frames.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass
