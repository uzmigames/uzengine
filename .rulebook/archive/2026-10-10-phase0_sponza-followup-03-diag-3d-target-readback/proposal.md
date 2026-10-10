# Proposal: phase0_sponza-followup-03-diag-3d-target-readback

## Why

`--diag` reports `gi.gdf_cascade0` and `gi.gdf_raw_cascade0` as `min=max=avg=0` on Sponza in
**both** repositories (our exe and the SponzaTest exe, 2026-09-08), even though the global distance
field demonstrably drives the radiance cache, SDF reflections and the final gather (138 bakes,
`probesReady`, `test_sdf_diffuse_gpu.ps1` passes on the same volumes). Task 05 ported the
`CopyTextureToBuffer` first-slice `D3D12_BOX` for `is3D` textures
(`dx12_command_list.cpp`), which was necessary but not sufficient. Until this works the inspector
cannot catch a broken GDF (the very failure mode task 03 started from: empty stub shaders), and the
readiness/loading harness cannot assert on it.

Where to look: `inspector_pass_validate.cpp` (`if (texDesc.is3D)` at ~94, `arraySlices` at ~145 —
footprint and subresource choice for volumes), the registration in `frame_lighting.cpp:459-463`
(`clip.texture`/`clip.srvIndex` vs `clip.rawTexture`), and the frame position of the readback
relative to `GlobalSDF_Composite` (a readback scheduled before the composite reads last frame's
cleared volume; a slice-0 read of a clipmap reads its boundary).

## What Changes

Make the inspector read a meaningful slice (centre slice, or a min/max reduction over all slices) of
3D targets with the right D3D12 footprint, after the pass that writes them; assert non-zero
signed-distance statistics for the GDF in the diag harness.

## Impact
- Affected specs: none
- Affected code: `runtime/engine/renderer/passes/debug/inspector_pass_validate.cpp`,
  `runtime/engine/rhi/dx12/dx12_command_list.cpp` (slice parameter), `frame_lighting.cpp`
  (registration/scheduling), `scripts/rendering/verify_sponza_loading.ps1` (assertion)
- Breaking change: NO
- User benefit: `--diag` becomes a real guard for the SDF GI volumes
