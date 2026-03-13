## 1. Implementation

- [x] 1.1 Implement ACES filmic tone mapping shader (HDR → LDR) — tonemap_ps.hlsl with parametric intensity + exposure
- [x] 1.2 Implement bloom downsample mip chain (progressive half-res) — bloom_downsample_ps.hlsl with threshold isolation
- [x] 1.3 Implement Gaussian blur passes (horizontal + vertical separable) — bloom_blur_h_ps.hlsl + bloom_blur_v_ps.hlsl (5-tap kernels)
- [x] 1.4 Implement bloom composite (additive blend with scene) — bloom_composite_ps.hlsl with intensity control
- [x] 1.5 Implement FXAA shader (edge detection + blend) — fxaa_ps.hlsl with luminance-based edge detection
- [x] 1.6 Implement post-process stack ordering via render graph — postprocess_stack.h with AddToneMapPass/AddBloomPass/AddFXAAPass/AddPostProcessStack
- [x] 1.7 Add enable/disable toggles for each effect — ToneMapPassDesc/BloomPassDesc/FXAAPassDesc all have 'enabled' boolean
- [ ] 1.8 Create post-process demo: HDR scene with bloom + tonemap + FXAA — pending C++ implementation of pass registration

## 2. Testing

- [ ] 2.1 Test tone mapping output range (0-1 for all HDR inputs) — pending: unit tests for ACESTonemapFast
- [ ] 2.2 Test bloom mip chain dimensions (correct half-res at each level) — pending: render graph integration tests
- [ ] 2.3 Test FXAA shader compilation — pending: integration test (FXAAPassDesc compilation)
- [ ] 2.4 Test effect toggle (disabled → pass culled by render graph) — pending: render graph skip logic
- [ ] 2.5 Verify 95%+ coverage — pending: complete test implementation

## 3. Documentation

- [x] 3.1 Document post-processing stack architecture — postprocess_stack.h with complete API docs, config structs, pass descriptions
- [x] 3.2 Add per-effect tuning guide (bloom threshold, FXAA quality) — all shader files include parameter ranges and tuning guidance
