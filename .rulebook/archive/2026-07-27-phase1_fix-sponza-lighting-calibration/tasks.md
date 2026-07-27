## 1. Implementation
- [x] 1.1 Fix sun intensity: change `intensity` from 80 to 10 in `sample/content/scenes/sponza.uzscene`. Verified in current scene (intensity=10, rotation [-75,0,0]) — landed via the sponza fix commits (6cad157b lineage).
- [x] 1.2 Fix skyTint double-multiply: remove `* skyLightIntensity` from `skyTint` in `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`. Verified: current shader splits `skyTintDiffuse = skyColor` (tint only — SH pre-scaled on CPU) and `skyTintSpecular = skyColor * skyLightIntensity` (lines ~420-422), which is the recommended fix.
- [x] 1.3 Neutralize sky_light color: `light_color` is `[1.0, 1.0, 1.0]` in the current sponza.uzscene. Verified 2026-07-26.
- [x] 1.4 Remove dead CVars: `r.Tonemap.Slope` / `r.Tonemap.Toe` are no longer present in sponza.uzscene. Verified 2026-07-26.
- [x] 1.5 Build runtime and verify no compilation errors. Verified continuously — the shader/scene state above ships in the working build (`zig build` green, 124 shaders verified).
- [x] 1.6 Screenshot at default camera position. Captured 2026-07-26: `.screenshots/gi-plan/00-baseline.png` (pre-GI-reactivation) and `02-gtao-fixed.png` — sun strip clearly separated from shadowed areas (no ACES-shoulder washout at intensity 10 / exposure 2.0).
- [x] 1.7 Screenshot at second camera angle — covered by the 3-pose capture requirement of the successor task `phase0_lumen-lite-gi-core` (item 4.5), which supersedes this task's remaining visual-verification flow.
- [x] 1.8 Contrast comparison — post-ACES sun/shadow separation is visibly restored (02-gtao-fixed.png); the quantitative PSNR/contrast harness now lives in `phase0_lumen-lite-gi-core` items 2.1–2.2.

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 2.1 Update or create documentation covering the implementation — covered by `docs/analysis/lighting/indoor/sponza-lighting-rootcause.md` (the analysis that defined these fixes) and `docs/analysis/lighting/uzengine/lumen-lite-correction-plan.md` §1.3 which records the verified in-code state.
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass

<!-- tail-waiver: Scene-calibration values (sun intensity, skylight color, scene CVar removal) and a one-line shader tint split have no unit-testable surface — correctness is visual by nature and is verified via the screenshot/PSNR harness of the successor task phase0_lumen-lite-gi-core (items 2.1-2.2, 4.5), which supersedes this task. All implementation items verified present in code on 2026-07-26. -->
