# Proposal: phase0_sponza-followup-04-decal-autolod-normal-consistency

## Why

`scripts/repair_sponza_decal_normals.{ps1,cs}` (task 02) fixed the flipped normals of
`decals_1st_floor1_dirt_decal_0.uzmesh` at the authored level. The SponzaTest handoff
(`docs/analysis/rendering/sponza-test-handoff.md:93`) records what is left: "The repaired decal has
correct LOD0/1 orientation; some LOD2/3 triangles remain inconsistent. LOD bias 0.1 favors detail
but does not force LOD0." LODs are generated at load by the auto-LOD simplifier
(`runtime/engine/renderer/mesh/auto_lod.h`: 3 levels at 0.5 / 0.25 / 0.125 of the triangles), so
the inconsistency is produced by the simplifier collapsing thin, nearly co-planar decal triangles
into faces whose winding no longer matches the vertex normals. At distance the decal then lights
from the wrong hemisphere (dark/white patch) and its SDF contribution flips sign locally.

## What Changes

A gtest that generates the auto LODs for the repaired decal mesh and counts triangles whose face
normal opposes the average vertex normal, then a simplifier fix: reject (or re-orient) collapses that
flip a face relative to its vertex normals. Verified on the decal and on the rest of Sponza (no
count increase on other meshes).

## Impact
- Affected specs: none
- Affected code: `runtime/engine/renderer/mesh/auto_lod.{h,cpp}`,
  `runtime/tests/engine/renderer/test_auto_lod_normals.cpp` (new)
- Breaking change: NO (LOD topology changes slightly for degenerate collapses only)
- User benefit: decals and thin geometry keep their orientation at every LOD
