## 1. Implementation — IBL Core

- [x] 1.1 Implement BRDF LUT compute shader (split-sum integration, 512x512 RG16F, 1024 samples) — `runtime/shaders/runtime/ibl/brdf_lut_cs.hlsl`
- [x] 1.2 Implement irradiance convolution compute shader (cosine-weighted, 32x32x6, RGBA16F, 64 samples) — `runtime/shaders/runtime/ibl/irradiance_cs.hlsl`
- [x] 1.3 Implement prefiltered environment map compute shader (GGX importance sampling, 128x128 base, 5 mip levels, 256 samples) — `runtime/shaders/runtime/ibl/prefilter_cs.hlsl`
- [x] 1.4 Create IBLBaker class to orchestrate bake pipeline (BakeBRDFLut + BakeEnvironment) — `runtime/engine/renderer/ibl/ibl_baker.{h,cpp}`
- [ ] 1.5 Add HDR equirectangular-to-cubemap conversion shader — NOT IMPLEMENTED (cubemaps currently generated on GPU via sky_cubemap_cs.hlsl compute shader, no HDR file loading path yet)
- [x] 1.6 Add SkyLight data to PerFrameCB (irradianceSrvIndex, prefilteredSrvIndex, brdfLutSrvIndex, prefilteredMipLevels, skyIrradianceSH[7]) — `runtime/engine/renderer/core/constant_buffers.h`
- [x] 1.7 Update mesh_ps.hlsl ambient section with IBL sampling (diffuse irradiance + specular prefiltered + BRDF LUT) — `runtime/shaders/runtime/geometry/mesh_ps.hlsl`
- [x] 1.8 Implement fallback to constant ambient when no IBL cubemaps are bound (UINT32_MAX sentinel check)
- [x] 1.9 Create IBL demo (procedural sky cubemap + PBR cubes grid) — `runtime/demos/ibl_test/`
- [x] 1.10 Add IBL helper functions in shader include — `runtime/shaders/runtime/include/ibl.hlsli` (Hammersley, ImportanceSampleGGX, FresnelSchlickRoughness, CubeFaceDirection)
- [x] 1.11 Implement L2 Spherical Harmonics (SH9) projection from GPU cubemap — `runtime/engine/renderer/ibl/sh_projection.{h,cpp}` — 7 float4 UE5 SkyIrradianceEnvironmentMap parity
- [x] 1.12 GPU cubemap readback for SH projection with fallback analytic Rayleigh SH — integrated in scene_renderer.cpp

## 2. Implementation — Sky Atmosphere & Environment

- [x] 2.1 Create SkyAtmosphereComponent ECS component (50+ parameters: intensity, cubemap capture, lower hemisphere, DFAO, cloud AO) — `runtime/engine/ecs/environment/sky_atmosphere_component.h`
- [x] 2.2 Create HeightFogComponent ECS component — `runtime/engine/ecs/environment/height_fog_component.h`
- [x] 2.3 Create SkyLightComponent ECS component — `runtime/engine/ecs/environment/sky_light_component.h`
- [x] 2.4 Implement SkyAtmospherePass render pass (fullscreen, SkyAtmosphereCB at b2, 256-byte aligned) — `runtime/engine/renderer/passes/atmosphere/sky_atmosphere_pass.{h,cpp}`
- [x] 2.5 Implement HeightFogPass render pass (fullscreen, HeightFogCB at b2, 256-byte aligned, UE5 CalculateLineIntegralShared exact) — `runtime/engine/renderer/passes/atmosphere/height_fog_pass.{h,cpp}`
- [x] 2.6 Implement TransmittanceLutPass compute pass (256x64 RGBA16F, dirty flag, Rayleigh+Mie optical depth) — `runtime/engine/renderer/passes/atmosphere/transmittance_lut_pass.{h,cpp}`
- [x] 2.7 Implement SkyCubemapCapturePass compute pass (64x64x6 RGBA16F cubemap for IBL baking) — `runtime/engine/renderer/passes/atmosphere/sky_cubemap_capture_pass.{h,cpp}`
- [x] 2.8 Add sky atmosphere pixel shader (physical Rayleigh+Mie scattering + transmittance LUT + sun disk) — `runtime/shaders/runtime/atmosphere/sky_atmosphere_ps.hlsl`
- [x] 2.9 Add height fog pixel shader (2 layers, directional inscattering, start/cutoff distance) — `runtime/shaders/runtime/atmosphere/height_fog_ps.hlsl`
- [x] 2.10 Add transmittance LUT compute shader — `runtime/shaders/runtime/atmosphere/transmittance_lut_cs.hlsl`
- [x] 2.11 Add sky cubemap compute shader — `runtime/shaders/runtime/atmosphere/sky_cubemap_cs.hlsl`
- [x] 2.12 Integrate all atmosphere + IBL passes into SceneRenderer render graph — `runtime/engine/renderer/scene_renderer.cpp`
- [x] 2.13 Add SkyAtmosphereCB, HeightFogCB, and AtmosphereLutCB constant buffer structs (256 bytes each) — `runtime/engine/renderer/core/constant_buffers.h`
- [x] 2.14 Add artistic scattering multipliers to SkyAtmosphereComponent (rayleighScatteringScale, mieScatteringScale, mieAbsorptionScale, skyLuminanceFactor, multiScatteringFactor) — artistic vs physical separation
- [ ] 2.15 Add HDR equirectangular-to-cubemap GPU conversion (replace procedural sky as sole cubemap source) — same as 1.5

