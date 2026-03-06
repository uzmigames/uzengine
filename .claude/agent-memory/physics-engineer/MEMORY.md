# Physics Engineer -- Persistent Memory

## Project: UzEngine
Jolt Physics (MIT) targeted for Phase 3. Lives entirely inside game.dll.

## Key Paths
- `docs/specs/physics-system.md` -- complete architecture spec
- `docs/reviews/physics-review.md` -- AAA gap analysis (16 critical gaps)
- `src/game/physics/` -- PhysicsWorld, body sync, queries, contact listener
- `src/engine/ecs/components.h` -- CRigidBody, CCollider*, CTrigger, CCharacterController

## Architecture Rules
- Jolt ONLY in game.dll -- never render.exe or uz_engine.lib
- Fixed 60 Hz timestep with accumulator pattern (not frame-rate dependent)
- Transform sync: AFTER physics step, BEFORE script on_update
- ContactListener dispatches to EventBus queue, drained on game thread
- Debug draw guarded by #ifdef UZ_EDITOR

## Collision Layers
- 16 max, configured in .uzproj [physics.layers] and [physics.collision_matrix]
- Never hardcode layer numbers -- always use named constants from project config

## Jolt Version
- Target: JoltPhysics 5.1.0+ via vcpkg

## AAA Gap Analysis (2026-03-02)
Identified 16 critical gaps in physics-review.md:
- Phase 3 must-adds: PhysMaterials, CCD config, batch queries, threading, profiler, regions
- Phase 4: Constraints, destruction, vehicles, cloth, LOD, ropes, soft body, buoyancy
- Phase 5: Deterministic physics, fluid sim (SPH)
- Total estimated effort: 27-34 weeks across all phases

## Key Jolt Features to Leverage
- `JPH::VehicleConstraint` for vehicle physics
- `JPH::SoftBodyCreationSettings` for cloth and soft body
- `JPH::PhysicsMaterial` subclass for surface types
- `MotionQuality::LinearCast` for CCD
- `JobSystemThreadPool` vs `JobSystemSingleThreaded` for determinism toggle
- `Constraint::GetTotalLambda()` for break force detection
