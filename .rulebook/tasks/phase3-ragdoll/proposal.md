# Proposal: phase3-ragdoll

## Why

Characters currently have no physical response to death, knockback, or environmental forces — they simply stop animating or play a death clip. Ragdoll simulation gives characters believable, physics-driven collapse and impact reactions that are impossible to hand-animate for every scenario. Without ragdoll, character death and hit reactions feel static and break immersion in any action or combat game.

## What Changes

- Define `.uzphys` Physics Asset format (INI-like): per-bone rigid body shape (capsule/box/sphere), mass, and Jolt constraint type (swing-twist, hinge, cone)
- Implement `CRagdoll` component that creates Jolt `Ragdoll` body chain from the Physics Asset at runtime
- Add blend-in transition: lerp from current animated pose to ragdoll pose over a configurable duration when triggered
- Implement active ragdoll (motor-driven): Jolt `MotorConstraint` drives each joint toward the target animated pose with configurable stiffness/damping
- Add impulse API for impact response: `ctx.ragdoll:apply_impulse(bone_name, velocity, world_pos)`
- Implement blend-out / recovery: when ragdoll settles, optionally blend back to a get-up animation clip
- Write per-bone transform readback each frame to update the skinning bone palette from Jolt body positions
- Expose state events to EventBus: `ragdoll.activated`, `ragdoll.settled`, `ragdoll.recovered`

## Impact

- Affected specs: `docs/specs/skeletal-mesh.md`, `docs/specs/physics-system.md`, `docs/specs/animation-system.md`
- Affected code: `src/physics/ragdoll.cpp`, `src/animation/anim_state_machine.cpp`, `src/renderer/skinning_pass.cpp`
- Breaking change: NO
- User benefit: Characters react believably to death and impacts with fully physics-simulated collapse, dramatically improving game feel in combat and action scenarios
