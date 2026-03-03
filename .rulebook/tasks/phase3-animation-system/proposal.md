# Proposal: phase3-animation-system

## Why

Skeletal meshes currently have no runtime animation capability — all meshes remain static after import. Games require characters and objects to move fluidly through keyframe animation, blending, and procedural overrides. Without an animation system, there is no way to play walk cycles, attack sequences, or any time-varying skeletal deformation, making the engine unsuitable for any character-based game.

## What Changes

- Add `AnimClip` resource that samples bone transforms from keyframe tracks (position, rotation, scale per bone)
- Implement `AnimStateMachine` with states, transitions, blend conditions, and the `.uzanim_sm` INI-like file format
- Add blend tree nodes: linear blend, additive blend, layered blend (per bone mask), blend space 1D/2D
- Implement root motion extraction — strips root bone translation/rotation from clip and applies to character capsule
- Add `AnimNotify` event system: fire audio, VFX, Lua callbacks at specific keyframe timestamps
- Implement two-bone IK solver (arm/leg reach) and FABRIK chain solver for spine/tail chains
- Integrate with `phase3-skeletal-mesh` bone palette so state machine output drives GPU skinning each frame
- Expose Lua API: `ctx.anim:play()`, `ctx.anim:set_param()`, `ctx.anim:get_current_state()`

## Impact

- Affected specs: `docs/specs/animation-system.md`, `docs/specs/skeletal-mesh.md`, `docs/specs/lua-scripting.md`
- Affected code: `src/animation/`, `src/renderer/skinning_pass.cpp`, `src/scripting/lua_anim_bindings.cpp`
- Breaking change: NO
- User benefit: Enables full character animation — walk, run, attack, death, and any blended motion for player and NPC characters
