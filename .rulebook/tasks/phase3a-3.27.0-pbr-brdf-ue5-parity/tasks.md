## 1. Implementation

- [ ] 1.1 Replace Lambertian with Burley diffuse in `pbr_common.hlsli` — implement `Diffuse_Burley(albedo, roughness, NdotV, NdotL, VdotH)`: `fd90 = 0.5 + 2.0 * VdotH² * roughness; lightScatter = 1 + (fd90-1)*(1-NdotL)⁵; viewScatter = 1 + (fd90-1)*(1-NdotV)⁵; return albedo * INV_PI * lightScatter * viewScatter`. Replace all `kD * albedo / PI` calls.
- [ ] 1.2 Replace Schlick-GGX geometry with Smith Joint GGX in `pbr_common.hlsli` — implement `Vis_SmithJointApprox(a2, NdotV, NdotL)`: `Vis_V = NdotL * (NdotV * (1-a) + a); Vis_L = NdotV * (NdotL * (1-a) + a); return 0.5 / (Vis_V + Vis_L)`. Returns combined D*G/(4*NdotV*NdotL) visibility term. Remove old `GeometrySmith` function.
- [ ] 1.3 Add multiscatter energy compensation — after single-scatter specular: read BRDF LUT (from IBL pipeline), compute `energyCompensation = 1.0 / (brdfLUT.x + brdfLUT.y) - 1.0`, multiply specular by `(1.0 + F0 * energyCompensation)`. If BRDF LUT not available, use fitted approximation: `compensation ≈ 1.0 + roughness * (1/(1-F0) - 1)`.
- [ ] 1.4 Update forward PBR pixel shader (`mesh_ps.hlsl`) — replace all BRDF evaluation calls with new functions. Ensure Cook-Torrance assembly uses Vis_SmithJointApprox (returns Vis term already divided by 4*NdotV*NdotL).
- [ ] 1.5 Update deferred lighting pixel shader (`deferred_lighting_ps.hlsl`) — same BRDF replacement as forward pass. Both paths must use identical BRDF for visual consistency.
- [ ] 1.6 Upgrade ACES tonemap to 3-segment piecewise curve — implement UE5's `FilmToneMap` with configurable Slope (0.91), Toe (0.53), Shoulder (0.23), BlackClip (0.0), WhiteClip (0.04). Add these as tonemap CB parameters. Keep Narkowicz as fallback option.
- [ ] 1.7 Implement proper sRGB transfer function — replace `pow(color, 1/2.2)` with correct sRGB curve: `sRGB = (linear <= 0.0031308) ? 12.92 * linear : 1.055 * pow(linear, 1/2.4) - 0.055`. UE5 uses this instead of gamma 2.2 approximation.
- [ ] 1.8 Add BRDF comparison debug mode — shader define that renders split-screen: left=old BRDF (Lambertian + Schlick-GGX), right=new BRDF (Burley + Smith Joint). For visual verification.
- [ ] 1.9 Create PBR parity demo — material ball with roughness gradient (0.0 → 1.0), metallic on/off, multiple view angles. Screenshot for comparison with UE5 default lit material.

## 2. Visual Verification

- [ ] 2.1 Screenshot: rough sphere at grazing angle — Burley shows brighter rim than Lambertian was
- [ ] 2.2 Screenshot: roughness=1.0 metal sphere — visibly brighter with multiscatter (energy conservation)
- [ ] 2.3 Screenshot: roughness gradient (0→1) — smooth transition, no banding, matches UE5 reference
- [ ] 2.4 Screenshot: split-screen old vs new BRDF — visible improvement at grazing angles
- [ ] 2.5 Screenshot: 3-segment ACES vs Narkowicz — compare toe/shoulder behavior
- [ ] 2.6 Regression: pbr_test, deferred_test demos still render correctly (improved, not broken)

## 3. Documentation

- [ ] 3.1 Document BRDF formulas with UE5 source file references (ShadingModels.ush, BRDF.ush)
- [ ] 3.2 Add Burley vs Lambertian visual comparison guide
- [ ] 3.3 Document multiscatter compensation formula and when BRDF LUT is required
- [ ] 3.4 Document 3-segment ACES parameters and artistic tuning guide
