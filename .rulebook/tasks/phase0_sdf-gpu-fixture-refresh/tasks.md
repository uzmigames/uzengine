## 1. Items
- [x] 1.1 Update both fixtures as test_gdf_raw_composite.cpp and test_gdf_raw_lifecycle.cpp were updated (whole-window region, windowOrigin, band 0, sliceOffset 0, WRAP sampler at s3).
- [ ] 1.2 Run test_sdf_window.ps1 -BvhCompare and the five other runners; update the known-issues r.RadianceCache items they measure.

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass

Evidence: [fixture refresh](../../../docs/analysis/sdf-gpu-fixture-refresh/01-fixture-refresh.md).
2026-10-13: vendored Zig 0.15.2 runtime build EXIT=0 (139 shaders verified);
standalone fixtures rebuilt, BVH comparison bit-identical for 2,097,152 fp16 voxels;
GPU, refined-controls, and reflection-energy runners pass; owned GI gtests 23/23.
Items 1.2 and 2.3 remain open because surface-origin and diffuse runners cannot
compile: their GatherSdfDiffuse calls provide seven arguments, current helper requires eight.
Stopped per the execution brief; the gate is not archived or claimed complete.
