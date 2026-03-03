# Proposal: phase3-distance-matching

## Why
Footstep sounds, foot IK targets, and sync marker events desync from actual character movement when the character's world-space speed does not match the animation's authored root motion speed. A character walking at 2.5 m/s playing a 3 m/s walk animation will show visually sliding feet because the animation playback is locked to wall-clock time rather than to the world-space distance traveled. Distance matching adjusts playback rate frame-by-frame so that the foot lands at the same world-space position it was authored for, making locomotion feel physically accurate across the full speed range supported by a locomotion blend tree.

## What Changes
- Add per-animation distance curve authoring support in the `.uzanim` metadata: a 1D curve mapping animation time (seconds) to cumulative root motion distance traveled (meters), baked at import time from the root motion track
- Add `DistanceMatchingSystem` ECS component that each tick queries the distance curve to find the animation time at which the root would be at the character's current cumulative travel distance, then sets the clip playback position to that time
- Add playback rate adjustment loop as a fallback when the distance is ahead of the animation maximum (clamped to configurable max rate multiplier, default 1.5x)
- Integrate with sync markers so that after distance matching repositions the playback cursor, the nearest sync marker is used as the gait phase reference for cross-animation blends
- Expose per-clip distance matching enable flag in the state machine node config, defaulting to off for non-locomotion clips such as attack or death animations

## Impact
- Affected specs: `docs/specs/animation-system.md`, `docs/specs/skeletal-mesh.md`
- Affected code: `src/animation/AnimatorSystem.cpp`, `src/animation/DistanceMatchingSystem.cpp` (new), `src/assets/AnimAsset.cpp`, `tools/importer/AnimImporter.cpp`
- Breaking change: NO
- User benefit: Locomotion animations remain physically grounded across all movement speeds without requiring a separate authored animation for every speed value, which eliminates foot sliding and desync between footstep audio and visual contact
