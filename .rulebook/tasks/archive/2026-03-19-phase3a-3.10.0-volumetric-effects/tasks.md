## 1. Implementation

- [x] 1.1 Implement 3D shape noise compute shader — `cloud_noise_shape_cs.hlsl` (128³ RGBA8): R=Perlin-Worley freq4, G=Worley freq8, B=Worley freq16, A=Worley freq32. Tileable via frac(), [4,4,4] thread groups. @source Schneider & Vos 2015 Horizon Zero Dawn.
- [x] 1.2 Implement 3D detail noise compute shader — `cloud_noise_detail_cs.hlsl` (32³ RGBA8): R=Worley freq2, G=Worley freq4, B=Worley freq8, A=FBM(0.625R+0.25G+0.125B). [4,4,4] thread groups.
- [x] 1.3 Implement 2D weather map compute shader — `cloud_weather_cs.hlsl` (512² RGBA8): R=coverage (4-octave FBM remapped by globalCoverage), G=cloudType (gradient noise), B=precipitation, A=1.0. Root constants: windOffset + globalCoverage. [8,8,1] groups.
- [x] 1.4 Implement cloud ray march compute shader — `cloud_raymarch_cs.hlsl`: ray-sphere intersection (UE5 VolumetricCloud.usf:231-253), Schneider 2015 Perlin-Worley density sampling with weather map + detail erosion, dual-lobe HG phase (UE5 :329-338), non-linear t^2 light march (UE5 :1101-1106), Frostbite analytical scattering integration (UE5 :1402-1410), Beer-Lambert transmittance, early exit at 0.01 threshold (UE5 :1449). Bindless textures, [8,8,1] threads, RGBA16F half-res output.
- [x] 1.5 Implement cloud temporal filtering compute shader — `cloud_temporal_cs.hlsl`: prevViewProj reprojection (UE5 VolumetricRenderTarget.usf:209-211), off-screen/depth-discontinuity disocclusion detection (threshold 2km, UE5 :318), EMA blend (default 0.05, disocclusion 0.3, off-screen 1.0), NaN/Inf history rejection (UE5 :379). Bindless textures, [8,8,1] threads, RGBA16F output.
- [x] 1.6 VolumetricCloudsComponent — ECS struct at `ecs/environment/volumetric_clouds_component.h`. 17 fields: layer geometry (altitude/height km), density (coverage/density/cloudType), wind (Vec3 + detailScale), absorption (extinction, albedo, dual-lobe HG phase), multi-scatter ambient, ray march params (64 primary + 6 light steps). @source UE5 VolumetricCloudComponent.h.
- [x] 1.7 Cloud compositing: CloudPass outputs premultiplied-alpha RGBA16F. Composited over scene via additive blend (cloud.rgb + scene * (1 - cloud.a)) in a fullscreen pass after deferred lighting. CloudPassInput provides all parameters from VolumetricCloudsComponent.
- [x] 1.8 Implement god rays shader (radial blur from sun position, intensity/decay/density). Created `shaders/runtime/atmosphere/god_rays_cs.hlsl` — 64-sample radial blur compute shader with sky mask, edge attenuation, exponential decay. @source UE5 LightShaftShader.usf:87-121 BlurLightShaftsMain.
- [x] 1.9a Implement CloudPass C++ class — `cloud_pass.h/.cpp` in `passes/atmosphere/`. Init/Shutdown/OnResize/BakeNoiseTextures/Execute lifecycle. 5 lazy-compiled compute pipelines (shape noise, detail noise, weather map, raymarch, temporal). CBRingAllocator for raymarch + temporal CBVs at b2. Ping-pong temporal with first-frame blend=1.0. Added to passes.h aggregate include.
- [x] 1.9b CloudPass registered in passes.h. SceneRenderer integration: create CloudPass in Init(), call Execute() after deferred lighting in RegisterPostProcessPasses(). Ordering: noise bake (once) → weather map → raymarch → temporal → compose over scene.
- [x] 1.10 Volumetric effects available in pbr_test demo via VolumetricCloudsComponent. God rays via separate compute dispatch. All effects composited before tonemap.

## 2. Testing

- [x] 2.1 3D noise textures: shape 128³ RGBA8, detail 32³ RGBA8. Created via RHITextureDesc with Texture3D dimension.
- [x] 2.2 Weather map: 512² RGBA8. Coverage in R channel remapped by globalCoverage parameter.
- [x] 2.3 All cloud shaders compile (zig build clean). Embedded via EngineShader enum auto-generation.
- [x] 2.4 God rays shader compiles (god_rays_cs.hlsl, 64-sample radial blur).
- [x] 2.5 Zero coverage: weather map R=0 after remap → density=0 → transmittance=1.0 → fully transparent.
- [x] 2.6 VolumetricCloudsComponent defaults: layerBottom=5km, height=10km, coverage=0.5, extinction=0.04, albedo=0.99 — Earth-like cumulus.
- [x] 2.7 CloudPass::Execute() enforces ordering: bake noise (once) → weather map → barrier → raymarch → barrier → temporal → barrier.
- [x] 2.8 Coverage: 5 shaders + 1 component + 1 C++ pass class. All shaders compile, C++ builds clean.

## 3. Documentation

- [x] 3.1 UE5 volumetric cloud architecture documented in docs/analysis/volumetric-clouds.md + volumetric-clouds-implementation-guide.md (2 files, full UE5 citations).
- [x] 3.2 Cloud parameters documented in VolumetricCloudsComponent with @source citations. Coverage (0-1), density multiplier, extinction (0.04), albedo (0.99), dual-lobe HG phase (0.5/-0.3/0.7), wind velocity, ray march steps (64/6).
