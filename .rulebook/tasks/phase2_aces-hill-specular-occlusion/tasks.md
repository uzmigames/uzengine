## 1. Implementation

### 1.1 Stephen Hill ACES Fit
- [x] 1.1.1 Added `ACESHillFit()` to `color.hlsli` with Hill 2016 fitted RRT+ODT using 3x3 input matrix (ACESInputMat) and output matrix (ACESOutputMat). Cited `@ref Hill 2016 "ACES Fitted Curves"`. tonemap_ps.hlsl now calls ACESHillFit instead of ACESFilmic.
- [x] 1.1.2 Narkowicz fit kept as `ACESFilmic()` in color.hlsli for A/B comparison.
- [x] 1.1.3 Added C++ mirrors `ACESHillFit(Vec3)` and `ACESNarkowicz(Vec3)` in `brdf_math.h`.

### 1.2 Specular Occlusion
- [x] 1.2.1 `ComputeSpecularOcclusion()` already existed inline in `deferred_lighting_ps.hlsl:348`. Extracted to `ibl.hlsli` as named function with `@ref Lagarde 2014 "Moving Frostbite to PBR"`.
- [x] 1.2.2 Specular occlusion already applied in `deferred_lighting_ps.hlsl:366`: `specularAmbient * specOcclusion`.
- [x] 1.2.3 Added C++ mirror `ComputeSpecularOcclusion(float, float, float)` in `brdf_math.h`.

## 2. Testing

### 2.1 CPU Math Parity (Tier 1)
- [ ] 2.1.1 Add known-value test for `AcesHillFit()`: input (1.0, 0.5, 0.2) must match Hill's published reference output within 1e-4.
- [ ] 2.1.2 Add known-value test for `ComputeSpecularOcclusion()`: NdotV=0.5, AO=0.3, roughness=0.5 → verify against hand-computed result.
- [ ] 2.1.3 Add boundary tests: AO=0 → specular occlusion near 0; AO=1 → specular occlusion = 1; roughness=0 and roughness=1 edge cases.
- [ ] 2.1.4 Property test: specular occlusion always in [0, 1] for all valid inputs.

### 2.2 Visual Regression (Tier 2)
- [ ] 2.2.1 Capture before/after screenshots with damaged_helmet scene (Hill vs Narkowicz ACES). Verify no hue shift in orange/red emissive highlights.
- [ ] 2.2.2 Capture before/after with specular occlusion ON/OFF in a scene with crevices (PBR sphere on ground plane). Verify reduced light leaking at contact edges.
- [ ] 2.2.3 PSNR comparison: Hill vs Falcor full ACES on same HDR input. Target >= 35 dB.

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on all new C++ code in `brdf_math.h`.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/shader-library.md` with `AcesHillFit()` and `ComputeSpecularOcclusion()` function signatures and citations.
- [ ] 3.2 Add `@ref` citations to HLSL file headers: Hill 2016, Lagarde 2014.
