## 1. Implementation

- [ ] 1.1 Implement chromatic aberration in final composite shader — compute radial direction: `float2 dir = normalize(uv - 0.5); float d = length(uv - 0.5);`. Compute offset: `float2 offset = dir * intensity * d * d * texelSize;`. Sample: `R = scene(uv - offset).r; G = scene(uv).g; B = scene(uv + offset).b;`.
- [ ] 1.2 Implement max offset clamp — clamp offset to `maxSize` percent of screen width to prevent extreme artifacts. UE5 default maxSize=5.0%.
- [ ] 1.3 Implement start distance — CA only applies when `d > startOffset` (0.5 default). Below this distance, no channel separation. Smooth transition via lerp.
- [ ] 1.4 Add ChromaticAberrationDesc to PostProcessStack — enabled/disabled, intensity (0.0 UE5 default = off, 1.0-5.0 visible), maxSize, startOffset.
- [ ] 1.5 Create chromatic aberration demo — toggle on/off, intensity slider, zoom view showing R/B fringe at edges.

## 2. Visual Verification

- [ ] 2.1 Screenshot: CA at intensity=3.0 — visible RGB fringe at edges
- [ ] 2.2 Screenshot: CA at intensity=0.0 — no effect
- [ ] 2.3 Screenshot: zoom on screen edge — clear R/B channel split
- [ ] 2.4 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document chromatic aberration parameters with UE5 SceneFringe equivalents
