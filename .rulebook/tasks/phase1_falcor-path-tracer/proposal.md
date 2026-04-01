# Proposal: phase1_falcor-path-tracer

## Why
UzEngine has a high-quality rasterized deferred pipeline but no ground-truth reference renderer. Without a path tracer, there is no way to validate whether the rasterized output is physically correct — every "bug vs artistic choice" is guesswork. Falcor's 3-stage wavefront path tracer (GeneratePaths, TracePass, ResolvePass) serves both as a reference renderer and as the production renderer for offline-quality output. A path tracer is also the prerequisite for ReSTIR DI, NRD denoising, and hybrid rasterize+trace workflows. Requires: DXR RT infrastructure (phase1_falcor-dxr-rt-infrastructure).

## What Changes
- `pathtracer_raygen.hlsl`: primary ray generation from camera, jittered per-pixel with Halton sequence for progressive accumulation; writes hit payload to UAV textures (position, normal, albedo, emission)
- `pathtracer_closesthit.hlsl`: reads material from GBuffer-equivalent data in hit attributes, evaluates direct lighting via shadow ray, samples GGX BRDF for next-bounce direction. Uses existing `brdf.hlsli`.
- `pathtracer_miss.hlsl`: evaluates environment map color for rays that escape the scene
- `pathtracer_accumulate_cs.hlsl`: running weighted average with Kahan summation for progressive convergence, frame count tracking, automatic reset on camera movement (camera transform change detection)
- `PathTracerPass` C++ class: manages RT pipeline, accumulation buffer, integrates with render graph as an alternative to deferred lighting
- Multi-bounce extension (2-4 bounces) with Russian roulette path termination, Next-Event Estimation (NEE) for direct light sampling
- CLI flag `--path-trace` to activate the path tracer in place of deferred lighting
- Affected files: new `pathtracer_*.hlsl` shaders, new `path_tracer_pass.h/.cpp`, `engine_loop.cpp` (pass selection)

## Impact
- Affected specs: new `docs/specs/rendering/path-tracer.md`
- Affected code: new path tracer shaders and pass, `engine_loop.cpp`, render graph pass registration
- Breaking change: NO — path tracer is an alternative path activated via `--path-trace`; deferred pipeline is unchanged
- User benefit: Ground-truth reference output for validating rasterized rendering; offline-quality renders; foundation for ReSTIR DI, NRD denoising, and hybrid rasterize+trace workflows
