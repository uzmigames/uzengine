## 1. Implementation

- [ ] 1.1 Implement ACES filmic tone mapping shader (HDR → LDR)
- [ ] 1.2 Implement bloom downsample mip chain (progressive half-res)
- [ ] 1.3 Implement Gaussian blur passes (horizontal + vertical separable)
- [ ] 1.4 Implement bloom composite (additive blend with scene)
- [ ] 1.5 Implement FXAA shader (edge detection + blend)
- [ ] 1.6 Implement post-process stack ordering via render graph
- [ ] 1.7 Add enable/disable toggles for each effect
- [ ] 1.8 Create post-process demo: HDR scene with bloom + tonemap + FXAA

## 2. Testing

- [ ] 2.1 Test tone mapping output range (0-1 for all HDR inputs)
- [ ] 2.2 Test bloom mip chain dimensions (correct half-res at each level)
- [ ] 2.3 Test FXAA shader compilation
- [ ] 2.4 Test effect toggle (disabled → pass culled by render graph)
- [ ] 2.5 Verify 95%+ coverage

## 3. Documentation

- [ ] 3.1 Document post-processing stack architecture
- [ ] 3.2 Add per-effect tuning guide (bloom threshold, FXAA quality)
