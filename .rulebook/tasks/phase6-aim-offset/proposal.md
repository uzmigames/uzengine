# Proposal: phase3-aim-offset

## Why
Weapon and head aiming require smooth 2D additive pose offsets blended on top of a locomotion state machine. Without aim offsets, a character's gun always points forward regardless of where the player is looking, making third-person shooters and over-the-shoulder games unplayable. The current animation system has no layer above the base state machine to inject additive directional poses, so aiming must be faked with bone constraints alone, which breaks inverse kinematics and recoil.

## What Changes
- Add `AimOffset` asset type (`.uzaimoffset` INI format) storing a 2D grid of additive poses indexed by pitch (rows) and yaw (columns) with configurable range and count
- Add `AimOffsetSampler` that bilinearly interpolates between the four nearest grid poses given a current pitch/yaw input
- Add additive blending layer in `AnimatorSystem` that applies the sampled aim pose on top of the base state machine output before the bone palette upload
- Add smooth input interpolation (configurable speed, degrees/s) to prevent snapping between aim poses
- Add `CAimOffset` ECS component referencing the asset and storing current pitch/yaw state
- Expose Lua API `ctx.anim:set_aim_direction(pitch, yaw)` that writes into the component each tick

## Impact
- Affected specs: `docs/specs/animation-system.md`, `docs/specs/asset-pipeline.md`, `docs/specs/lua-scripting.md`
- Affected code: `src/animation/AnimatorSystem.cpp`, `src/animation/AimOffsetSampler.cpp` (new), `src/assets/AimOffsetAsset.cpp` (new), `src/scripting/LuaAnimBindings.cpp`
- Breaking change: NO
- User benefit: Developers can author a small grid of aim poses in the editor and get full directional aiming for free at runtime, which is required for every third-person shooter, RPG combat system, and cover-based game
