## 1. Implementation

- [ ] 1.1 Add shadingModelID to G-Buffer — pack 4-bit ID into RT2.A (or RT0.A upper nibble). Values: 0=Unlit, 1=DefaultLit, 2=Subsurface, 3=ClearCoat, 4=Cloth, 5=TwoSidedFoliage. Read in deferred lighting pass.
- [ ] 1.2 Add shadingModel parameter to PerObjectCB — `uint32_t shadingModel` (0-5). G-Buffer pass writes model ID per pixel.
- [ ] 1.3 Implement Unlit shading model — skip all lighting evaluation. Output = albedo × emissiveIntensity. No diffuse, no specular, no shadows. Simplest model.
- [ ] 1.4 Implement Subsurface shading model — wrap diffuse: `NdotL_wrap = (NdotL + wrap) / (1 + wrap)` with wrap=0.5. Add subsurface color contribution: `subsurfaceColor * saturate(-NdotL) * subsurfaceIntensity`. Simulates light scattering through thin/translucent materials. UE5: `SubsurfaceColor` stored in custom data G-Buffer channel.
- [ ] 1.5 Implement Clear Coat shading model — dual-layer specular: bottom layer = standard GGX at base roughness, top layer = GGX at coat roughness (typically 0.04-0.1). Coat Fresnel = Schlick with F0=0.04 (dielectric coating). Total specular = `coatFresnel * coatSpec + (1-coatFresnel) * baseSpec`. UE5 params: `ClearCoatRoughness` (0.1 default), `ClearCoat` (1.0 = full coating).
- [ ] 1.6 Implement Cloth shading model — replace GGX specular with Ashikhmin velvet BRDF: `D_Ashikhmin = (1 / (π * (1 + 4*α²))) * (sin(θ)^(-2/α² - 2) + cos(θ))`. Add sheen term: `sheenColor * pow(1-NdotV, 5)` for edge brightening (velvet look). UE5: `FuzzColor` for sheen, `Cloth` flag.
- [ ] 1.7 Implement Two-Sided Foliage shading model — transmittance: `backLighting = saturate(-NdotL) * subsurfaceColor * transmittanceStrength`. Light passes through thin surfaces (leaves). Combine front diffuse + back transmittance. UE5: uses `SubsurfaceColor` as transmittance tint.
- [x] 1.8 Update deferred lighting to branch on shading model — implemented shading model dispatch in deferred_lighting_ps.hlsl: RT3 load + smID decode, Unlit early-out (smID==0), Subsurface wrap diffuse + back-lighting (smID==2), ClearCoat dual-layer specular + IBL coat reflection (smID==4). DefaultLit (smID==1) unchanged. Cloth/Foliage pending.
- [ ] 1.9 Update forward pass with same model selection — mesh_ps.hlsl evaluates different BRDF based on shadingModel uniform.
- [ ] 1.10 Add custom data G-Buffer channel for model-specific params — subsurfaceColor (RGB), clearCoatRoughness (float), sheenColor (RGB). Pack into RT2 or dedicated RT3 (R8G8B8A8_UNORM).
- [ ] 1.11 Create shading models demo — 5 material balls in a row, each with different shading model: Default Lit, Subsurface (waxy), Clear Coat (car paint), Cloth (velvet), Foliage (leaf). Same lighting for comparison.

## 2. Visual Verification

- [ ] 2.1 Screenshot: Unlit material — flat color, no lighting response
- [ ] 2.2 Screenshot: Subsurface — soft wrap lighting, colored light bleeding through
- [ ] 2.3 Screenshot: Clear Coat — two specular highlights (base + glossy coat layer)
- [ ] 2.4 Screenshot: Cloth — soft velvet sheen at grazing angles
- [ ] 2.5 Screenshot: Foliage — backlit translucency when light is behind surface
- [ ] 2.6 Screenshot: all 5 models side-by-side for comparison
- [ ] 2.7 Regression: existing DefaultLit materials unchanged

## 3. Documentation

- [ ] 3.1 Document all shading models with UE5 ShadingModelID equivalents
- [ ] 3.2 Add per-model BRDF formulas and parameter reference
- [ ] 3.3 Document G-Buffer custom data channel layout per shading model
- [ ] 3.4 Add shading model selection guide for artists
