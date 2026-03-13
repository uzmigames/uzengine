## 1. Implementation

- [ ] 1.1 Implement GTAO shader (horizon-angle search, half-res generation)
- [ ] 1.2 Implement GTAO temporal filtering and bilateral blur upsampling
- [ ] 1.3 Implement Hi-Z depth mip chain generation shader (min-depth reduction)
- [ ] 1.4 Implement SSR shader (Hi-Z ray march, binary refinement, Fresnel weighting)
- [ ] 1.5 Implement velocity buffer generation (depth reprojection from previous frame)
- [ ] 1.6 Implement TAA resolve shader (Halton jitter, YCoCg clamping, history blend)
- [ ] 1.7 Implement DoF CoC computation shader (thin-lens equation)
- [ ] 1.8 Implement DoF separable blur shader (CoC-weighted Gaussian)
- [ ] 1.9 Implement motion blur shader (velocity-weighted directional blur)
- [ ] 1.10 Implement auto-exposure shader (luminance histogram, eye adaptation)
- [ ] 1.11 Implement color grading shader (temperature, tint, saturation, contrast)
- [ ] 1.12 Implement contact shadows shader (short-range ray march along light direction)
- [ ] 1.13 Implement screen effects in composite pass (film grain, vignette, chromatic aberration, CAS)
- [ ] 1.14 Implement PostProcessVolume data structure (AABB, priority, blend weight, effect parameters)
- [ ] 1.15 Implement PostProcessVolume blending (camera-distance interpolation of stacked volumes)
- [ ] 1.16 Register all new passes in render graph with correct ordering
- [ ] 1.17 Create advanced post-processing demo (all effects active, toggleable via UI)

## 2. Testing

- [ ] 2.1 Test GTAO output range (0-1 occlusion values)
- [ ] 2.2 Test Hi-Z mip chain dimensions (correct halving per level)
- [ ] 2.3 Test SSR shader compilation (SM6.6 and SM5)
- [ ] 2.4 Test TAA jitter sequence (8-sample Halton, correct sub-pixel offsets)
- [ ] 2.5 Test DoF CoC computation (correct sign for near/far, zero at focus distance)
- [ ] 2.6 Test motion blur velocity magnitude clamping
- [ ] 2.7 Test auto-exposure convergence (adapts toward target EV over time)
- [ ] 2.8 Test PostProcessVolume blending (correct interpolation between overlapping volumes)
- [ ] 2.9 Test effect toggles (each effect individually disableable, pass culled when off)
- [ ] 2.10 Test render graph pass ordering (SSAO before lighting, TAA after tonemap)
- [ ] 2.11 Verify 95%+ coverage on all new code

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/07-post-processing.md` with all implemented effects
- [ ] 3.2 Add PostProcessVolume configuration and tuning guide
- [ ] 3.3 Add per-effect quality scalability reference (Off/Low/Medium/High/Ultra)
