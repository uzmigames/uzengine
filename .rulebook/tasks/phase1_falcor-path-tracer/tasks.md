## Session 1: Ray Generation Shader

- [ ] 1.1 Create `pathtracer_raygen.hlsl` — entry point `main` (`[shader("raygeneration")]`). Compute ray origin and direction from camera matrices in `PerFrameCB`. Apply Halton sequence jitter for anti-aliasing (sample index from accumulation frame count, dimensions 2+3).
- [ ] 1.2 Define `PathPayload` struct: `float3 radiance`, `float3 throughput`, `uint bounceCount`, `bool terminated`. Define `ShadowPayload`: `bool hit`. Add to shared `pathtracer_shared.hlsli`.
- [ ] 1.3 `TraceRay` call in raygen: TLAS SRV bound at `t0`. Output: write `PathPayload.radiance` to `RWTexture2D<float4> uRadiance`. Single bounce in this session (miss + closest-hit only, no recursion yet).
- [ ] 1.4 Build + verify: raygen shader compiles with DXC `lib_6_6` profile. RT pipeline creates without error.

## Session 2: Closest-Hit + Miss Shaders

- [ ] 2.1 Create `pathtracer_miss.hlsl` — entry `main` (`[shader("miss")]`): sample `TextureCube<float4> tEnvMap` in the ray direction, write to `PathPayload.radiance`, set `terminated = true`
- [ ] 2.2 Create `pathtracer_closesthit.hlsl` — entry `main` (`[shader("closesthit")]`): read vertex attributes (position, normal, UV) from hit triangle via `BuiltInTriangleIntersectionAttributes`. Load material from instance's `materialId` via `StructuredBuffer<MaterialData>`.
- [ ] 2.3 Direct lighting in closest-hit: for each directional light, cast shadow ray (`TraceRay` with `ACCEPT_FIRST_HIT_AND_END_SEARCH` flag and `ShadowPayload`). If not shadowed, evaluate `EvaluatePBR` from `brdf.hlsli`. Add to `PathPayload.radiance`.
- [ ] 2.4 BRDF sampling for next bounce: call `SampleGGX(N, V, roughness, metallic, u)` from `brdf.hlsli` to generate next-bounce direction. Write to `PathPayload.throughput *= brdfWeight / pdf`. Set `bounceCount++`.
- [ ] 2.5 Build + verify: 1-bounce path tracer renders a scene with a directional light. Capture via `--path-trace --screenshot-stdout --frames 30`. Verify soft shadows and metallic reflections visible.

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
