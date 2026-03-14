## 1. Implementation

- [x] 1.1 Implement BRDF LUT compute shader (split-sum integration, 512x512 RG16F, 1024 samples) — `runtime/shaders/runtime/brdf_lut_cs.hlsl`
- [x] 1.2 Implement irradiance convolution compute shader (cosine-weighted, 32x32x6, RGBA16F, 64 samples) — `runtime/shaders/runtime/irradiance_cs.hlsl`
- [x] 1.3 Implement prefiltered environment map compute shader (GGX importance sampling, 128x128 base, 5 mip levels, 256 samples) — `runtime/shaders/runtime/prefilter_cs.hlsl`
- [x] 1.4 Create IBLBaker class to orchestrate bake pipeline (BakeBRDFLut + BakeEnvironment) — `runtime/engine/renderer/ibl/ibl_baker.{h,cpp}`
- [ ] 1.5 Add HDR equirectangular-to-cubemap conversion shader — NOT IMPLEMENTED (cubemaps currently generated on CPU)
- [x] 1.6 Add SkyLight data to PerFrameCB (irradianceSrvIndex, prefilteredSrvIndex, brdfLutSrvIndex, prefilteredMipLevels) — `runtime/engine/renderer/core/constant_buffers.h`
- [x] 1.7 Update mesh_ps.hlsl ambient section with IBL sampling (diffuse irradiance + specular prefiltered + BRDF LUT) — `runtime/shaders/runtime/mesh_ps.hlsl:212-251`
- [x] 1.8 Implement fallback to constant ambient when no IBL cubemaps are bound (UINT32_MAX sentinel check)
- [x] 1.9 Create IBL demo (procedural sky cubemap + PBR cubes grid) — `runtime/demos/ibl_test/ibl_test_demo.cpp`

## 2. Testing

- [ ] 2.1 Test BRDF LUT output dimensions and format (512x512, RG16F)
- [ ] 2.2 Test irradiance cubemap output (6 faces, 32x32, RGBA16F)
- [ ] 2.3 Test prefiltered cubemap mip chain (5 levels, decreasing resolution)
- [ ] 2.4 Test equirect-to-cubemap conversion (output is valid 6-face cubemap) — BLOCKED by 1.5
- [ ] 2.5 Test IBL fallback (no SkyLight → constant ambient, no visual regression)
- [ ] 2.6 Test SkyLight SRV indices in PerFrameCB (correct offsets and alignment)
- [ ] 2.7 Verify 95%+ coverage on all new code

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/06-lighting.md` with IBL pipeline and bake process
- [ ] 3.2 Add IBL tuning guide (environment map requirements, quality vs performance tradeoffs)
