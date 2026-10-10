# Proposal: Incremental global distance field (lighting block B, item 2)

## Why
The GI is "static scenes only" because of how the global distance field (GDF) is maintained, not
because of its consumers. `GlobalDistanceField::Composite` hashes the ordered instance list every
frame (`global_distance_field.cpp:288-303`) and, on any change of that hash, marks all four
clipmaps dirty and recomposes every voxel of each of them over every instance (`:305-422`,
`global_sdf_composite_cs.hlsl:80-111`); a recentre does the same for the clipmap that moved
(`:181-214` sets `dirty` on a whole-voxel move). Each recompose bumps `m_sceneRevision`, which
discards the radiance-cache history (`frame_gi_passes.cpp:242-244`, retrace of every probe),
resets the SSGI/final-gather history (`ssgi_renderer.cpp:371-373`) and reruns the probe
classification (`radiance_cache.cpp:700-711`). The hash also changes when a texture streams in
(placeholder SRV -> real SRV) or a LOD file swaps, so Sponza's preload already shows 34 global
invalidations. Any moving entity would therefore reset the whole GI every frame. H-49 and H-54
(ledger) measured that a one-voxel recentre leaves the overlapping field bit-identical on
clipmaps 0 and 1 — the whole recompose produces exactly what a scroll of the old content plus the
entering slab would produce — so an incremental path can be held to bit-identity.

## What Changes
- Per-clipmap integer voxel shift and toroidal addressing: a recentre recomposes only the
  entering slabs; the readers (`gdf_surface.hlsli`, `sdf_reflections.hlsli`, `ssgi_surface.hlsli`)
  wrap; `ServiceFieldCompare` follows the new layout.
- Instances keyed by entity (`scene_sync.cpp:327`) instead of the ordered-list hash; a per-instance
  diff (old AABB ∪ new AABB, material-only changes) recomposes only the affected regions. The
  stored distance is untruncated today, so exactness needs a band: the composite clamps to the
  readers' maximum useful distance and the band is validated against the full field before it is
  relied on.
- Region-scoped knock-on: the radiance cache, SSGI/gather history and probe classification are
  invalidated only where the field changed; an authored mobility (Static / Movable) lets static
  scenes skip the diff entirely.
- Gates measured, not assumed: bit-identity of the scrolled field against a full recompose, GPU
  cost of a recentre and of a moving object (block B item 1 timings), Sponza ROIs unchanged, and a
  reference scene with a moving box whose GI follows it without global resets.

## Impact
- Affected specs: gdf-incremental-update (new)
- Affected code: runtime/engine/renderer/gi/global_distance_field.{h,cpp}; runtime/shaders/runtime/gi/global_sdf_composite_cs.hlsl, gdf_surface.hlsli, sdf_reflections.hlsli, ssgi_surface.hlsli; runtime/engine/renderer/frame/frame_gi_passes.cpp; runtime/engine/renderer/gi/radiance_cache.{h,cpp}; runtime/engine/renderer/passes/gi/ssgi_renderer.cpp; runtime/engine/renderer/scene/scene_sync.cpp; runtime/engine/ecs/scene/scene_component.h; tests test_gi_gpu.cpp, scripts/rendering/test_gdf_raw_lifecycle.ps1, new scripts/rendering/test_gdf_incremental.py
- Breaking change: NO (defaults reproduce the full recompose until the gates pass; the incremental path is behind a CVar until then)
- User benefit: dynamic objects and streaming no longer reset the GI; recentres and scene edits cost a slab, not four volumes.
