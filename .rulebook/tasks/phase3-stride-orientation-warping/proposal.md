# Proposal: phase3-stride-orientation-warping

## Why
Characters currently move at whatever angle their root motion or velocity dictates, but the leg animation always plays as if moving straight forward. A character strafing left still cycles through a pure forward walk, producing obvious foot sliding where the legs move in the wrong direction relative to actual displacement. Stride warping compresses and stretches per-leg stride vectors to match the actual movement direction, and orientation warping twists the hip bone to face the velocity direction smoothly — together eliminating the foot-sliding artifact without requiring a full directional locomotion animation set.

## What Changes
- Stride vector computation: each frame, compute the angle between the animation's baked root motion direction and the character's actual velocity direction
- Per-leg stride offset: modify the IK target position for each foot by projecting the stride vector onto the actual movement direction, scaling leg extension/compression accordingly
- Hip rotation blend: rotate the pelvis/hip bone toward the velocity direction at a configurable blend rate (degrees/sec), clamped to a maximum twist angle to avoid unnatural poses
- Integration with the existing motion warping system so stride and orientation warping can be enabled/disabled per animation state in the blend tree
- Per-character skeleton configuration asset specifying which bones are the left foot IK target, right foot IK target, and hip root

## Impact
- Affected specs: `docs/specs/animation-system.md`, `docs/specs/skeletal-mesh.md`
- Affected code: `src/animation/`, `src/components/character_controller.h`
- Breaking change: NO
- User benefit: Characters moving in any direction relative to their facing angle look correct without requiring artists to author a full 8-directional locomotion animation set, saving significant animation production cost.
