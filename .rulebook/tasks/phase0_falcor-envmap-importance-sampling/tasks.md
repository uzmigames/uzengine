## Session 1: Luminance Pyramid Compute Shader

- [ ] 1.1 Create `envmap_luminance_pyramid_cs.hlsl` — input: `TextureCube<float4> tEnvMap`, output: `RWTexture2D<float> uLuminanceMip[N]` (equirectangular projection of cube). `[numthreads(8,8,1)]` entry point `main`
- [ ] 1.2 Cube face to equirectangular conversion in shader: for each output texel compute direction from (u,v), sample env map, compute luminance `Y = dot(rgb, float3(0.2126, 0.7152, 0.0722))`. Write to mip 0 (full resolution equirect)
- [ ] 1.3 Hierarchical reduction pass: each subsequent mip sums 2×2 blocks of the previous mip (log-luminance sum, not average) until 1×1. @ref Pharr & Humphreys 2004 "Physically Based Rendering" Chapter 14.6 (2D importance sampling)
- [ ] 1.4 `EnvMapSampler` C++ class skeleton: `Init(device, envCubeSRV)`, `Rebuild()` runs the pyramid CS, `GetPyramidSRV() → RhiTextureHandle`, `GetAliasSRV() → RhiBufferHandle`. Build + verify CS compiles and dispatches without device removal.

## Session 2: Alias Table + HLSL Sampling Library

- [ ] 2.1 CPU alias table construction: readback luminance mip 0 from GPU (staging buffer + fence wait). Build Vose's alias method table from the flat probability array: `struct AliasEntry { float prob; uint alias; }`. Upload as `StructuredBuffer<AliasEntry>` to GPU.
- [ ] 2.2 Create `include/envmap_sampling.hlsli` — `struct EnvSample { float3 direction; float pdf; float3 color; }`. `SampleEnvironment(float2 u, StructuredBuffer<AliasEntry> aliasTable, Texture2D<float> luminanceMip0, TextureCube<float4> envMap) → EnvSample`. O(1) lookup: index = floor(u.x * N), accept/reject with alias table entry. @ref Vose 1991 "A Linear Algorithm for Generating Random Numbers with a Given Distribution"
- [ ] 2.3 `EvaluateEnvironmentPDF(float3 direction, Texture2D<float> luminanceMip0, float totalLuminance) → float` — look up luminance at direction's texel divided by total; include Jacobian for equirectangular projection (1 / (2π² sinθ))
- [ ] 2.4 Build + verify: alias table CPU construction is tested with a simple gradient image — sampling should concentrate in bright regions (histogram check in unit test)

## Session 3: MIS Integration in Deferred Lighting

- [ ] 3.1 Add environment sample path to `deferred_lighting_ps.hlsl`: for metallic pixels (metallic > 0.5), generate a direction sample via `SampleEnvironment`, evaluate GGX BRDF for that direction, compute MIS weight using balance heuristic: `w = pdf_env / (pdf_env + pdf_brdf)`
- [ ] 3.2 Evaluate BRDF PDF for environment sample direction via existing `GGX_PDF(N, V, H, roughness)` helper. Combine: `contribution = brdf * envColor * misWeight / pdf_env`
- [ ] 3.3 Bind alias buffer and luminance mip0 to deferred lighting pass descriptor table. Guard behind `HAS_ENV_IMPORTANCE_SAMPLING` define so existing path is unchanged when buffers are not bound
- [ ] 3.4 Build + verify: metallic sphere demo — compare screenshot with and without importance sampling at `--screenshot-stdout`. Bright spot in env map should produce a sharper highlight with importance sampling enabled.
