## 1. Baseline and gates
- [x] 1.1 **Done** Record the before state (doc "Before"): the fix-alone ratios 0.894 / 0.928 (block B.5c's S4 and Camera A measurement).
- [x] 1.2 **Registered; results in the doc gate table** Pre-register Q1-Q3 (proposal).

## 2. The scene
- [x] 2.1 (Q1 PASS, Q2 FAIL, Q3 FAIL, Q4 FAIL) `sponza.uzscene`, `sponza-gi-camera-c.qa.uzscene`, `sponza-fog.uzscene`: the SkyLight's intensity and indirect-lighting intensity at √3 (Amendment 3 corrects the first version, which kept the indirect knob at 1).

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 3.1 (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-sponza-recalibration.md`, the B.5c document, the block-B index) Update or create documentation covering the implementation (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-sponza-recalibration.md`; the B.5c document's "look of Sponza"; the block-B index).
- [x] 3.2 (the pre-fix captures as the reference, the direct light as the invariant, the fix-alone captures as the control) Write tests covering the new behavior with behavioral assertions and negative controls (`scripts/rendering/test_sponza_recalibration.py`: the pre-fix captures as the reference, the direct light as the invariant, the fix-alone captures as the control).
- [x] 3.3 Run tests and confirm they pass; validate this task through Rulebook; report pre-existing failures separately. **Done** — `test_sponza_recalibration.py` 1/4 on the final binary (corrected 2026-10-08): Q1 positions and direct light identical to this binary with the scenes before the edit; Q4 FAIL as registered at Camera A only (diffuse 1.0085; Camera C 1.0000): a repeat of the same configuration reads 0.9913 against the harness run and 1.0005 against the intensity-only capture, and no Camera A pixel reads the SH fallback (path bits 0x3), so the knob is inert and the 0.85 % is the frame-60 cross-process spread; the edit alone: diffuse_indirect 1.075 / 1.072, colour 1.015 / 1.072 (Camera A / C); Q2 / Q3 FAIL as registered (against the pre-fix captures: diffuse 1.374 / 1.627; the mesh-decal commit removed a bogus distance-field occluder, Amendment 2).
