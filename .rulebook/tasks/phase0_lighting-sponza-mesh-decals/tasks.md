## 1. Baseline and gates
- [x] 1.1 **Done** Record the before state (doc "Before"): the masked routing of `dirt_decal.uzmat` (threshold 0 → 0.3333), the opaque quads in the G-buffer and the shadow passes; `scripts/test_mesh_decals.py --before` on build b8ff5077 (the reference scene and Sponza Camera A).
- [x] 1.2 **Registered; results in the doc gate table** Pre-register D1-D5 (proposal).

## 2. Mesh decals
- [x] 2.1 (D1 PASS, D2 PASS) `material_pipeline.{h,cpp}`: the mesh-decal PSO variant (albedo alpha blend, the other targets kept, no depth write).
- [x] 2.2 (D3 PASS) `scene_sync.cpp`: the draw flag (bit 4, 0x10; bit 3 is the two-sided flag) from the graph's Decal domain; `frame_shadows.cpp`, `shadow_pass.cpp`, `frame_gi_passes.cpp`: not an occluder.
- [x] 2.3 (D1 PASS, D4 PASS) `gbuffer_pass.cpp`: the second phase after the opaque and masked draws; skipped in the depth-only pass.
- [x] 2.4 (D1 PASS) `gbuffer_ps.hlsl`: opacity in the albedo target's alpha, no masked test, discard below 1/255.
- [x] 2.5 (D4 PASS, D5 PASS) `dirt_decal.uzmat`: `domain: Decal`; `scripts/decal_reference.py`.

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 3.1 (`docs/analysis/lighting-sponza-mesh-decals.md`, the known-issues entry, the block-B index) Update or create documentation covering the implementation (`docs/analysis/lighting-sponza-mesh-decals.md`; the known-issues "Decals" entry; the block-B index).
- [x] 3.2 (the blend identity against the control, the wall plane, the shadow-free control, Sponza's positions outside the decal bounds) Write tests covering the new behavior with behavioral assertions and negative controls (`scripts/test_mesh_decals.py`: the blend identity against the control capture, the wall plane under the decal, the shadow-free control, Sponza's positions outside the decal bounds as the invariant).
- [x] 3.3 Run tests and confirm they pass; validate this task through Rulebook; report pre-existing failures separately. **Done** — `test_mesh_decals.py` 5/5 on the final binary (D1 the lit colour along the ramp is lerp(wall, decal, alpha) to 0.012 with R² 0.99995, before 0.83 / 0.66; D2 positions under the decal on the wall, |y| 0.03 cm, before 2.97; D3 the direct light outside the footprint equal to the control; D4 Sponza Camera A and C positions changed only inside the decal meshes' world-bounds footprint (190 070 and 241 509 px, 0 outside), GBuffer 0.548 → 0.567 and 0.520 → 0.532 ms; D5 the footprint's boundary contrast 0.53 → 0.12 and 0.50 → 0.19); gtests 1085/1095 in the pipeline + render-graph + GI + light + scene-loader + material + decal + G-buffer + shadow filter, the 8 failures pre-existing and outside this diff (SceneLoaderTest.CameraComponent_DefaultValues_WhenFieldsAbsent; ShaderPipelineMetal ×4; VFXRenderGraph constant-buffer sizes ×3), 2 skipped; loading 3/3 (startup, converged, camera-c: exit 0, pre-load complete after 39-41 frames).
