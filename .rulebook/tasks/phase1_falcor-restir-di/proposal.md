# Proposal: phase1_falcor-restir-di

## Why
UzEngine's deferred lighting evaluates each light independently per pixel — with N lights that is N shading evaluations per pixel, making large light counts (1000+) infeasible. ReSTIR DI (Reservoir-based Spatiotemporal Importance Resampling for Direct Illumination) by Bitterli et al. 2020 handles millions of lights in real time by reducing the problem to streaming RIS candidate selection per pixel, then reusing reservoirs across neighboring pixels and previous frames. Falcor uses the RTXDI SDK. UzEngine implements ReSTIR DI from scratch to avoid the SDK dependency and to remain BSD-compatible. Requires: DXR RT infrastructure (for shadow visibility rays). @ref Bitterli et al. 2020 "Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting".

## What Changes
- `Reservoir` struct in `include/restir_di.hlsli`: `{ LightSample sample; float weightSum; uint M; float W }` — stores the selected candidate and its unbiased contribution weight
- Initial sampling pass (`restir_initial_cs.hlsl`): per pixel, generate N=32 candidate light samples via streaming RIS (Talbot et al. 2005), evaluate target PDF (unshadowed contribution), store selected reservoir
- Temporal reuse pass (`restir_temporal_cs.hlsl`): reproject previous reservoir using motion vectors, combine with current via pairwise MIS, clamp `M` to 20 frames to bound temporal bias, discard on disocclusion (normal/depth test)
- Spatial reuse pass (`restir_spatial_cs.hlsl`): 5-iteration resampling from random neighbors within 30px radius, Jacobian correction for geometry difference, shadow ray visibility test per reuse candidate
- Integration in deferred lighting: `--restir-di` flag activates ReSTIR passes; deferred lighting reads final reservoir and evaluates the selected light via existing PBR shading
- `RestirDiPass` C++ class managing 3 compute passes, reservoir ping-pong buffers, motion vector dependency
- Affected files: new `restir_di.hlsli`, `restir_initial_cs.hlsl`, `restir_temporal_cs.hlsl`, `restir_spatial_cs.hlsl`, new `restir_di_pass.h/.cpp`, `deferred_lighting_ps.hlsl`

## Impact
- Affected specs: new `docs/specs/rendering/restir-di.md`
- Affected code: new ReSTIR shaders, new pass class, deferred lighting integration, motion vector pass (must exist or be added)
- Breaking change: NO — ReSTIR is an opt-in path via `--restir-di`; default deferred pipeline is unchanged
- User benefit: Scenes with 1000+ lights render correctly at real-time frame rates; spatiotemporal reuse provides noise-free shadows and multi-light contribution without N×lighting cost; foundation for ReSTIR GI and hybrid RT workflows
