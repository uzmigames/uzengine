# Proposal: phase3-audio-animation-sync

## Why
Footstep sounds, weapon impact sounds, and voiced grunts require frame-accurate triggering tied to specific points in an animation — the exact frame when a foot contacts the ground, not an approximate timer. IK foot placement similarly requires knowing the precise contact frame to correct foot sliding. Neither is achievable without an AnimNotify system that fires callbacks at animation-specified frame boundaries rather than relying on game-code timers which drift under variable frame rates.

## What Changes
- `AudioAnimNotify` type added to the animation state machine definition, storing frame index, asset reference, and volume/pitch parameters
- `AnimationSystem` evaluates notify events at the end of each tick by comparing the previous and current normalized playback position, firing any notifies whose frame falls within the elapsed window
- Fired notifies forwarded to `IAudioEngine::play_oneshot()` with the character's world position as the emitter origin
- Lua API: `ctx.anim:on_notify("Footstep_L", handler)` registers a per-character callback for named notify events
- Visual timeline indicators rendered in the animation editor showing notify positions on the timeline scrubber
- Notify evaluation is frame-rate-independent: sub-frame interpolation ensures a notify is never skipped on a long frame

## Impact
- Affected specs: `docs/specs/animation-system.md`, `docs/specs/audio-system.md`, `docs/specs/lua-scripting.md`
- Affected code: `src/animation/`, `src/audio/`, `src/lua_bindings/`, `src/editor/animation_editor/`
- Breaking change: NO
- User benefit: Footsteps, impacts, and voiced reactions play at the exact animation frame they are authored on, eliminating the timing drift that occurs when these events are triggered by game-code timers.
