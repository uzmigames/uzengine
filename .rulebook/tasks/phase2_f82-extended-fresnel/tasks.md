## 1. Implementation

- [ ] 1.1 Add `F_AdobeF82(float3 F0, float3 F82, float VoH)` to `brdf.hlsli`: `Fc = Pow5(1 - VoH); K = 49.0/46656.0; b = (K - K*F82) * (7776 + 9031*F0); return saturate(F0 + Fc * ((1-F0) - b * (VoH - VoH*VoH)))`. Add `@ref Adobe Standard Material Specification`.
- [ ] 1.2 Add `float3 f82Tint = {1,1,1}` to `PBRMaterial` struct. When f82Tint != (1,1,1), use `F_AdobeF82` instead of `F_Schlick` in BRDF evaluation.
- [ ] 1.3 Add C++ mirror `F_AdobeF82(Vec3, Vec3, float)` in `brdf_math.h`.

## 2. Testing

### 2.1 CPU Math Parity (Tier 1)
- [ ] 2.1.1 Known-value test: `F_AdobeF82({0.95,0.64,0.54}, {0.96,0.7,0.5}, 0.3)` for copper — verify within 1e-4 of reference.
- [ ] 2.1.2 Property test: when F82 = (1,1,1), output must match `F_Schlick(F0, VoH)` exactly (backwards compatible).
- [ ] 2.1.3 Boundary test: VoH=0 (grazing, should approach 1) and VoH=1 (normal, should return F0).
- [ ] 2.1.4 Property test: output always in [0, 1] for valid F0/F82 in [0, 1].

### 2.2 Visual Regression (Tier 2)
- [ ] 2.2.1 Render copper sphere with F82 vs standard Schlick — verify visible difference at mid-angle.
- [ ] 2.2.2 Verify default materials (f82Tint=1,1,1) produce identical output (PSNR >= 40 dB).

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/shader-library.md` with `F_AdobeF82()` function signature and citation.
