# Proposal: phase3-sync-markers-inertialization

## Why
Blend transitions between animations cause visible pops and sliding feet because poses are interpolated via raw LERP, which does not account for gait phase alignment between the source and destination clips. A character switching from walk to run mid-stride will have one foot sliding backward during the blend window. Sync markers align the gait cycles of source and destination animations so left-foot-down always matches left-foot-down, and inertialization replaces raw LERP with a smooth velocity-decay curve that eliminates pose pops without requiring cross-fade blend trees.

## What Changes
- Add `AnimNotify` sync marker system: artists place named markers (e.g. `left_foot_down`, `right_foot_down`) at time positions in the animation asset stored in `.uzanim` metadata
- Add phase-sync logic in the state machine transition handler that finds the nearest matching marker in the destination animation and adjusts the destination start time to align gait phase
- Add inertialization buffer that records the last 3 frames of bone velocity (position and rotation delta per bone) at transition start
- Add inertial blend that, after the transition is triggered, decays recorded bone velocity to zero over a configurable duration using a critically-damped spring curve rather than blending between poses
- Replace the default LERP blend in all state machine cross-fade transitions with the inertialization path when inertialization is enabled on the `CAnimator` component

## Impact
- Affected specs: `docs/specs/animation-system.md`, `docs/specs/skeletal-mesh.md`
- Affected code: `src/animation/AnimatorSystem.cpp`, `src/animation/StateMachine.cpp`, `src/animation/Inertialization.cpp` (new), `src/assets/AnimAsset.cpp`
- Breaking change: NO
- User benefit: Locomotion transitions look professional with correctly aligned foot contacts and no visible pops, which is the single largest quality gap between indie and AAA character animation on flat terrain
