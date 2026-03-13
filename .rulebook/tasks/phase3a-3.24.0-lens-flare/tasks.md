## 1. Implementation

- [ ] 1.1 Implement ghost generation shader (`lensflare_ps.hlsl`) — for each of 5 ghost scales (0.3, 0.5, 0.8, 1.2, 1.5): reflect UV through center `ghostUV = float2(0.5, 0.5) + (float2(0.5, 0.5) - uv) * scale`. Sample bloom threshold texture at ghostUV. Tint each ghost with per-ghost color (rainbow gradient: violet → blue → green → yellow → red).
- [ ] 1.2 Implement chromatic fringe per ghost — sample R/G/B at slightly different ghost scales: `R = threshold(ghostUV * 0.98).r; G = threshold(ghostUV).g; B = threshold(ghostUV * 1.02).b`. Creates rainbow edge on each ghost.
- [ ] 1.3 Implement halo ring — sample bloom threshold at `normalize(uv - 0.5) * haloRadius + 0.5`. Apply Gaussian blur along ring direction. Width controlled by haloWidth parameter. Creates ring artifact around bright sources.
- [ ] 1.4 Implement starburst pattern (optional) — modulate ghosts by radial starburst texture (precomputed 1D angular pattern). Rotates with camera. Creates star-shaped streaks on flares.
- [ ] 1.5 Implement distance attenuation — ghosts falling outside [0,1] UV range are faded `weight *= smoothstep(0.0, 0.1, min(ghostUV.x, 1-ghostUV.x)) * smoothstep(0.0, 0.1, min(ghostUV.y, 1-ghostUV.y))`. Prevents hard cutoff at screen edges.
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
