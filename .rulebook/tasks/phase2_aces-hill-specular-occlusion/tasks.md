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
- [x] 2.1.1 `ACESHillFit.PublishedMatricesKnownValue` in `test_brdf_parity.cpp`: shipped symbol `ACESHillFit`, input (1, 0.5, 0.2), independently evaluated Hill matrices output (0.636295744, 0.384356474, 0.162757709), tolerance 1e-4.
- [x] 2.1.2 `SpecularOcclusion.KnownValue`: (0.5, 0.3, 0.5) gives `pow(0.8, 1/512) - 0.7 = 0.299564267710`, tolerance 1e-6.
- [x] 2.1.3 `SpecularOcclusion.AoBoundaries` sweeps view/roughness endpoints and interiors (33 x 33); `RoughnessExtremes` checks roughness 0 and 1 against 0.194427191 and 0.299998298 (1e-6). Hill black/HDR saturation is also checked.
- [x] 2.1.4 `SpecularOcclusion.ValidInputSweepStaysFiniteAndSaturated`: all 35,937 points on the 33 x 33 x 33 valid-input grid stay finite and in [0, 1]. This samples the domain; it is not an exhaustive continuum proof.

### 2.2 Visual Regression (Tier 2)
- [ ] 2.2.1 Capture before/after screenshots with damaged_helmet scene (Hill vs Narkowicz ACES). Verify no hue shift in orange/red emissive highlights. Deferred: needs Fable/GPU lane.
- [ ] 2.2.2 Capture before/after with specular occlusion ON/OFF in a scene with crevices (PBR sphere on ground plane). Verify reduced light leaking at contact edges. Deferred: needs Fable/GPU lane.
- [ ] 2.2.3 PSNR comparison: Hill vs Falcor full ACES on same HDR input. Target >= 35 dB. Deferred: needs Fable/GPU lane.

Items 2.2.1, 2.2.2 and 2.2.3 need Fable/GPU lane. This CPU-only lane
does not run the viewer, render, or capture screenshots; neither the visual
comparisons nor Falcor PSNR can be established by CPU mirror tests.

### 2.3 Coverage
- [x] 2.3.1 CPU source line accounting: 23/23 behavior-bearing physical lines
      covered (100%) across the phase's three CPU functions. `ACESHillFit`:
      18/18 (480-483, 486-489, 492, 495-500, 502-503, 505);
      `ACESNarkowicz`: 4/4 (512-514, 516), exercised by
      `ACESNarkowicz.RetainedComparisonFit`; `ComputeSpecularOcclusion`: 1/1
      (529), including both saturation endpoints. Excludes signatures, braces,
      comments and blank lines. This is source accounting, not instrumented
      coverage; the repository has no coverage tool.

## 3. Documentation
- [x] 3.1 `docs/specs/rendering/shader-library.md` documents shipped `ACESHillFit()` and `ComputeSpecularOcclusion()` signatures, spaces/ranges, formula, CPU mirrors, tests and references.
- [x] 3.2 Added header `@ref` citations: Hill 2016 in `color.hlsli`, Lagarde 2014 in `ibl.hlsli`; comment-only changes.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 4.1 Updated shader-library documentation above.
- [x] 4.2 Added seven CPU math tests in the existing automatically registered `test_brdf_parity.cpp`.
- [x] 4.3 Pinned Zig 0.15.2 `build -Doptimize=ReleaseFast` exit 0,
      173 shaders verified. `GTEST_FILTER=-RuntimeViewportTest.*`:
      `[==========] 8254 tests from 854 test suites ran. (48042 ms total)`,
      `[  PASSED  ] 8237 tests.`, `[  SKIPPED ] 17 tests, listed below:`;
      zero failures (exit 0), all seven new tests pass, no new failures versus
      the stated zero-failure baseline. `npm run test:layouts` exit 0,
      `Tier A: 105/105 passing entries`, `all layouts agree`.
      Logs and patch: `E:/UzmiEngine/_evidence/lighting/aces-hill-tail/`.