## 3. Implementation — HDR Pipeline & Post-Processing

- [x] 3.1 Implement GPU histogram auto-exposure compute pass — `runtime/engine/renderer/passes/postprocess/auto_exposure.{h,cpp}`
- [x] 3.2 Implement exposure histogram compute shader (luminance histogram, 256 bins) — `runtime/shaders/runtime/postprocess/exposure_histogram_cs.hlsl`
- [x] 3.3 Implement exposure average compute shader (weighted average from histogram) — `runtime/shaders/runtime/postprocess/exposure_average_cs.hlsl`
- [x] 3.4 Implement tonemap pass (ACES filmic + auto-exposure, HDR→LDR, sole tonemap point in pipeline) — `runtime/shaders/runtime/postprocess/tonemap_ps.hlsl`
- [x] 3.5 Integrate auto-exposure + tonemap into SceneRenderer pipeline — exposure result feeds PerFrameCB.exposure

## 4. Testing

- [ ] 4.1 Test BRDF LUT output dimensions and format (512x512, RG16F)
- [ ] 4.2 Test irradiance cubemap output (6 faces, 32x32, RGBA16F)
- [ ] 4.3 Test prefiltered cubemap mip chain (5 levels, decreasing resolution)
- [ ] 4.4 Test equirect-to-cubemap conversion (output is valid 6-face cubemap) — BLOCKED by 1.5/2.15
- [ ] 4.5 Test IBL fallback (no SkyLight → constant ambient, no visual regression)
- [ ] 4.6 Test SkyLight SRV indices in PerFrameCB (correct offsets and alignment)
- [ ] 4.7 Test SkyAtmosphereCB/HeightFogCB/AtmosphereLutCB constant buffer alignment (256 bytes each)
- [ ] 4.8 Test SH L2 projection coefficients against known analytic values
- [ ] 4.9 Test auto-exposure convergence (histogram → average → exposure value)
- [ ] 4.10 Verify 95%+ coverage on all new code

## 5. Documentation

- [ ] 5.1 Create `docs/engine/12-rendering/06-lighting.md` with IBL pipeline, bake process, SH projection, and sky atmosphere
- [ ] 5.2 Add IBL tuning guide (environment map requirements, quality vs performance tradeoffs, SH vs cubemap tradeoffs)
- [x] 5.3 Sky architecture analysis docs — `docs/analysis/rendering/` (UE5, Godot, synthesis)
- [ ] 5.4 Document HDR pipeline (auto-exposure histogram, tonemap, exposure control)
- [ ] 5.5 Document height fog system (UE5 parity, 2-layer, directional inscattering, CPU precomputed RayOriginTerms)
