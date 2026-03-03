# Proposal: phase3-procedural-foot-placement

## Why
Characters' feet clip through uneven terrain, stairs, and slopes because skeletal animations are authored on a flat reference plane and the engine plays them back without any knowledge of the actual ground surface beneath each foot. This produces obvious visual artifacts whenever a character walks on any geometry other than a perfectly flat floor. Procedural IK adjusts each foot's world-space position to the actual traced ground surface every frame, making the character appear grounded regardless of terrain geometry.

## What Changes
- Add per-foot downward physics trace (Jolt ray cast) originating from the ankle bone world position, extended by a configurable reach distance
- Add 2-bone IK solver (thigh -> shin -> foot) that positions the foot at the traced hit point with the foot normal aligned to the surface normal
- Add body height adjustment that lowers or raises the character pelvis so that the higher foot does not overshoot the leg's maximum extension length
- Add FABRIK fallback solver for extreme slopes where the 2-bone analytical solution diverges
- Add `CFootIK` ECS component with per-foot config fields: `reach`, `blend_speed`, `max_angle_degrees`, `collision_layer_mask`
- Integrate the IK post-process pass into `AnimatorSystem` after all other post-process layers but before bone palette upload

## Impact
- Affected specs: `docs/specs/animation-system.md`, `docs/specs/skeletal-mesh.md`, `docs/specs/physics-system.md`
- Affected code: `src/animation/AnimatorSystem.cpp`, `src/animation/FootIKSolver.cpp` (new), `src/physics/PhysicsSystem.cpp`
- Breaking change: NO
- User benefit: Characters automatically look grounded on all terrain types, stairs, and slopes without requiring artists to hand-animate every surface variation, which would be impossible in a game with procedural or user-created terrain
