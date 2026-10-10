## 1. Baseline and gates
- [x] 1.1 **Done** Record the before state (doc "Before"): the audit of the 254 sample meshes (13 not clean: 11 whole-mesh inversions, 2 mixed), chrome_test's far-hemisphere dump (block B.5b).
- [x] 1.2 **Registered; results in the doc gate table** Pre-register W1-W4 (proposal).

## 2. The repair
- [x] 2.1 (W1 PASS) `scripts/rendering/repair_mesh_winding.py`: the audit (LOD0 and the LOD table) and the whole-mesh flip.
- [x] 2.2 (W2 PASS, W3 PASS, W4 PASS) The assets repaired in place (the sphere, seven Sponza meshes, bunny, teapot, suzanne, test_shard) and the residual LOD triangles cleaned with `test_decal_lod_normals.ps1 -Repair`.

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 3.1 (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-mesh-winding.md`, the known-issues entries, the block-B index) Update or create documentation covering the implementation (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-mesh-winding.md`; the known-issues entries for `sphere.uzmesh` and `arch_stones_01_stone_trims_01_0`; the block-B index).
- [x] 3.2 (the audit, the sphere's hemisphere and PMREM reflection, Sponza's positions outside the repaired meshes) Write tests covering the new behavior with behavioral assertions and negative controls (`scripts/rendering/test_mesh_winding.py`: the audit, the sphere's hemisphere and its PMREM reflection, Sponza's positions outside the repaired meshes as the invariant).
- [x] 3.3 Run tests and confirm they pass; validate this task through Rulebook; report pre-existing failures separately. **Done** — `test_mesh_winding.py` 4/4 on build b8ff5077 (no engine change: W1 the audit of 253 meshes reads no inversion, two mixed meshes of the simplifier class; W2 the chrome sphere's positions x median 72.2 cm and N·V 0.70; W3 its specular against F × the PMREM, median error 0.035; W4 Sponza Camera C positions and direct light identical, 0 px changed); the gtests are those of block B.5c's binary (unchanged); the repaired meshes load (the chrome_test and Sponza captures ran to completion).
