## 1. Implementation
- [ ] 1.1 Fix sun intensity: change `intensity` from 80 to 10 in `sample/content/scenes/sponza.uzscene`
- [ ] 1.2 Fix skyTint double-multiply: remove `* skyLightIntensity` from line 398 in `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`
- [ ] 1.3 Neutralize sky_light color: set `light_color` to `[1.0, 1.0, 1.0]` in `sponza.uzscene`
- [ ] 1.4 Remove dead CVars: delete `r.Tonemap.Slope` and `r.Tonemap.Toe` from `sponza.uzscene`
- [ ] 1.5 Build runtime and verify no compilation errors
- [ ] 1.6 Screenshot at default camera position — verify sun shafts visible on floor
- [ ] 1.7 Screenshot at camera-z 700 — verify contrast holds at different angle
- [ ] 1.8 Compare before/after contrast ratio (pre-tonemap HDR sun-lit vs shadow should be >3:1 post-ACES)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
