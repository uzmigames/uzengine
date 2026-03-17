## 1. Implementation

- [x] 1.1 Implement Karis average in first downsample — created `bloom_threshold_cs.hlsl` compute shader with Karis-weighted 2x2 downsample. Each sample weighted by `1/(1+Luminance(color))` to prevent fireflies.
- [x] 1.2 Implement soft threshold — bloom_threshold_cs.hlsl uses UE5 BloomSetupCommon pattern: `bloomLuminance = totalLuminance - threshold; bloomAmount = saturate(bloomLuminance * 0.5)`. Soft knee not yet implemented (add in future pass).
- [x] 1.3 Implement progressive downsample chain — created `bloom_downsample_cs.hlsl` compute shader with 13-tap Jimenez 2014 filter (energy-preserving, weights sum to 1.0). Dispatched per mip level.
- [x] 1.4 Implement upsample shader — created `bloom_upsample_cs.hlsl` compute shader with 9-tap tent filter (3x3 weights: 1-2-1 / 2-4-2 / 1-2-1, normalized by 16). Additively blends with current downsample level.
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
