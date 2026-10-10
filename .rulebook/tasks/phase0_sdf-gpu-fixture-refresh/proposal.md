# Refresh the SDF GPU fixtures ported in parity-03/04

## Why

scripts/test_sdf_gpu.cpp and scripts/test_sdf_window.cpp still bind the pre-block-B layouts: the GDF composite
takes 13 root DWORDs since ce2caa5e / 0a9cc732, the mesh SDF bake 9 since 8c4d14b6, and sdf_reflections.hlsli samples
through a WRAP sampler at s3 since ce2caa5e. Their runners (test_sdf_gpu.ps1, test_sdf_window.ps1,
test_sdf_refined_controls.ps1, test_sdf_surface_origin.ps1, test_sdf_diffuse_gpu.ps1, test_sdf_reflection_energy.ps1)
therefore test old layouts, and parity-03's -BvhCompare run (GPU BVH vs brute-force bakes bit-identical) never ran.

Origin: parity-06 close-out (2026-10-08).

## What Changes

- Update both fixtures as test_gdf_raw_composite.cpp and test_gdf_raw_lifecycle.cpp were updated (whole-window region, windowOrigin, band 0, sliceOffset 0, WRAP sampler at s3).
- Run test_sdf_window.ps1 -BvhCompare and the five other runners; update the known-issues r.RadianceCache items they measure.

## Impact
- Affected specs: sdf-gpu-fixture-refresh (new)
- Affected code: see the items; gates are pre-registered per item before its first run
- Breaking change: NO until a gate passes
- User benefit: the open points of the closed followups have an owner and a plan.
