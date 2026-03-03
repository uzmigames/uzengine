# Proposal: phase3-motion-warping

## Why
Characters slide past interaction points such as doors, ledge grabs, and NPCs because root motion plays on a fixed trajectory that does not account for the target position. Motion warping stretches or compresses root motion curves within a defined time window so the character root arrives exactly at the desired world-space position and rotation. This technique is standard in third-person action games and is the foundation of Unreal Engine 5's Motion Warping plugin.

## What Changes
- Add `MotionWarpTarget` struct with fields: `name` (string key), `position` (float3), `rotation` (quaternion), `window_start_s` (float), `window_end_s` (float)
- Add root motion trajectory pre-pass that extracts the unwarped delta transform for the warp window from the animation clip
- Add per-frame warp matrix computation that scales the trajectory delta to reach the registered target within the remaining window duration
- Apply the additive warp transform to the root bone after the base animation pose is sampled, before the bone palette is uploaded to the GPU
- Expose Lua API `ctx.anim:set_warp_target(name, pos, rot)` and `ctx.anim:clear_warp_target(name)` on the `CAnimator` node

## Impact
- Affected specs: `docs/specs/animation-system.md`, `docs/specs/skeletal-mesh.md`, `docs/specs/lua-scripting.md`
- Affected code: `src/animation/AnimatorSystem.cpp`, `src/animation/RootMotionSolver.cpp` (new), `src/scripting/LuaAnimBindings.cpp`
- Breaking change: NO
- User benefit: Interaction animations (vaults, grabs, dialogue approach) land precisely on target points without requiring hand-authored root motion for every possible distance offset, saving significant animation content work
