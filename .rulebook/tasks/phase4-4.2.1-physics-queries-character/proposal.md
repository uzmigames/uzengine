# Proposal: phase4-4.2.1-physics-queries-character

## Why
With rigid bodies and colliders from phase4-4.2.0, the next critical piece is physics queries (raycasts, shape casts, overlaps) and a character controller. Raycasts are essential for shooting, UI interaction, ground detection, and AI line-of-sight. The character controller (Jolt CharacterVirtual) provides smooth player movement with grounding, stepping, and slope handling — required for any playable character.

## What Changes
- PhysicsQueries namespace: Raycast, RaycastAll, SphereCast, BoxCast, CapsuleCast, OverlapSphere, OverlapBox
- CCharacterController ECS component wrapping JPH::CharacterVirtual
- CharacterMovementSystem with gravity, slope sliding, step-up, moving platform support
- Physics debug draw (wireframe colliders, contact points, raycasts) via JPH::DebugRenderer
- Full Lua physics API: uz.physics.raycast, overlap, rigid body control, character movement, collision/trigger callbacks

## Impact
- Depends on: phase4-4.2.0-physics-jolt-core (must be complete first)
- Affected code: runtime/engine/physics/, runtime/engine/scripting/api/
- New files: ~12 .h/.cpp files
- Breaking change: NO
- User benefit: Playable character movement, physics-based gameplay, Lua scripting of physics interactions
