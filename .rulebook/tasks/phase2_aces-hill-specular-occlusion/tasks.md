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
- [x] 2.2.1 Captured damaged_helmet from three views (quarter, profile, visor) on the
      RTX 5080 with `r.TonemapOperator` unset, 1 and 2. All runs used TAA on and bloom off;
      everything else was shipped. Hill is the shipped default. The CVar defaults to
      1 = `ACESHillFit`, and every unset run reports 1. Each unset run's screenshot
      equals `ACESHillFit` applied offline to the same frame's linear HDR (region-dump
      `output` record times that frame's exposure): 61.1-61.7 dB, every pixel within
      1 code value. Against Narkowicz the same screenshots score 15.4-15.5 dB.
      Operator 2 matches Narkowicz at 61.3-61.6 dB. HDR and exposure are identical
      across operators. Narkowicz has no 0.6 pre-scale, so it renders about twice as
      bright (mean display luminance 0.29-0.30 vs 0.14-0.15). Hue: the helmet's
      emissive map has no orange or red (it is cyan, green and yellow), so a fixture
      adds 15 orange/red/amber emissive swatches. Hill reduces the hue shift but does
      not remove it:
      - At a curve-input peak up to about 0.5, Hill stays within 4.4 deg (Narkowicz
        reaches 7.3).
      - At about 1.8, Hill shifts +3/+13/+11 deg (Narkowicz +7/+27/+20).
      - At 7-28, Hill shifts +17 to +39 deg (Narkowicz +24 to +49).
      - Mean |shift| is 13.8 deg for Hill vs 18.8 deg for Narkowicz.
      Over-exposed reds desaturate toward white under Hill; Narkowicz turns them
      yellow.
- [x] 2.2.2 Compared specular occlusion ON (shipped AO) with OFF. OFF sets
      `r.Material.AOMap=0` and `r.GTAO=0`, so the AO input is 1 and the factor is
      exactly 1. Scenes: damaged_helmet (baked-AO crevices) and two dielectric spheres
      on a ground plane (GTAO contact AO). Per pixel, the on/off ratio of the deferred
      pass's `specular_indirect` equals `ComputeSpecularOcclusion(NoV, ao, roughness)`:
      median |error| at most 4.2e-8, p95 at most 1.4e-7. Metals stay at exactly 1, by
      design. Helmet dielectric pixels with AO < 0.9 keep 0.75-0.81 of their
      specular. Within 0.5 R of the sphere contacts they keep 0.14-0.37, so the
      specular rim no longer leaks at the contact edges.
- [x] 2.2.3 Compared Hill with full ACES on the same linear HDR input. The
      reference is the Academy ACES 1.3 CTL (RRT + ODT.Academy.sRGB_100nits_dim),
      ported and self-checked against the CTL tonescale knots and Hill's matrices.
      Results: 52.9-53.7 dB on the three helmet views (8-bit: 50.9-51.3 dB),
      51.4 dB on the swatch frame, and 40.3 dB on its 41,894 saturated emissive
      pixels. The >= 35 dB target is met. Falcor has no full-ACES operator: its
      ToneMapper `Aces` is the Narkowicz fit after a 0.6 pre-scale. Against that
      operator Hill scores 26.7-26.8 dB, below 35 dB.

Evidence, montages and `analysis.json` are in
`E:/UzmiEngine/_evidence/lighting/aces-visual/`. The script is
`scripts/rendering/aces-visual/` (`verify.py`, `aces_reference.py`). GPU-lane gates
at 06515aec: build exit 0 (173 shaders), `npm run test:layouts` exit 0
(Tier A 105/105), gtests 8254 run, 8237 passed, 17 skipped, 0 failed.

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
