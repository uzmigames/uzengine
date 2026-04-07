## 1. Implementation

- [ ] 1.1 Extend `BlockerSearch()` in `shadow_pcss.hlsli` to accept atlas region UV bounds (min/max) so it works within a sub-region of the shadow atlas (point/spot light tile).
- [ ] 1.2 Implement `SampleShadowPCSS_PointLight()` — blocker search in the point light's atlas face, compute penumbra width from blocker distance and light radius, apply variable-kernel PCF.
- [ ] 1.3 Implement `SampleShadowPCSS_SpotLight()` — same approach for spot light perspective shadow with cone-based atlas region.
- [ ] 1.4 In `deferred_lighting_ps.hlsl`, replace point/spot PCF calls with PCSS when `shadowPCSSEnabled` is set in the light CB.
- [ ] 1.5 Add per-light `softShadowRadius` parameter to LightComponent and LightCB (controls penumbra size).
- [ ] 1.6 Add CVar `r.ShadowPCSS.PointSpot` (0=PCF only, 1=PCSS, default=1).

## 2. Testing

### 2.1 Visual Regression (Tier 2)
- [ ] 2.1.1 Point light near a wall with objects at varying distances — verify shadow softness increases with distance from caster.
- [ ] 2.1.2 Spot light with cone shadow — verify correct penumbra along cone edges.
- [ ] 2.1.3 Compare PCSS ON vs OFF — verify no artifacts, no light leaking at shadow edges.
- [ ] 2.1.4 Verify shadow quality at atlas boundary (no sampling outside the light's atlas tile).

### 2.2 Performance
- [ ] 2.2.1 Benchmark PCSS vs PCF for point/spot: target < 0.5ms additional GPU cost per light at 1080p.

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/shader-library.md` with PCSS point/spot functions.
- [ ] 3.2 Update `docs/specs/rendering/point-spot-light-shadows.md` with PCSS support description.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
