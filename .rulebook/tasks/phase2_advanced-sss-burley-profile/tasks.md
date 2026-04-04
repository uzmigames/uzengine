## 1. Implementation

### 1.1 Diffusion Profile
- [ ] 1.1.1 Implement `BurleyDiffusionProfile(float r, float d)` in `subsurface.hlsli`: `R(r) = A*exp(-r/d) + B*exp(-r/(3d))` with proper normalization. Add `@ref Christensen & Burley 2015 "Approximate Reflectance Profiles for Efficient SSS"`.
- [ ] 1.1.2 Create `SubsurfaceProfile` struct: `float3 scatterColor`, `float scatterRadius`, `float3 transmittanceTint`, `float falloff`. Store in StructuredBuffer accessible by material ID.
- [ ] 1.1.3 Pre-compute kernel weights for 11-tap separable blur from the diffusion profile (uploaded as CB per profile).

### 1.2 SSS Blur Pass
- [ ] 1.2.1 Create `SSSBlurPass` class in `passes/postprocess/` — two-pass separable blur (horizontal then vertical).
- [ ] 1.2.2 Create `sss_blur_h_cs.hlsl` and `sss_blur_v_cs.hlsl` — screen-space Gaussian-weighted blur using kernel weights from the profile. Only blur pixels flagged as SSS in GBuffer stencil.
- [ ] 1.2.3 Use stencil mask to skip non-SSS pixels (avoid blurring the entire screen).
- [ ] 1.2.4 Depth-aware blur: attenuate kernel weight based on depth difference to prevent bleeding across edges.

### 1.3 Transmittance
- [ ] 1.3.1 Implement `TransmittanceBurley(float thickness, float3 transmittanceTint)` for back-lit thin features (ears, fingers). Sample shadow map depth to estimate local thickness.
- [ ] 1.3.2 Apply transmittance in deferred lighting for SSS materials: add transmitted light contribution modulated by diffusion profile.

### 1.4 Render Graph Integration
- [ ] 1.4.1 Register SSSBlurPass in render graph: after deferred lighting, before tonemapping.
- [ ] 1.4.2 Add GBuffer stencil bit for SSS pixels (set during GBuffer fill for SSS materials).

## 2. Testing

### 2.1 CPU Math Parity (Tier 1)
- [ ] 2.1.1 Known-value test: `BurleyDiffusionProfile(0.5, 1.0)` matches published values within 1e-4.
- [ ] 2.1.2 Property test: profile integrates to 1.0 over infinite radius (energy conservation).
- [ ] 2.1.3 Boundary test: r=0 (peak) and large r (approaches zero).

### 2.2 Visual Regression (Tier 2)
- [ ] 2.2.1 Render skin-textured head mesh with SSS profile. Verify red translucency at ears and nose.
- [ ] 2.2.2 Compare SSS ON vs OFF — verify visible difference in thin features.
- [ ] 2.2.3 Verify no blur bleeding across depth discontinuities (face edge against background).
- [ ] 2.2.4 Compare against UE5 SSS reference screenshot. Target visual similarity.

### 2.3 Performance
- [ ] 2.3.1 Benchmark SSS blur pass: target < 0.5ms GPU at 1080p.

### 2.4 Coverage
- [ ] 2.4.1 Verify 95%+ coverage on all new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/shader-library.md` with Burley diffusion profile functions.
- [ ] 3.2 Update `docs/specs/rendering/renderer.md` with SSS blur pass pipeline description.
- [ ] 3.3 Add `@ref Christensen & Burley 2015` citations.
