# Proposal: phase3-physics-batch-queries

## Why
Physics raycasts for AI line-of-sight, projectile tracing, and obstacle detection happen one at a time on the game logic thread. 100 AI agents each doing 5 raycasts equals 500 serial physics queries per frame, stalling the logic thread while Jolt processes each query synchronously. Batch queries dispatched in parallel using the engine job system would cut this to near-zero frame time by saturating available CPU cores. Without batching, scaling AI populations beyond ~30 agents causes measurable frame budget overruns.

## What Changes
- Add `BatchRaycastQuery` struct: `{origin, direction, max_distance, filter_mask, user_data}`
- Add `BatchSphereOverlapQuery` struct: `{center, radius, filter_mask, user_data}`
- Add `PhysicsWorld::batch_query(std::span<BatchRaycastQuery>, std::span<RaycastResult>)` that fans out to `JobSystem::submit_parallel()`
- Add `PhysicsWorld::batch_sphere_overlap(std::span<BatchSphereOverlapQuery>, std::span<OverlapResult>)` with same parallel dispatch
- Results written directly to caller-provided output spans (zero allocation)
- Lua binding `ctx.physics:batch_raycast(queries_table)` returning results table
- Lua binding `ctx.physics:batch_sphere_overlap(queries_table)` returning results table
- Update `PhysicsSystem::update()` to fence all batch jobs before stepping the simulation

## Impact
- Affected specs: `docs/specs/physics-system.md`, `docs/specs/ai-native-features.md`
- Affected code: `src/physics/physics_world.cpp`, `src/physics/physics_world.h`, `src/scripting/lua_physics.cpp`, `src/jobs/job_system.h`
- Breaking change: NO
- User benefit: AI-heavy scenes with 100+ agents performing per-frame visibility checks remain within the 2ms physics query budget. Projectile systems can test 64 simultaneous bullets per frame with no serial stall.
