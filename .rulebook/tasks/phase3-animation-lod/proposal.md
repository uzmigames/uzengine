# Proposal: phase3-animation-lod

## Why
All entities with a `CSkeletalMesh` component currently compute a full bone palette every frame regardless of their distance from the camera. An entity 50 meters away occupies roughly 2 pixels on screen but still evaluates 64 bones, runs all IK post-process passes, and uploads a full constant buffer to the GPU. In scenes with 50-100 animated characters this is a significant and entirely wasted CPU cost. Animation LOD reduces the active bone count proportionally with distance, keeping only structurally important bones at far range.

## What Changes
- Add 4 default LOD levels in `CSkeletalMesh`: LOD0 (full skeleton, 0-10m), LOD1 (50% bones, 10-25m), LOD2 (25% bones, 25-50m), LOD3 (10% bones, 50m+); distance thresholds are configurable per component
- Add bone mask asset per LOD level that lists which bones are active; inactive bones inherit their parent transform without running any IK or post-process solvers
- Define a set of always-active critical bones that are never culled regardless of LOD: root, pelvis, spine chain, neck, head, and weapon attach sockets
- Skip `CFootIK`, `CJiggleBone`, and `CAimOffset` post-process passes entirely when the entity is at LOD2 or LOD3
- Add debug visualization mode (toggled via console command `anim_lod_debug 1`) that color-codes active bones per LOD level in the editor viewport

## Impact
- Affected specs: `docs/specs/animation-system.md`, `docs/specs/skeletal-mesh.md`, `docs/specs/debug-profiling.md`
- Affected code: `src/animation/AnimatorSystem.cpp`, `src/components/CSkeletalMesh.h`, `src/animation/BoneMask.cpp` (new)
- Breaking change: NO
- User benefit: Scenes with many animated characters become feasible on mid-range hardware; CPU animation budget scales with actual screen contribution of each character rather than burning full cost for distant background characters
