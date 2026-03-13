## 1. Implementation

- [ ] 1.1 Create `pom.hlsli` include — steep parallax ray march + binary refinement (3 iterations)
- [ ] 1.2 Implement adaptive step count in `pom.hlsli` — lerp(minSteps, maxSteps) based on dot(N, V)
- [ ] 1.3 Implement POM self-shadowing in `pom.hlsli` — ray march along light dir through heightmap
- [ ] 1.4 Implement distance fade in `pom.hlsli` — smooth blend to flat UV at configurable distance
- [ ] 1.5 Add POM material parameters to PerObjectCB — displacementScale, pomMinLayers, pomMaxLayers, pomSelfShadow, pomFadeStart, pomFadeEnd
- [ ] 1.6 Add POM variant to `mesh_ps.hlsl` — `#ifdef UZ_POM` enables heightmap sampling + UV offset
- [ ] 1.7 Create heightmap texture slot in material system — register(t6) or next available SRV slot
- [ ] 1.8 Create POM demo — textured cubes with brick/cobblestone heightmaps, camera orbit, screenshot

## 2. Testing (Visual Verification)

- [ ] 2.1 POM demo screenshot — flat vs POM comparison (clear depth parallax visible)
- [ ] 2.2 Self-shadow screenshot — grazing light angle shows contact shadows in crevices
- [ ] 2.3 Distance fade screenshot — close-up POM vs distant flat transition
- [ ] 2.4 View angle test — head-on vs grazing angle shows correct parallax shift
- [ ] 2.5 Regression — existing pbr_test and deferred_test demos still work

## 3. Documentation

- [ ] 3.1 Document POM parameters in material system docs
- [ ] 3.2 Add `pom.hlsli` API reference to shader library docs
