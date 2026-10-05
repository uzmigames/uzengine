# Lighting block B, item 2 — incremental global distance field (2026-10-05)

Companion to the lighting assessment of 2026-10-05 (block B, "GDF incremental") and to the Rulebook
task `phase0_lighting-block-b-02-gdf-incremental` (design.md has the decisions). Branch
`feat/sponza-gi-parity`.

## Phase 2 — toroidal storage, entering slabs only

**Before.** Every recentre of a clipmap rewrote all of its `res^3` voxels over every instance
(`CompositeFromMeshSDFs`, 512-instance batches). On the long sweep of Sponza (project configuration,
256^3, 138 instances) that is **~125 ms of GPU per clipmap-0 recentre** (`GDF_Composite` mean
1.27 ms over 297 frames with three recentres, `73-b02-baseline/manifest-sweep.json`), a hitch the
flicker programme had measured as a lighting step (H-40 and followers). H-49 / H-54 had shown the
overlapping voxels come out bit-identical, so all but the entering slab of that work was redundant.

**Change.** World voxel `k` lives in texel `k mod resolution`; the window centre is always a whole
number of voxels, so a voxel keeps its texel across recentres. `Update()` accumulates the whole-voxel
shift of each clipmap and the composite dispatches one slab per moved axis (the entering side,
`|shift|` planes, corners written twice) through three packed root constants (region origin, region
size, window origin modulo resolution; 12 DWORDs). The whole window is still dispatched on the first
fill, on a scene change (the instance hash) and with `r.GI.GDF.Incremental 0`. Every reader
addresses the volume as `frac(p / (2 extent))` through the static linear WRAP sampler (s3) and treats
the outer voxel of the window as outside, where a trilinear tap would wrap to the far side. The
diagnostic phase offset (`r.GI.GDF.CenterPhaseOffset`) now shifts the crossing frames only, not the
grid. `r.GI.GDF.Diagnostic.FieldCompare` compares the slab recompose with a full recompose of the
same window forced on the next frame.

**Gates** (`scripts/test_gdf_incremental.py`, pre-registered in the task):

| Gate | Criterion | Result |
|---|---|---|
| G1 | slab recompose == full recompose on every clipmap-0 recentre of the boundary trajectory (`run_gdf_field_compare.py`: IDENTICAL) | PASS (20 recentre events of the boundary trajectory, +-x and +-y single-voxel moves: 100.0000 % identical texels over the whole 256^3 volume, max |delta| 0 cm; 2 events during loading skipped by design) |
| G4 | Sponza Camera C ROI means (project configuration) with Incremental 1 vs 0 within 1e-4 relative | PASS (shadow_wall 2.3e-6, lit_wall 1.7e-7, centre 1.5e-6 relative; unsupported fractions identical) |
| G5 | `GDF_Composite` mean on the long sweep with Incremental 1 <= 25 % of Incremental 0, same recentres | PASS (`GDF_Composite` mean 1.327 -> 0.0224 ms, ratio 0.017, 46 clipmap-0 recentres in both runs; frame 25.1 -> 23.9 ms) |

Other checks on the same build: `test_gdf_controls.py` 5/5 (freeze, phase shift of the crossing frames, world tracking), `verify_sponza_loading.ps1` 3/3, `test_local_light_shadows.py` 6/6, GI gtests 59/59 (`*GI*:*Gdf*:*Radiance*:*Sdf*`), `test_gdf_raw_composite.ps1` 210/210, `test_gdf_raw_lifecycle.ps1` 34/34.

**Harnesses that compile the production code as text.** `scripts/test_gdf_raw_composite.ps1`
(the shader's `main` against a frozen original, 210/210) still passes: the region constants were
packed so that the world-position formula and `clipmapCenter` stay as the original wrote them.
`scripts/test_gdf_raw_lifecycle.ps1` had not compiled since followup-13 added the field-compare
diagnostic inside the block it extracts; its template now carries the stubs and checks the 12 root
constants (34/34).

## Still to do (phases 3 and 4 of the task)

- Truncation band (`r.GI.GDF.Band`) measured against the untruncated field before it is relied on.
- Per-entity diff: regions from old ∪ new bounds (+ band) instead of the whole-list hash, so a moving
  object, a streamed texture or a LOD swap no longer recomposes four volumes (34 global
  invalidations during Sponza's preload today).
- Region-scoped radiance-cache / SSGI invalidation; reference scene `gdf-incremental-reference`
  (`scripts/gdf_incremental_reference.py`, `gdf_box_move.lua`) for gates G2 / G3.
