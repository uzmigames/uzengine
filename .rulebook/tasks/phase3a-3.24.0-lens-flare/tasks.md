## 1. Implementation

- [x] 1.1 Implement ghost generation shader (`lensflare_cs.hlsl`) — compute shader with 5 ghost scales (0.3, 0.5, 0.8, 1.2, 1.5), UV reflection through center, per-ghost rainbow tint, edge attenuation via smoothstep radial falloff. Bindless ResourceDescriptorHeap access. 8x8 thread groups.
- [x] 1.2 Implement chromatic fringe per ghost — SampleChromatic() offsets R/G/B UVs along radial direction by (1-shift, 1, 1+shift) factors. chromaticShift parameter controls spread (default 0.02).
- [x] 1.3 Implement halo ring — 3-sample Gaussian blur along ring tangent at haloRadius from center. Weights (0.4732, 0.2634) from sigma=0.65 Gaussian. Chromatic aberration at half strength on halo.
- [ ] 1.4 Implement starburst pattern (optional) — modulate ghosts by radial starburst texture (precomputed 1D angular pattern). Rotates with camera. Creates star-shaped streaks on flares.
- [x] 1.5 Implement distance attenuation — EdgeAttenuation() uses smoothstep(1.0, 0.5, length(uv-0.5)*2) for radial falloff. Applied to both ghosts and halo samples.
- [ ] 1.6 Composite lens flare — additive blend all ghosts + halo onto scene: `output = sceneColor + flareColor * intensity * tint`. Uses bloom threshold as input (no new threshold pass needed).
- [ ] 1.7 Add LensFlareDesc to PostProcessStack — enabled/disabled, intensity, tint, threshold (reuses bloom threshold), ghostCount, haloRadius, haloWidth, starburstEnabled.
- [ ] 1.8 Register lens flare pass in render graph — after bloom threshold (reuse texture), before bloom composite. Additive blend into bloom output.
- [ ] 1.9 Create lens flare demo — bright directional light, camera looking toward it. Show ghosts, halo, and starburst. Toggle on/off, intensity slider.

## 2. Visual Verification

- [ ] 2.1 Screenshot: bright light with visible ghost reflections — multiple colored ghosts through center
- [ ] 2.2 Screenshot: chromatic fringe on ghosts — rainbow edge visible
- [ ] 2.3 Screenshot: halo ring around bright source
- [ ] 2.4 Screenshot: lens flare OFF — clean image, no ghosts
- [ ] 2.5 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document lens flare parameters with UE5 equivalents
- [ ] 3.2 Add ghost generation algorithm overview

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
