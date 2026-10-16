# Design: incremental global distance field

## What exists (read 2026-10-05, `global_distance_field.{h,cpp}`, `global_sdf_composite_cs.hlsl`)
- Four clipmaps (half-extents 2500 / 10000 / 40000 / 160000 cm), each an RGBA16F expanded volume
  (distance + reflectance) and an R16F raw volume, `r.GI.GDF.Resolution` voxels per axis (256 in the
  project configuration: 19.5 / 78 / 312 / 1250 cm voxels).
- `Update()` snaps the centre to the voxel grid and sets one `dirty` bool when the snapped centre
  moved by a whole voxel on any axis. Voxel centres are therefore world-fixed points
  (`centre + (k + 0.5 - res/2) * voxel`), which is why H-49/H-54 found the overlap bit-identical
  after a recentre.
- `CompositeFromMeshSDFs()` rebuilds the instance list from the whole draw list every frame, hashes
  the ordered list, marks all four clipmaps dirty when the hash or count changed, and for every dirty
  clipmap dispatches the full `res^3` volume over every instance in cbuffer batches of 512
  (`accumulate` between batches). Each voxel loops over all instances with an AABB lower-bound skip;
  the stored distance is untruncated (clamped only to +-65504).
- Readers address the volume as `uvw = (p - centre) / extent * 0.5 + 0.5` with the static clamp
  sampler s2 (`gdf_surface.hlsli` for the cache trace, DDGI writer and probe reach;
  `sdf_reflections.hlsli` for the reflections, the final gather and the SSGI world gather, through
  the PerFrameCB clipmap arrays). The window test is `any(abs(local) > 1)`, falling through to the
  next cascade.
- `++m_sceneRevision` on a hash change drives `RadianceCache::InvalidateHistory()` (every probe
  retraced, history discarded), the SSGI history reset (`ssgiParams.sceneRevision`) and the probe
  classification rebuild.

## Decisions

### D1. Toroidal addressing without an origin uniform
The texel of world voxel `k` (an integer per axis, `k = floor(p / voxel)`) is `k mod res`. The
continuous sampling coordinate is then `uvw = frac(p / (2 * extent))` with the static WRAP sampler
s3 (linear, already in the root signature), and the window test stays on `centre` / `extent`. No new
per-clipmap constant reaches the readers: the clipmap's content for a world voxel never moves
between recentres, which is exactly what makes the scroll exact.

Caveat: a trilinear sample within half a voxel of the window edge would blend with the opposite
edge of the volume (world-distant voxels). The window is therefore shrunk by one voxel for sampling
(`abs(local) > 1 - 1/res` falls through to the next cascade); the next cascade covers it.

### D2. Recentre = entering slabs only
`Update()` records the integer shift `s` per axis. For each axis with `s != 0` the entering slab is
the range of `|s|` world-voxel planes on the moving side; the composite is dispatched over the slab
(a region in world-voxel indices: origin + size, 6 DWORDs added to `GdfCompositeConstants`, 15 of
16), writing texels `k mod res`. Three axis slabs may overlap at corners; the overlap is recomputed
twice with identical values (cheaper than carving the union). The initial placement and a scene
invalidation dispatch the whole volume as today (region = everything).

Gate G1: with `r.GI.GDF.Diagnostic.FieldCompare` the raw field written by the slab recompose is
copied out, a full recompose of the same window is forced on the next frame and copied out too,
and the two are compared texel by texel: 100 % identical, max 0 cm. (The old before/after
comparison would be identical by construction in toroidal storage: the overlap is never rewritten.)
If the window moves again before the full recompose the event is skipped and logged.

