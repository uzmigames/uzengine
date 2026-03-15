## 1. Implementation — IBL Core

- [x] 1.1 Implement BRDF LUT compute shader (split-sum integration, 512x512 RG16F, 1024 samples) — `runtime/shaders/runtime/ibl/brdf_lut_cs.hlsl`
- [x] 1.2 Implement irradiance convolution compute shader (cosine-weighted, 32x32x6, RGBA16F, 64 samples) — `runtime/shaders/runtime/ibl/irradiance_cs.hlsl`
- [x] 1.3 Implement prefiltered environment map compute shader (GGX importance sampling, 128x128 base, 5 mip levels, 256 samples) — `runtime/shaders/runtime/ibl/prefilter_cs.hlsl`
- [x] 1.4 Create IBLBaker class to orchestrate bake pipeline (BakeBRDFLut + BakeEnvironment) — `runtime/engine/renderer/ibl/ibl_baker.{h,cpp}`
- [ ] 1.5 Add HDR equirectangular-to-cubemap conversion shader — NOT IMPLEMENTED (cubemaps currently generated on CPU via sky_cubemap.cpp)
- [x] 1.6 Add SkyLight data to PerFrameCB (irradianceSrvIndex, prefilteredSrvIndex, brdfLutSrvIndex, prefilteredMipLevels) — `runtime/engine/renderer/core/constant_buffers.h`
- [x] 1.7 Update mesh_ps.hlsl ambient section with IBL sampling (diffuse irradiance + specular prefiltered + BRDF LUT) — `runtime/shaders/runtime/geometry/mesh_ps.hlsl`
- [x] 1.8 Implement fallback to constant ambient when no IBL cubemaps are bound (UINT32_MAX sentinel check)
- [x] 1.9 Create IBL demo (procedural sky cubemap + PBR cubes grid) — `runtime/demos/ibl_test/`
- [x] 1.10 Add IBL helper functions in shader include — `runtime/shaders/runtime/include/ibl.hlsli`

## 2. Implementation — Sky Atmosphere & Environment

- [x] 2.1 Create SkyAtmosphereComponent ECS component (50+ parameters: intensity, cubemap capture, lower hemisphere, DFAO, cloud AO) — `runtime/engine/ecs/sky_atmosphere_component.h`
- [x] 2.2 Create HeightFogComponent ECS component — `runtime/engine/ecs/height_fog_component.h`
- [x] 2.3 Implement SkyAtmospherePass render pass (fullscreen, SkyAtmosphereCB at b2, 256-byte aligned) — `runtime/engine/renderer/passes/sky_atmosphere_pass.{h,cpp}`
- [x] 2.4 Implement HeightFogPass render pass (fullscreen, HeightFogCB at b2, 256-byte aligned) — `runtime/engine/renderer/passes/height_fog_pass.{h,cpp}`
- [x] 2.5 Add sky atmosphere pixel shader — `runtime/shaders/runtime/atmosphere/sky_atmosphere_ps.hlsl`
- [x] 2.6 Add height fog pixel shader — `runtime/shaders/runtime/atmosphere/height_fog_ps.hlsl`
- [x] 2.7 Integrate sky atmosphere + height fog passes into SceneRenderer render graph — `runtime/engine/renderer/scene_renderer.cpp`
- [x] 2.8 Add SkyAtmosphereCB and HeightFogCB constant buffer structs (256 bytes each) — `runtime/engine/renderer/core/constant_buffers.h`
- [ ] 2.9 Add HDR equirectangular-to-cubemap GPU conversion (replace CPU sky_cubemap.cpp) — same as 1.5

## 3. Testing

- [ ] 3.1 Test BRDF LUT output dimensions and format (512x512, RG16F)
- [ ] 3.2 Test irradiance cubemap output (6 faces, 32x32, RGBA16F)
- [ ] 3.3 Test prefiltered cubemap mip chain (5 levels, decreasing resolution)
- [ ] 3.4 Test equirect-to-cubemap conversion (output is valid 6-face cubemap) — BLOCKED by 1.5/2.9
- [ ] 3.5 Test IBL fallback (no SkyLight → constant ambient, no visual regression)
- [ ] 3.6 Test SkyLight SRV indices in PerFrameCB (correct offsets and alignment)
- [ ] 3.7 Test SkyAtmosphereCB/HeightFogCB constant buffer alignment (256 bytes each)
- [ ] 3.8 Verify 95%+ coverage on all new code

## 4. Documentation

- [ ] 4.1 Create `docs/engine/12-rendering/06-lighting.md` with IBL pipeline, bake process, and sky atmosphere
- [ ] 4.2 Add IBL tuning guide (environment map requirements, quality vs performance tradeoffs)
- [x] 4.3 Sky architecture analysis docs — `docs/analysis/rendering/` (UE5, Godot, synthesis)
