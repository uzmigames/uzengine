## 1. Baseline and gates (before any change)
- [ ] 1.1 Record with block B item 1 timings the GPU cost of `GDF_Composite` on Sponza (project configuration) for a recentre of clipmap 0 and for a full invalidation, and the count of global invalidations during preload and during a 600-frame walk; freeze them as the before numbers.
- [ ] 1.2 Reference scene `gdf-incremental-reference`: ground, two walls, a 100 cm box that moves 400 cm along x between frames 100 and 160 (Lua), camera above; pre-register the gates: G1 field after a scroll bit-identical to a full recompose (FieldCompare diagnostic, 100 % of overlapping voxels, max 0 cm); G2 a moving box recomposes only regions overlapping its old ∪ new AABB (+ band) and the radiance cache logs no `reason=invalidate`; G3 `diffuse_indirect` under the box follows it within 8 frames; G4 Sponza Camera C ROIs bit-identical with no moving object; G5 recentre cost <= 25 % of the full recompose on clipmap 0.

## 2. Toroidal scroll per clipmap
- [ ] 2.1 `ClipmapData`: integer voxel shift per recentre, toroidal origin, dirty slabs instead of one `dirty` bool (`global_distance_field.h:77-90`, `.cpp:181-214`).
- [ ] 2.2 Composite takes a region (origin, size) in `GdfCompositeConstants` (<= 16 root DWORDs) and dispatches only the entering slabs; readers wrap (`gdf_surface.hlsli:33-44`, `sdf_reflections.hlsli:39-49, 67-81`, `ssgi_surface.hlsli:28-35`); `ServiceFieldCompare` follows the layout.
- [ ] 2.3 G1 and G5 measured on Sponza (clipmaps 0 and 1) and on the reference.

## 3. Per-instance diff
- [ ] 3.1 Key instances by entity id (`scene_sync.cpp:327`), diff old/new transform, SDF and material SRVs; derive dirty regions from old ∪ new AABB; texture streaming and LOD swaps become material-only or region-scoped updates (`scene_texture_assets.cpp:119-139`, `scene_sync.cpp:309-313`).
- [ ] 3.2 Band: clamp the composite distance to the readers' maximum useful distance (`global_sdf_composite_cs.hlsl:85-110`) and show Sponza ROIs and the reference lighting unchanged against the untruncated field before the band is relied on for partial updates.
- [ ] 3.3 Region recompose re-iterates every instance overlapping the region (+ band); G2 on the reference.

## 4. Region-scoped knock-on
- [ ] 4.1 Radiance cache: invalidate only probes whose cells overlap the changed regions instead of the global revision reset (`frame_gi_passes.cpp:242-244`, `radiance_cache.cpp:692-693, 837-839`); SSGI/final-gather history and probe classification likewise.
- [ ] 4.2 Mobility authored in the scene (`scene_component.h:168-169` default Movable today): Static instances are excluded from the per-frame diff; Sponza marked static; G3 and G4.

## 5. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 5.1 Update or create documentation covering the implementation (docs/specs/rendering/gi-system.md "fully dynamic" claim and the half-voxel comment, known-issues, ledger).
- [ ] 5.2 Write tests covering the new behavior with behavioral assertions and negative controls (`test_gi_gpu.cpp` layouts, `scripts/test_gdf_incremental.py` with the gates of 1.2, `test_gdf_raw_lifecycle.ps1` updated).
- [ ] 5.3 Run tests and confirm they pass; validate this task through Rulebook; report pre-existing failures separately.
