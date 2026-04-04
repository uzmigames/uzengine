## 1. Implementation

### 1.1 Thin-Film Fresnel
- [ ] 1.1.1 Create `iridescence.hlsli` with `EvalIridescence(float outsideIOR, float iridescenceIOR, float filmThickness, float cosTheta)` returning modified `float3 F0`. Add `@ref Belcour & Barla 2017 "A Practical Extension to Microfacet Theory for the Modeling of Varying Iridescence"`.
- [ ] 1.1.2 Implement Airy summation approximation: compute OPD (optical path difference), evaluate thin-film reflectance for R/G/B wavelengths (380nm, 530nm, 620nm approximation).
- [ ] 1.1.3 Add iridescence thickness texture support (optional) for per-pixel thickness variation.

### 1.2 Material Integration
- [ ] 1.2.1 Add `float iridescence = 0.0f`, `float iridescenceIOR = 1.3f`, `float iridescenceThicknessMin = 100.0f`, `float iridescenceThicknessMax = 400.0f` to `PBRMaterial`.
- [ ] 1.2.2 Add `SHADING_MODEL_IRIDESCENT` variant to `standard_material.hlsli` — when active, compute iridescent F0 and use instead of standard Schlick F0.
- [ ] 1.2.3 Upload iridescence params to material StructuredBuffer.

### 1.3 C++ Math Mirror
- [ ] 1.3.1 Add C++ `EvalIridescence()` in `brdf_math.h` for parity tests.

## 2. Testing

### 2.1 CPU Math Parity (Tier 1)
- [ ] 2.1.1 Known-value test: `EvalIridescence(1.0, 1.3, 300.0, 0.5)` matches Three.js reference output within 1e-3.
- [ ] 2.1.2 Property test: when `iridescence = 0`, output matches standard Schlick F0 exactly.
- [ ] 2.1.3 Property test: output F0 components always in [0, 1].
- [ ] 2.1.4 Boundary test: `cosTheta = 0` (grazing) and `cosTheta = 1` (normal incidence) produce valid results.

### 2.2 Visual Regression (Tier 2)
- [ ] 2.2.1 Create test sphere with iridescence (thickness=300nm, IOR=1.3). Verify rainbow color shift at grazing angles.
- [ ] 2.2.2 Verify iridescence=0 produces identical output to standard material (PSNR >= 40 dB).

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/shader-library.md` with `iridescence.hlsli` functions.
- [ ] 3.2 Update `docs/specs/rendering/shader-variants.md` with `SHADING_MODEL_IRIDESCENT`.
