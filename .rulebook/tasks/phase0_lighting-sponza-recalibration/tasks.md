## 1. Baseline and gates
- [x] 1.1 **Done** Record the before state (doc "Before"): the fix-alone ratios 0.894 / 0.928 (block B.5c's S4 and Camera A measurement).
- [x] 1.2 **Registered; results in the doc gate table** Pre-register Q1-Q3 (proposal).

## 2. The scene
- [x] 2.1 (Q1 PASS, Q2 FAIL, Q3 FAIL) `sponza.uzscene`, `sponza-gi-camera-c.qa.uzscene`, `sponza-fog.uzscene`: the SkyLight's intensity at √3 (the indirect-lighting intensity stays 1, see the amendments).

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 3.1 (`docs/analysis/lighting-sponza-recalibration.md`, the B.5c document, the block-B index) Update or create documentation covering the implementation (`docs/analysis/lighting-sponza-recalibration.md`; the B.5c document's "look of Sponza"; the block-B index).
- [x] 3.2 (the pre-fix captures as the reference, the direct light as the invariant, the fix-alone captures as the control) Write tests covering the new behavior with behavioral assertions and negative controls (`scripts/test_sponza_recalibration.py`: the pre-fix captures as the reference, the direct light as the invariant, the fix-alone captures as the control).
- [x] 3.3 Run tests and confirm they pass; validate this task through Rulebook; report pre-existing failures separately. **Done** — `test_sponza_recalibration.py` 1/3 on the final binary: Q1 positions and direct light identical to this binary with the scenes before the edit; the edit alone: diffuse_indirect 1.054 / 1.072, specular 1.032 / 1.038, colour 1.010 / 1.072 (Camera A / C); against the pre-fix captures diffuse_indirect 1.358 / 1.627, colour 1.253 / 1.629 (Q2, Q3 FAIL as registered: the mesh-decal commit removed the atrium-spanning dirt-decal mesh from the distance-field occluders, x 1.39 / x 1.69 on the indirect, the second amendment). renderer gtests 7471 passed.