Implementation notes (2026-10-05): the region constants are packed 9 bits per axis into three
DWORDs (12 root constants in all) so the harness-visible layout keeps `clipmapCenter` and the
world-position formula of the original shader (`scripts/rendering/test_gdf_raw_composite.ps1` still compares
the extracted `main` with the frozen original, 210/210). The window origin modulo the resolution is
computed on the CPU from `centre - extent` (`lround`), so the shader never divides a world position
by the voxel size. The diagnostic phase offset (`r.GI.GDF.CenterPhaseOffset`) used to shift the
voxel grid by `-phase`; readers address by `frac(p / (2 extent))` without any origin constant, so
the grid must sit at voxel multiples and the phase now shifts only the crossing frames (which is what
followup-13's `test_gdf_controls.py` measures).

### D3. Truncation band, measured before it is relied on
Partial updates are exact only if a change cannot affect voxels beyond a known distance. With the
stored distance clamped to a band `B` (in voxels of the clipmap, CVar `r.GI.GDF.Band`, default = no
truncation until the gate passes), an instance change affects only voxels within `B` of its old and
new bounds. Every reader uses the distance as a sphere-tracing step, a validity threshold (5 cm) or
a clearance, never as a far-field value, so a band of 32 voxels should leave the lighting unchanged;
that is measured, not assumed: Sponza Camera C ROIs and the reference lighting bit-identical with
and without the band, plus the step-count/cost change from the block B item 1 timings.

### D4. Per-entity diff
Instances keyed by `DrawCommand::entityId` (`scene_sync.cpp`), with the previous frame's
`GdfMeshSdfGPU` entry per key. Changed, added and removed entries produce dirty regions = old AABB
union new AABB, expanded by `B`; material-only changes (texture streaming replaces a placeholder
SRV, LOD swap) go through the same path and no longer invalidate the world. A region recompose
iterates only the instances overlapping the region expanded by `B` (CPU filter) and runs with
`accumulate = 0` for that region. Instances outside every region are not touched.

### D5. Region-scoped knock-on
`m_sceneRevision` is replaced by a list of changed world regions per frame. The radiance cache
retraces (history kept) only the probes whose cells overlap a region; the SSGI/gather history is
not reset for partial updates (its temporal filter converges); probe classification is rebuilt
(cheap, H-70). Mobility is not authored in this task: with the diff keyed per entity, a static
scene produces no regions and the hash disappears, so the per-frame cost is the diff itself
(138 instances on Sponza).

## Phase 3-4 implementation notes (2026-10-05)
- `MeshSDF::instanceKey` = `DrawCommand::entityId`; records sorted by key with a stable sort, so
  several draws of one entity pair up in order; unknown keys (UINT32_MAX) pair by order too.
- World bounds of an instance = the eight corners of its local SDF volume through the world matrix,
  expanded by the two-sided surface bias times the largest row length of the matrix.
- A change region per clipmap = (old ∪ new bounds) ⊕ (band + 2 voxels), clipped to the window; its
  instance list = records whose (bounds ⊕ band) intersects the box. Regions are dispatched after
  the recentre slabs with a UAV barrier between them; the whole window wins when the band is 0,
  when a clipmap has no data yet, or when more than `r.GI.GDF.MaxRegions` instances changed.
- `GdfCompositeConstants` gains `band` (13 DWORDs); the shader clamps both outputs to it after the
  instance loop. `test_gdf_raw_composite.ps1` runs with band 0 (the frozen original is untruncated).
- The scene revision bumps only for whole-window recomposes; `RadianceCache::RetraceKeepingHistory()`
  (`m_dirty = true`) is the regional reaction, logged as `GDF regional recompose`.

- Phase 4: the change boxes (bounds ⊕ band of clipmap 0 + 1 voxel) go to the radiance cache
  (`RadianceCacheTraceConstants`: `regionCount` + 8 boxes, 464 B; a probe inside replaces or blends
  its history at `r.GI.RadianceCache.RegionBlendAlpha`, default 1.0) and to the SSGI temporal
  accumulation (`HistoryCB` 592 B: this and last frame's boxes, reject code 6 `field_changed`).
- Field compare rewritten to compare the incremental result with a whole-window recompose of the
  same instances in the same command list (the earlier next-frame form skipped trajectory events
  and was meaningless during loading).
- The moving-box gates G3 were re-specified on 2026-10-05: the first form (7 frames after arrival
  against the static reference at frame 95) measured the cache's own ~200-frame convergence, not the
  regional path. The reference is now rendered over the same frames and the transient is gated
  separately (G3e, registered before the blend-weight candidate was measured).

## Order and gates
1. Baseline (manifests `73-b02-baseline`: longsweep `GDF_Composite` cost per recentre, preload
   invalidations) — task 1.1.
2. D1 + D2 + FieldCompare rewrite — gates G1 (bit-identity) and G5 (recentre cost <= 25 % of the
   full recompose on clipmap 0); the reference scene `gdf-incremental-reference`
   (`scripts/rendering/gdf_incremental_reference.py`, box moved by `gdf_box_move.lua`) and Sponza both behind
   `r.GI.GDF.Incremental` (default off until the gates pass).
3. D3 band gate (Sponza ROIs, reference lighting, cost).
4. D4 + D5 — gates G2 (no global invalidation while the box moves), G3 (indirect light under the box
   follows it within 8 frames), G4 (Sponza Camera C unchanged).

## Rejected
- A per-clipmap origin offset uniform (Lumen-style): equivalent to D1 but every reader and the
  reach grid would need the extra constants; `k mod res` needs none.
- Recomposing the union of the three slabs exactly: more CPU logic for at most a few percent of a
  slab's cost at the corners.
- Authoring mobility first: nothing in the engine reads it, and the per-entity diff makes a static
  scene free without it.
