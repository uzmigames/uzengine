## 1. Implementation

- [ ] 1.1 Implement `AgXTonemap(float3 linearColor)` in `tonemap_ps.hlsl`: AgX log encoding → 6th-order polynomial approximation → sRGB output. Add `@ref Sobotka 2023 "AgX"`.
- [ ] 1.2 Implement optional "Punchy" look transform: slight S-curve contrast + saturation boost applied after AgX base curve.
- [ ] 1.3 Add `TONEMAP_AGX` (value 4) and `TONEMAP_AGX_PUNCHY` (value 5) to the tonemap operator enum.
- [ ] 1.4 Wire CVar `r.TonemapOperator` to support AgX selection (0=ACES, 1=Filmic, 2=Reinhard, 3=Linear, 4=AgX, 5=AgX Punchy).
- [ ] 1.5 Add C++ mirror `AgXTonemap(float3)` in `brdf_math.h` for parity tests.

## 2. Testing

### 2.1 CPU Math Parity (Tier 1)
- [ ] 2.1.1 Known-value test: `AgXTonemap(1.0, 0.5, 0.1)` matches Three.js reference output within 1e-3.
- [ ] 2.1.2 Boundary test: pure black (0,0,0) → (0,0,0); extreme HDR (100,50,10) → saturated but hue-preserved.
- [ ] 2.1.3 Property test: output always in [0, 1] for non-negative input.

### 2.2 Visual Regression (Tier 2)
- [ ] 2.2.1 Capture scene with saturated fire/emissive (orange, red). Compare AgX vs ACES — verify AgX preserves hue better.
- [ ] 2.2.2 Capture same scene with all 5 operators for A/B comparison gallery.
- [ ] 2.2.3 PSNR comparison AgX vs ACES on same HDR input (expected to differ — this is a quality check, not a regression).

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/shader-library.md` with `AgXTonemap()` function and operator enum values.
- [ ] 3.2 Add tone mapping comparison guide to `docs/guides/shader-authoring.md` showing when to use each operator.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
