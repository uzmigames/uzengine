## 1. Implementation

- [ ] 1.1 Implement BRDF LUT compute shader (split-sum integration, 512x512 R16G16)
- [ ] 1.2 Implement irradiance convolution compute shader (hemisphere cosine-weighted sampling)
- [ ] 1.3 Implement prefiltered environment map compute shader (GGX importance sampling, 5 mip levels)
- [ ] 1.4 Create IBLBaker class to orchestrate bake pipeline (BRDF LUT + irradiance + prefilter)
- [ ] 1.5 Add HDR equirectangular-to-cubemap conversion shader
- [ ] 1.6 Add SkyLight data to PerFrameCB (irradiance SRV index, prefiltered SRV index, BRDF LUT SRV index)
- [ ] 1.7 Update mesh_ps.hlsl ambient section with IBL sampling (diffuse irradiance + specular prefiltered + BRDF LUT)
- [ ] 1.8 Implement fallback to constant ambient when no IBL cubemaps are bound
- [ ] 1.9 Create IBL demo (HDR environment map + metallic/dielectric spheres showcasing reflections)

## 2. Testing

- [ ] 2.1 Test BRDF LUT output dimensions and format (512x512, R16G16)
- [ ] 2.2 Test irradiance cubemap output (6 faces, correct resolution)
- [ ] 2.3 Test prefiltered cubemap mip chain (5 levels, decreasing resolution)
- [ ] 2.4 Test equirect-to-cubemap conversion (output is valid 6-face cubemap)
- [ ] 2.5 Test IBL fallback (no SkyLight → constant ambient, no visual regression)
- [ ] 2.6 Test SkyLight SRV indices in PerFrameCB (correct offsets and alignment)
- [ ] 2.7 Verify 95%+ coverage on all new code

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/06-lighting.md` with IBL pipeline and bake process
- [ ] 3.2 Add IBL tuning guide (environment map requirements, quality vs performance tradeoffs)
