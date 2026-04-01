# Proposal: phase4-4.2.0-physics-jolt-core

## Why
UzEngine has zero physics capabilities. Without rigid body dynamics, collision detection, and trigger volumes, no gameplay can be built — objects can't fall, collide, or interact. Physics is the #1 blocker for a "playable scene" milestone. Jolt Physics is the chosen library: open-source, high-performance, used by Horizon Forbidden West, and designed for modern multi-threaded engines.

## What Changes
- Jolt Physics integration via `scripts/setup.ps1` + Zig build linkage
- PhysicsWorld singleton managing JPH::PhysicsSystem with 120Hz fixed timestep
- CRigidBody ECS component (dynamic/static/kinematic) with mass, friction, restitution, damping
- 4 collider components: Box, Sphere, Capsule, Mesh (convex hull + triangle mesh)
- ShapeFactory for compound shape composition (multiple colliders per entity)
- Collision layer system (8 layers with configurable interaction matrix)
- ContactListener + TriggerListener → EventBus<CollisionEvent/TriggerEvent>
- BodySync system for bidirectional Jolt ↔ ECS transform sync
- Scene serialization for all physics components
- Physics demo with falling cubes on a ground plane

## Impact
- Affected specs: physics-system.md (primary), scene-system.md (serialization)
- New directory: `runtime/engine/physics/`
- New thirdparty: Jolt Physics v5.x in `runtime/thirdparty/JoltPhysics/`
- Dependencies: ECS (EnTT), Scene system, EventBus
- Breaking change: NO
- Performance target: 1000 dynamic bodies at 120Hz within 2ms physics step
