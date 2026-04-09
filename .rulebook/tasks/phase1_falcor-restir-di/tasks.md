## Session 1: Reservoir Data Structure + Initial Sampling

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
