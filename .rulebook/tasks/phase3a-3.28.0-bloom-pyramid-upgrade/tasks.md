## 1. Implementation

- [ ] 1.1 Implement Karis average in first downsample — modify `bloom_downsample_ps.hlsl` stage 0: weight each 2×2 sample by `1/(1+Luminance(color))` before averaging. Prevents bright specular fireflies from dominating bloom.
- [ ] 1.2 Implement soft threshold knee — replace hard `luma > threshold` with soft curve: `knee = threshold * 0.5; soft = clamp(luma - threshold + knee, 0, 2*knee); contribution = soft² / (4*knee + 1e-5)`. Produces smooth bloom falloff.
- [ ] 1.3 Implement 6-stage progressive downsample chain — each stage: 13-tap Gaussian downsample (or 4-tap bilinear with offset for performance). Output formats: RGBA16F at each mip. Stage 0=1/2, 1=1/4, 2=1/8, 3=1/16, 4=1/32, 5=1/64.
- [ ] 1.4 Implement upsample shader (`bloom_upsample_ps.hlsl`) — bilinear upsample from coarser mip + add current mip with per-stage weight. `output = upsample(coarser) + currentMip * stageWeight`. Uses tent filter (9-tap) for smooth upsample.
- [ ] 1.5 Implement 5-stage upsample chain — start from 1/64, upsample to 1/32 + add weighted 1/32 mip, continue up to 1/2. Final output = sum of all weighted stages at half-res.
- [ ] 1.6 Implement per-stage tint weights — 6× float3 tints matching UE5 defaults: (0.3465, 0.138, 0.1176, 0.066, 0.066, 0.061). Multiply each stage's contribution by its tint color before accumulation.
- [ ] 1.7 Update bloom composite — `bloom_composite_ps.hlsl` now receives upsampled pyramid result instead of single blurred mip. `output = scene + bloomPyramid * bloomIntensity`. Default intensity = 0.675 (UE5).
- [ ] 1.8 Update render graph bloom passes — replace old 3-pass (downsample→blur_h→blur_v→composite) with new: threshold+downsample[0] → downsample[1..5] → upsample[5..1] → composite. Total ~12 passes.
- [ ] 1.9 Add per-stage size and tint parameters to BloomPassDesc — bloomIntensity, bloomThreshold, bloomSizeScale, bloom[1-6]Size, bloom[1-6]Tint. All with UE5 default values.
- [ ] 1.10 Remove old single-mip bloom shaders — delete or archive bloom_blur_h_ps.hlsl and bloom_blur_v_ps.hlsl, replaced by pyramid system.
- [ ] 1.11 Create bloom comparison demo — bright light source: old bloom (narrow halo) vs new pyramid bloom (wide soft glow). Side-by-side or toggle. Show Karis average preventing fireflies on specular.

## 2. Visual Verification

- [ ] 2.1 Screenshot: new bloom — wide, soft glow halo around bright sources (100+ pixel radius)
- [ ] 2.2 Screenshot: old vs new bloom comparison — dramatic improvement visible
- [ ] 2.3 Screenshot: specular highlight — no firefly artifacts (Karis average working)
- [ ] 2.4 Screenshot: soft threshold — smooth bloom onset, no hard on/off line
- [ ] 2.5 Screenshot: bloom intensity=0 — clean image, no glow
- [ ] 2.6 Regression: postprocess_test demo still works

## 3. Documentation

- [ ] 3.1 Document bloom pyramid algorithm with UE5 parameter reference
- [ ] 3.2 Add per-stage tint and size tuning guide for artists
- [ ] 3.3 Document Karis average and soft threshold knee formulas
