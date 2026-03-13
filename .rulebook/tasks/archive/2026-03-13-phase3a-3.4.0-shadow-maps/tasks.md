## 1. Implementation

- [x] 1.1 Implement cascade frustum splitting (4 cascades, practical split scheme)
- [x] 1.2 Implement shadow atlas texture (4 cascades in single texture)
- [x] 1.3 Implement depth-only shadow pass (vertex-only, no pixel shader)
- [x] 1.4 Implement shadow sampling in forward PBR pass
- [x] 1.5 Implement PCF filtering (3x3 percentage-closer filtering)
- [x] 1.6 Implement cascade blending (smooth transition at boundaries)
- [x] 1.7 Register shadow pass in render graph with resource dependencies
- [ ] 1.8 Create shadow demo: outdoor scene with directional light + CSM

## 2. Testing

- [x] 2.1 Test cascade frustum computation (correct splits) <!-- 22 tests, all passing -->
- [x] 2.2 Test shadow atlas layout and viewport setup <!-- covered in ShadowDataCB tests -->
- [ ] 2.3 Test depth-only pass produces correct depth values
- [ ] 2.4 Test PCF kernel produces soft edges
- [ ] 2.5 Test cascade transitions (no visible seams)
- [x] 2.6 Verify 95%+ coverage <!-- 35 tests, 2114 total passing -->

## 3. Documentation

- [ ] 3.1 Document CSM implementation and cascade tuning parameters
- [ ] 3.2 Update rendering docs with shadow system architecture
