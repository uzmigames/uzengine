# Proposal: phase0_falcor-envmap-importance-sampling

## Why
UzEngine uses a prefiltered cubemap (IBL) for environment lighting — this approximation bakes the environment into fixed-roughness mips and cannot represent direct contributions from bright spots (sun through glass, neon signs, emissive panels). Falcor's `EnvMapSampler` constructs a hierarchical luminance pyramid from the environment map and uses an alias table for O(1) importance sampling, allowing direct environment sampling weighted by luminance. This is the correct reference for accurate environment-lit metallic surfaces, glass transmission, and future path tracing where the environment is sampled as an explicit light source. Without importance sampling the environment, Monte Carlo estimators converge slowly and miss narrow bright sources entirely.

## What Changes
- Luminance pyramid compute shader: `envmap_luminance_pyramid_cs.hlsl` — downsamples env map to 1×1 via log-luminance reduction, storing per-texel importance weights in a mip chain
- CPU alias table construction: reads luminance mip chain back to CPU, builds Vose alias table for O(1) discrete sampling from the 2D distribution; uploaded as a `StructuredBuffer<AliasEntry>`
- `include/envmap_sampling.hlsli`: HLSL helper library with `SampleEnvironment(float2 u) → EnvSample { direction, pdf, color }` and `EvaluateEnvironmentPDF(direction) → float` for MIS weight computation
- MIS integration in deferred lighting: combine environment PDF with GGX BRDF PDF using balance heuristic for accurate metallic specular from environment
- `EnvMapSampler` C++ class: manages pyramid texture, alias table buffer, rebuild on env map change
- Affected files: new `envmap_luminance_pyramid_cs.hlsl`, new `envmap_sampling.hlsli`, new `env_map_sampler.h/.cpp`, `deferred_lighting_ps.hlsl`

## Impact
- Affected specs: `docs/specs/rendering/ibl-pipeline.md`
- Affected code: `env_map_sampler.h/.cpp`, `deferred_lighting_ps.hlsl`, `include/envmap_sampling.hlsli`
- Breaking change: NO — existing prefiltered IBL path is preserved as fallback; importance sampling is additive for metallic/specular contribution
- User benefit: Accurate environment lighting on metallic surfaces without the low-frequency blur of prefiltered IBL; prerequisites met for path tracing environment sampling; correct MIS weights for future ReSTIR integration
