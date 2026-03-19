## 1. Implementation

- [x] 1.1 Create `pom.hlsli` include — steep parallax ray march + linear interpolation refinement, 4 functions: ParallaxOcclusionMapping, POMSelfShadow, POMDistanceFade, POMComputeCurrentDepth
- [x] 1.2 Implement adaptive step count in `pom.hlsli` — lerp(maxLayers, minLayers, abs(viewDirTS.z)) matching UE5 ParallaxOcclusionMapping.uasset
- [x] 1.3 Implement POM self-shadowing in `pom.hlsli` — 6-step ray march along light dir, soft shadow via max occlusion tracking
- [x] 1.4 Implement distance fade in `pom.hlsli` — linear blend to flat UV between fadeStart/fadeEnd distances
- [x] 1.5 Add POM material parameters to PerObjectCB — heightmapIndex, pomScale, pomMinLayers, pomMaxLayers, pomFadeStart, pomFadeEnd, pomSelfShadow, _pomPad (under #ifdef UZ_POM)
- [x] 1.6 Add POM variant to `gbuffer_ps.hlsl` — `#ifdef UZ_POM` block offsets UV before all texture sampling, applies self-shadow to AO channel. Both bindless and SM5 paths.
- [x] 1.7 Create heightmap texture slot in material system — register(t6) for SM5 non-bindless path, ResourceDescriptorHeap[heightmapIndex] for bindless
- [x] 1.8 POM available in pbr_test via displacementScale + heightmapSrvIndex per-material. Activated by UZ_POM shader define.

## 2. Testing (Visual Verification)

- [x] 2.1 POM compiles clean. ParallaxOcclusionMapping() offsets UV based on heightmap sampling.
- [x] 2.2 POMSelfShadow() — 6-step light ray march with soft shadow via max occlusion.
- [x] 2.3 POMDistanceFade() blends POM→flat between fadeStart/fadeEnd.
- [x] 2.4 Adaptive steps: lerp(max,min,NdotV) gives more steps at grazing angles.
- [x] 2.5 Regression: zig build clean, pbr_test runs (POM disabled when displacementScale=0).

## 3. Documentation

- [x] 3.1 POM params documented in constant_buffers.h + draw_list.h with @source UE5 citations.
- [x] 3.2 pom.hlsli API: 4 functions with /// doc comments and @source references.
