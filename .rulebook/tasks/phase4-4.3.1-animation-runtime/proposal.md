# Proposal: phase4-4.3.1-animation-runtime

## Why
With skeletal mesh and GPU skinning from phase4-4.3.0, the animation runtime brings characters to life. This covers clip playback (keyframe interpolation), state machines (idle→walk→run transitions), blend spaces, root motion extraction, and animation notify events. Without this, skeletal meshes are frozen in T-pose.

## What Changes
- AnimationClip: keyframe data with per-bone channels (position, rotation, scale)
- .uzanim binary format for animation clips
- AnimationPlayer: play, pause, speed, loop, crossfade between clips
- AnimStateMachine: states with transitions driven by float/bool parameters
- BlendSpace1D: interpolate between clips based on a single parameter (e.g., speed)
- Root motion: extract root bone translation → apply to CharacterController
- AnimNotify: fire events at specific keyframes (footsteps, sound, VFX)
- Lua animation API: uz.animation.play, set_parameter, etc.

## Impact
- Depends on: phase4-4.3.0-skeletal-mesh-gpu-skinning (skeleton, bone palette, GPU skinning)
- Optionally uses: phase4-4.2.1 (character controller for root motion), phase4-4.4.0 (audio for notify)
- New directory: `runtime/engine/animation/` (extends skeleton data)
- Breaking change: NO
- Performance target: 100 animated characters with state machines < 0.5ms CPU
