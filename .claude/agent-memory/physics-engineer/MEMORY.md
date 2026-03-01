# Physics Engineer — Persistent Memory

## Project: UzEngine
Jolt Physics (MIT) targeted for Phase 3. Lives entirely inside game.dll.

## Key Paths
- `docs/specs/physics-system.md` — complete architecture spec
- `src/game/physics/` — PhysicsWorld, body sync, queries, contact listener
- `src/engine/ecs/components.h` — CRigidBody, CCollider*, CTrigger, CCharacterController

## Architecture Rules
- Jolt ONLY in game.dll — never UzEngine.exe or uz_engine.lib
- Fixed 60 Hz timestep with accumulator pattern (not frame-rate dependent)
- Transform sync: AFTER physics step, BEFORE script on_update
- ContactListener dispatches to EventBus queue, drained on game thread
- Debug draw guarded by #ifdef UZ_EDITOR

## Collision Layers
- 16 max, configured in .uzproj [physics.layers] and [physics.collision_matrix]
- Never hardcode layer numbers — always use named constants from project config

## Jolt Version
- Target: JoltPhysics 5.1.0+ via vcpkg
