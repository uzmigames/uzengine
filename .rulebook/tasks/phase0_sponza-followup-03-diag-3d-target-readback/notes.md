# Notes — phase0_sponza-followup-03-diag-3d-target-readback

2026-09-08. Exe 12:40.

## Diagnosis (item 1.1)

The readback was never broken — **the inspector refused to attempt it.** `inspector_pass_validate.cpp` had an explicit early-out:

```cpp
// 3D volume textures — CopyTextureToBuffer only copies Z-slice 0 of a
// volume subresource via the current RHI signature, ...
if (texDesc.is3D) {
    result.minVal = 0.0f; result.maxVal = 0.0f; result.avgVal = 0.0f;
    result.isZero = false;
    UZ_LOG(..., "SKIPPED (3D volume — use --pass-dump/--vis)");
    continue;
}
```

The skip wrote 0/0/0 into the result and cleared `isZero`, so `--diag` printed `gi.gdf_cascade0 OK min=0 max=0 avg=0` — indistinguishable from an empty volume. That is why the same numbers appeared in SponzaTest: both branches carry the same early-out, and the `D3D12_BOX` hunk ported in task 05 fixed the RHI half of the problem while this branch still skipped every volume.

Second half of the problem: `CopyTextureToBuffer` computed `subresourceIndex = mipLevel + arraySlice * mipLevels`. That is correct for arrays and cubemaps but wrong for volumes — a 3D texture has **one** subresource per mip containing every Z slice, so `arraySlice` had no way to reach slice 1..N.

## Fix

1. `dx12_command_list.cpp` — for `is3D`, the subresource is `mipLevel` alone and `arraySlice` selects the **Z slice** through the source box (`D3D12_BOX{0,0,z, w,h,z+1}`, clamped to the mip's depth). 2D/array/cubemap behaviour is unchanged, and the null box for depth-stencil copies is preserved.
2. `inspector_pass_validate.cpp` — the skip is gone. A volume is sampled at **one** slice, its centre (`depthOrArraySize / 2`): slice 0 of a global distance field clipmap is its boundary plane, uniformly far from geometry, so it reads as a constant; the centre slice cuts through the scene.
3. `verify_sponza_loading.ps1` — the converged run (which passes `--diag`) now parses the `DIAG_JSON` line and throws if any `gi.gdf*` target reads all zeros, so this cannot silently regress. It prints the min/max/avg it saw.

## Evidence

`--diag` on Sponza, default project, frame ~237:

| Target | Before | After |
|---|---|---|
| `gi.gdf_cascade0` (expanded RGBA16F) | min 0 max 0 avg 0 | **min +0.06641 max +139.75 avg +31.54** |
| `gi.gdf_raw_cascade0` (raw R16F) | min 0 max 0 avg 0 | **min +121.88 max +165.88 avg +143.75** |
| `atmo.ap_volume` (aerial perspective, also 3D) | min 0 max 0 avg 0 | **min +0.01064 max +0.96191 avg +0.26433** |
| `volfog.inscattering` / `volfog.integrated` | WARN_ZERO | WARN_ZERO — genuinely zero, volumetric fog is off in this scene |

`hasFailure=false`, 28 targets. Cross-check: `scripts/test_gdf_raw_composite.ps1` 210/210 on the same build (CPU-side reference for the same volume).

The distances read positive across the centre slice of cascade 0 — that slice sits above the atrium floor, so it never enters geometry. This is the readback reporting real content, not a claim that the field has no interior. A future refinement (not needed for the assertion) is a min/max reduction over all slices instead of one representative slice.

## Regression

- 2D targets unchanged (`gbuffer.*`, `hdr`, `ssgi.*`, `lighting.*`, `atmo.sky_cubemap` identical to the 2026-09-08 baseline).
- The DDGI atlases registered by followup-01 are 2D and still read correctly (`gi.ddgi_irradiance` avg 0.181).
- `verify_sponza_loading.ps1` (with the new assertion) — see item 3.1 in tasks.md.
