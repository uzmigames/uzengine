# Proposal: phase10_10.10.0-physics-contact-listener-registry

## Why
Phase 9.18 (Jolt fracture bridge) needs a hook where the VFX subsystem registers `JPH::ContactListener`-style callbacks. The PhysicsWorld today owns the Jolt system but exposes no listener-registration API — gameplay code that wants per-contact events has to fork the physics init.

## What Changes
Add `PhysicsWorld::RegisterContactListener(IPhysicsContactListener*)` + `UnregisterContactListener`. Internal impl multiplexes callbacks from Jolt's single listener slot to N registered subscribers. A future `IPhysicsFractureListener` (9.18) plugs in via the same pattern.

## Impact
- Affected specs: docs/engine/physics/
- Affected code: runtime/engine/physics/physics_world.*
- Breaking change: NO
- User benefit: Physics events become subscribable by any subsystem.
- Estimated effort: M

## Source
UE5 FCollisionNotifyInfo + Jolt's `ContactListener` multiplexer pattern.
