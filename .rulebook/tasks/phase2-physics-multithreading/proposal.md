# Proposal: phase2-physics-multithreading

## Why
Jolt Physics ships with `JPH::JobSystemThreadPool`, a production-ready multithreaded job system, but the current UzEngine integration leaves it unconfigured so Jolt falls back to single-threaded execution. With 100+ rigid bodies, single-threaded broad phase and constraint solving consumes the full frame budget for game logic on a single core, leaving 7+ cores idle. Enabling Jolt's job system is the highest-leverage physics performance improvement available with zero algorithmic changes.

Jolt's internal architecture is designed from the ground up for multithreading: the broad phase uses a lock-free AABB tree, island detection is parallelised, and constraint solving uses a split-impulse solver that partitions independent islands across job batches. All of this work is done inside Jolt's own job system, meaning UzEngine does not need to restructure any game code — it simply needs to provide a properly configured `JobSystemThreadPool` at initialisation time. The payoff scales linearly with core count for most simulation configurations.

The complementary concern is memory: Jolt's temp allocator is used for per-step scratch buffers (contact manifolds, velocity deltas, island arrays). Leaving it at the default unbounded heap allocation causes heavy per-step fragmentation. A fixed-size `TempAllocatorImpl` with a pre-committed 32 MB pool eliminates this fragmentation entirely, and the pool size can be reduced for simpler scenes via CVar to reduce memory pressure on constrained platforms.

## What Changes
- Instantiate `JPH::JobSystemThreadPool` in `src/physics/physics_world.cpp` at `PhysicsWorld::Init()` with `worker_count = std::max(1u, std::thread::hardware_concurrency() - 2)` to reserve cores for the game logic and render threads
- Integrate `JPH::TempAllocatorImpl` with a configurable pool size (default 32 MB, exposed as CVar `phys.temp_alloc_mb`); fall back to `TempAllocatorMalloc` only if the fixed pool is exhausted, with a warning logged
- Implement `UzJobSystem` — a thin `JPH::JobSystem` subclass in `src/physics/uz_job_system.h` that delegates to UzEngine's existing fiber/job scheduler rather than spawning its own threads, avoiding over-subscription when the engine already manages a thread pool
- Add a `phys.worker_threads` CVar (range 0–`hardware_concurrency`) so developers can tune the thread count at runtime; 0 means use the default `hardware_concurrency - 2` formula
- Add a `phys.mt_enabled` CVar (default 1) to fall back to single-threaded mode for determinism testing and platform compatibility validation
- Add Tracy instrumentation by subclassing `JPH::JobSystem::Job::Execute` to emit `TracyZoneScoped` on each Jolt job execution, so Jolt physics jobs appear in Tracy timelines attributed to the correct thread
- Add a dedicated `phys` Tracy plot group with plots for `broad_phase_ms`, `narrow_phase_ms`, `constraint_solve_ms`, `integrate_ms` extracted from Jolt's built-in `PhysicsSystem::GetStats()` output
- Expose `engine.get_physics_stats` IPC handler returning `{sim_time_ms, body_count, active_body_count, sleeping_body_count, worker_threads, temp_alloc_used_bytes, temp_alloc_peak_bytes}`
- Add thread-safety audit: run the engine with Jolt's built-in `JPH_ENABLE_ASSERTS` in Debug builds to catch any remaining single-thread assumptions in game code that calls physics from arbitrary threads
- Write integration tests in `tests/physics/multithreading_test.cpp` that simulate 500 rigid bodies for 1000 steps and verify deterministic output matches single-threaded reference output

## Impact
- Affected specs: `docs/specs/physics-system.md`, `docs/specs/debug-profiling.md`
- Affected code: `src/physics/physics_world.cpp`, `src/physics/physics_world.h`, `src/physics/uz_job_system.h`, `src/physics/uz_job_system.cpp`, `src/ipc/ipc_handlers.cpp`
- Breaking change: NO — multithreading is transparent to all game code; `phys.mt_enabled 0` restores single-threaded behaviour; simulation results are deterministic at fixed step
- User benefit: Scenes with 100+ rigid bodies simulate in a fraction of the previous frame time; Tracy profiling shows Jolt jobs distributed across CPU cores with per-phase timing; VRAM-like pressure from temp allocator fragmentation is eliminated
