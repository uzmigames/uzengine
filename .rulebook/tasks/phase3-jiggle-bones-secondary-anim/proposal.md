# Proposal: phase3-jiggle-bones-secondary-anim

## Why
Hair, capes, pouches, tails, and cloth accessories require secondary physics-driven animation to feel alive. Hard-coding these as rigidly animated bones looks stiff and unconvincing, and hand-animating secondary motion for every clip is prohibitively expensive in content production. A spring-damper simulation running as a post-process pass on selected bones produces organic secondary motion automatically, driven by the parent bone's velocity, with zero per-animation artist work.

## What Changes
- Add `CJiggleBone` ECS component with per-bone configuration: `stiffness` (float, spring constant), `damping` (float, 0-1), `follow_weight` (float, how strongly the bone tracks its animated position), `max_angle_degrees` (cone constraint), `collision_sphere_radius` (optional, 0 = disabled)
- Add `JiggleBoneSystem` that runs after `AnimatorSystem` completes the bone palette, iterates all `CJiggleBone` components, and applies spring-damper integration using the parent bone world transform delta as the driving force
- Apply the simulated position back to the bone's local transform relative to its parent before the palette is finalized
- Add optional per-bone collision sphere that prevents the bone from penetrating a small sphere volume around the root attachment point, useful for earrings and pouches
- Expose Lua API `ctx.anim:set_jiggle_config(bone_name, params_table)` for runtime tuning

## Impact
- Affected specs: `docs/specs/animation-system.md`, `docs/specs/skeletal-mesh.md`, `docs/specs/lua-scripting.md`
- Affected code: `src/animation/AnimatorSystem.cpp`, `src/animation/JiggleBoneSystem.cpp` (new), `src/scripting/LuaAnimBindings.cpp`
- Breaking change: NO
- User benefit: Characters gain organic secondary motion on accessories and hair at essentially no artist cost, significantly raising the visual quality bar for character presentation without adding animation content work
