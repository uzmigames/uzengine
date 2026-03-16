## 1. Implementation — Sky Atmosphere

- [x] 1.1 Implement fullscreen vertex shader (fullscreen triangle, world-space ray direction) — `runtime/shaders/runtime/lighting/fullscreen_vs.hlsl` (shared across sky, fog, tonemap passes)
- [x] 1.2 Implement sky atmosphere pixel shader (Rayleigh + Mie single-scatter ray march, transmittance LUT sampling, sun disk) — `runtime/shaders/runtime/atmosphere/sky_atmosphere_ps.hlsl`
- [x] 1.3 Add SkyAtmosphereComponent ECS component (Rayleigh/Mie coefficients, scale heights, planet radius, sun illuminance, ground albedo, 50+ parameters) — `runtime/engine/ecs/environment/sky_atmosphere_component.h`
- [x] 1.4 Add SkyAtmosphereCB constant buffer struct (256 bytes, b2 slot) — `runtime/engine/renderer/core/constant_buffers.h`
- [x] 1.5 Implement SkyAtmospherePass render pass (fullscreen, after geometry, before fog) — `runtime/engine/renderer/passes/atmosphere/sky_atmosphere_pass.{h,cpp}`
- [x] 1.6 Implement transmittance LUT compute shader (Bruneton 2017 parameterization, 256x64 RGBA16F) — `runtime/shaders/runtime/atmosphere/transmittance_lut_cs.hlsl`
- [x] 1.7 Implement TransmittanceLutPass compute pass (dirty flag, baked once per atmosphere change) — `runtime/engine/renderer/passes/atmosphere/transmittance_lut_pass.{h,cpp}`
- [x] 1.8 Add AtmosphereLutCB constant buffer struct (256 bytes) — `runtime/engine/renderer/core/constant_buffers.h`
- [x] 1.9 Add artistic scattering multipliers (rayleighScatteringScale, mieScatteringScale, mieAbsorptionScale, skyLuminanceFactor, multiScatteringFactor) — artistic vs physical separation in shader and component

## 2. Implementation — Height Fog

- [x] 2.1 Implement height fog pixel shader (UE5-exact CalculateLineIntegralShared, 2 layers, directional inscattering) — `runtime/shaders/runtime/atmosphere/height_fog_ps.hlsl`
- [x] 2.2 Add HeightFogComponent ECS component (density, heightFalloff, baseHeight, maxOpacity, second layer, inscattering) — `runtime/engine/ecs/environment/height_fog_component.h`
- [x] 2.3 Add HeightFogCB constant buffer struct (256 bytes, b2 slot, pre-computed RayOriginTerms) — `runtime/engine/renderer/core/constant_buffers.h`
- [x] 2.4 Implement HeightFogPass render pass (fullscreen, after sky atmosphere, before tonemap) — `runtime/engine/renderer/passes/atmosphere/height_fog_pass.{h,cpp}`
- [x] 2.5 CPU precompute RayOriginTerms (`density * pow(2, -heightFalloff * (camZ - baseHeight))`) per frame — `runtime/engine/renderer/scene_renderer.cpp`

## 3. Implementation — Sky Cubemap Capture (IBL Integration)

- [x] 3.1 Implement sky cubemap capture compute shader (64x64x6 RGBA16F, atmosphere ray march without sun disk) — `runtime/shaders/runtime/atmosphere/sky_cubemap_cs.hlsl`
- [x] 3.2 Implement SkyCubemapCapturePass compute pass (Dispatch [8,8,6], reads transmittance LUT) — `runtime/engine/renderer/passes/atmosphere/sky_cubemap_capture_pass.{h,cpp}`
- [x] 3.3 Integrate sky cubemap → IBL baker pipeline in SceneRenderer (dirty flag triggers rebake) — `runtime/engine/renderer/scene_renderer.cpp`

## 4. Implementation — Render Graph Integration

- [x] 4.1 Register transmittance LUT pass in render graph (cached, dirty flag) — `scene_renderer.cpp`
- [x] 4.2 Register sky cubemap capture pass (when m_skyIBLDirty) — `scene_renderer.cpp`
- [x] 4.3 Register sky atmosphere pass (after geometry, reads transmittance LUT) — `scene_renderer.cpp`
- [x] 4.4 Register height fog pass (after sky atmosphere, before tonemap) — `scene_renderer.cpp`
- [x] 4.5 Pass ordering: Geometry → Shadow → TransmittanceLUT → SkyCubemapCapture → SkyAtmosphere → HeightFog → Tonemap

## 5. Testing

- [x] 5.1 Test SkyAtmosphereCB struct size (256 bytes) and field offsets — `test_ibl_atmosphere_cb.cpp`
- [x] 5.2 Test HeightFogCB struct size (256 bytes) and field offsets — `test_ibl_atmosphere_cb.cpp`
- [x] 5.3 Test AtmosphereLutCB struct size (256 bytes) and field offsets — `test_ibl_atmosphere_cb.cpp`
- [x] 5.4 Test SkyAtmosphereComponent Earth-like defaults (Rayleigh/Mie coefficients, scale heights) — `test_ibl_atmosphere_cb.cpp`
- [x] 5.5 Test HeightFogComponent UE5 defaults (density, falloff, inscattering) — `test_ibl_atmosphere_cb.cpp`
- [x] 5.6 Test SkyCubemapCaptureRootConstants struct size (32 bytes) and field offsets — `test_ibl_atmosphere_cb.cpp`
- [x] 5.7 Test CB memcpy round-trips for all atmosphere structs — `test_ibl_atmosphere_cb.cpp`
- [x] 5.8 Test TransmittanceLutRootConstants (32 bytes, field offsets, Earth invariant) — `test_ibl_atmosphere_cb.cpp`
- [x] 5.9 Test SkyAtmospherePassDesc defaults (transmittanceLutSrvIndex=UINT32_MAX, invalid handles, viewport zero) — `test_ibl_atmosphere_cb.cpp`
- [x] 5.10 Test HeightFogPassDesc defaults (invalid handles, viewport zero, bindless false) — `test_ibl_atmosphere_cb.cpp`
- [x] 5.11 Test TransmittanceLutPassDesc defaults (invalid target/pipeline, root constants zero) — `test_ibl_atmosphere_cb.cpp`
- [x] 5.12 Fallback verified: no SkyAtmosphereComponent → sky pass loop body never executes → clear color preserved
- [x] 5.13 All 185 tests pass (1 pre-existing failure in UE5Parity.SkyLightDefaults_Occlusion, unrelated)

## 6. Documentation

- [x] 6.1 Height fog system doc — `docs/engine/12-rendering/26-height-fog.md`
- [x] 6.2 Sky atmosphere system doc — `docs/engine/12-rendering/27-sky-atmosphere.md`
