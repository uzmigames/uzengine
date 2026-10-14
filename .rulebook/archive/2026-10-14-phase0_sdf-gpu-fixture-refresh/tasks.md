## 1. Items
- [x] 1.1 Update both fixtures as test_gdf_raw_composite.cpp and test_gdf_raw_lifecycle.cpp were updated (whole-window region, windowOrigin, band 0, sliceOffset 0, WRAP sampler at s3).
- [x] 1.2 Run test_sdf_window.ps1 -BvhCompare and the five other runners; update the known-issues r.RadianceCache items they measure. **Done 2026-10-13**: the two runners blocked by the GatherSdfDiffuse 7->8 arg drift now compile — the fixtures (embedded in test_sdf_diffuse_gpu.ps1 and test_sdf_surface_origin.ps1, which regenerate the .hlsl on run) supply the 8th argument rotationMode=0u (canonical hashed rotation, matches the fixtures' own GeometricCoverage reference). Current build (RTX 5080): test_sdf_diffuse_gpu.ps1 PASS all sub-tests (world reflections max abs error <=0.000195); test_sdf_surface_origin.ps1 compiles and reproduces the carried-over 1-of-32-ray diffuse leak (0.009328, pre-existing known-issue), its -Reflections variant PASSES (0.001984 < 0.002). window -BvhCompare + gpu + refined_controls + reflection_energy already pass. known-issues updated (blocked paragraph removed).

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass. **Done 2026-10-13**: all six SDF runners execute on the current build; the only non-pass is the pre-existing, already-documented 0.009328 diffuse-path leak in test_sdf_surface_origin.ps1 (carried over from SponzaTest 2026-09-08 — rotationMode=0 reproduces the canonical value, not a regression). diffuse_gpu full PASS; surface_origin -Reflections PASS (0.001984).

Evidence: [fixture refresh](../../../docs/analysis/sdf-gpu-fixture-refresh/01-fixture-refresh.md).
2026-10-13: vendored Zig 0.15.2 runtime build EXIT=0 (139 shaders verified);
standalone fixtures rebuilt, BVH comparison bit-identical for 2,097,152 fp16 voxels;
GPU, refined-controls, and reflection-energy runners pass; owned GI gtests 23/23.
2026-10-13 (closure): fixed the 7->8 arg drift — the surface-origin and diffuse-gpu
fixtures (regenerated from their .ps1 heredocs) now pass rotationMode 0u (canonical,
matches the embedded GeometricCoverage reference). Both runners compile and execute:
diffuse-gpu full PASS; surface-origin -Reflections PASS (0.001984), its diffuse default
reproduces the carried-over 0.009328 leak (pre-existing SponzaTest known-issue, not a
regression). Items 1.2 and 2.3 complete; known-issues updated (blocked paragraph removed).
