## Session 1: Luminance Pyramid Compute Shader

- [x] 1.1 Create `envmap_luminance_pyramid_cs.hlsl` — Two-pass compute shader: PYRAMID_MIP0 (cube→equirect luminance × sinθ) and PYRAMID_REDUCE (2×2 sum reduction). Both at [numthreads(8,8,1)] entry point `main`. Created at `runtime/shaders/runtime/ibl/envmap_luminance_pyramid_cs.hlsl`.
- [x] 1.2 Cube face to equirectangular conversion in shader: EquirectToDir() maps UV to direction, samples cubemap, computes luminance × sin(theta) for correct solid angle weighting. BT.709 luminance coefficients.
- [x] 1.3 Hierarchical reduction pass: PYRAMID_REDUCE sums 2×2 blocks via Load (not average) preserving total luminance. Separate per-mip R32F textures, dispatched sequentially with SRV↔UAV barriers.
- [x] 1.4 `EnvMapSampler` C++ class: Full implementation at `runtime/engine/renderer/ibl/env_map_sampler.h/.cpp`. Init, Rebuild (pyramid dispatch → readback → alias table → upload), Shutdown. Lazy pipeline creation with PYRAMID_MIP0 and PYRAMID_REDUCE shader permutations.

## Session 2: Alias Table + HLSL Sampling Library

- [x] 2.1 CPU alias table construction: Vose's O(N) algorithm in EnvMapSampler::BuildAliasTable(). Readback via staging buffer + WaitIdle. Uploaded as StructuredBuffer<AliasEntry> with GetBufferSRVIndex() for bindless access.
- [x] 2.2 Create `include/envmap_sampling.hlsli` — EnvSample struct, SampleAliasTable() O(1) lookup, SampleEnvironment() full pipeline (alias → texel → UV → direction → cubemap sample → PDF). Includes equirect↔direction conversions (Z-up).
- [x] 2.3 `EvaluateEnvironmentPDF()` — direction → equirect UV → Load luminance → PDF with equirectangular Jacobian (texelCount / 2π²). For MIS weight computation in deferred lighting.
- [ ] 2.4 Build + verify: alias table CPU construction is tested with a simple gradient image — sampling should concentrate in bright regions (histogram check in unit test)

## Session 3: MIS Integration in Deferred Lighting

- [x] 3.1 Add environment sample path to `deferred_lighting_ps.hlsl`: for metallic>0.3, roughness<0.7 pixels, 4 Hammersley samples via SampleEnvironment(), evaluate GGX BRDF, compute MIS balance heuristic weight: `w = pdf_env / (pdf_env + pdf_brdf)`. Golden-ratio pixel decorrelation.
- [x] 3.2 BRDF PDF: `D_GGX * NoH / (4 * VoH)`. MIS contribution: `brdf * envColor * NoL * misWeight / pdfEnv`. Added to ambient (additive to split-sum IBL).
- [x] 3.3 Added 6 fields to PerFrameCB (envSamplerAliasSrv, LumMip0Srv, TotalLuminance, TexelCount, LumWidth, LumHeight). Used 6 of 22 padding floats. Gated by `envSamplerAliasSrv != kSentinelSrvIndex` bindless check. Full CB declared in deferred_lighting_ps.hlsl (680 bytes to reach env fields).
- [x] 3.4 Build + verify: PBR showcase renders clean with EnvMapSampler active. Pipeline logs confirm: mip0+reduce shaders compiled, 10-level pyramid (512x256→1x1), alias table 131072 entries (SRV=98520), totalLum=51053.9. No visual corruption or crashes.
