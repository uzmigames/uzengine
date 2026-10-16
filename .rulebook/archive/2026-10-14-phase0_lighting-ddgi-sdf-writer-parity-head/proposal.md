# DDGI and SDF probe writers compared on the current scene set

## Why

Followup-01 closed the writer-level DDGI / SDF parity at 0.9662 / 0.9637 (block B.6's D4), but those values
predate 513e2983 (mesh decals), b8ff5077 (sky capture) and 4501fa74 (recalibration), and since 513e2983 the DDGI
writer traces Sponza's dirt-decal quads as opaque occluders (TLASManager::Rebuild takes every draw) while the SDF
writer no longer does. H-61's open term (the DDGI escapes 6.5-9 % more sky than the cache inside the building, where
the field reports a surface) was never measured against r.GI.MeshSdf.TwoSided.

Origin: followup-01 close-out (2026-10-08).

## What Changes

- TLASManager::Rebuild: skip translucent draws and mesh decals through one predicate shared with the distance-field occluder set, with a gtest.
- Re-measure the writers before and after (scripts/rendering/test_gi_energy_gpu.ps1 -Frames 200, scripts/rendering/gi_energy_populations.py) on a pre-registered population.
- Measure H-61's term with r.GI.MeshSdf.TwoSided 0 and 1 (give scripts/rendering/run_probe_indirect_residual.py a CVar override).

## Impact
- Affected specs: lighting-ddgi-sdf-writer-parity-head (new)
- Affected code: see the items; gates are pre-registered per item before its first run
- Breaking change: NO until a gate passes
- User benefit: the open points of the closed followups have an owner and a plan.
