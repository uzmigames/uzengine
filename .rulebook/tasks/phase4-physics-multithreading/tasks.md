## 1. Jolt Thread Pool Configuration

- [ ] 1.1 Instantiate `JPH::JobSystemThreadPool` in `src/engine/physics/physics_world.cpp` inside `PhysicsWorld::Init()` with `worker_count = std::max(1u, std::thread::hardware_concurrency() - 2)` to reserve the main and game logic threads
- [ ] 1.2 Store the `JPH::JobSystemThreadPool` instance as `std::unique_ptr<JPH::JobSystemThreadPool> m_job_system` in `PhysicsWorld`'s member variables and pass it to `JPH::PhysicsSystem::Init()`
- [ ] 1.3 Add CVar `phys.worker_threads` (int, range `0` to `hardware_concurrency`, default `0` = auto) that overrides the worker count formula; when `0` use `hardware_concurrency - 2`; re-apply the setting at `PhysicsWorld::Reinit()` when the CVar changes

## 2. TempAllocator Configuration

- [ ] 2.1 Instantiate `JPH::TempAllocatorImpl` with a pre-committed pool of `phys.temp_alloc_mb * 1024 * 1024` bytes (default 32 MB) in `PhysicsWorld::Init()`; pass it to `JPH::PhysicsSystem::Init()`
- [ ] 2.2 Add CVar `phys.temp_alloc_mb` (int, default `32`, min `8`, max `256`) controlling the temp allocator pool size; document that this value must be set before `PhysicsWorld::Init()` in the project loader
- [ ] 2.3 Fall back to `JPH::TempAllocatorMalloc` if the pre-committed pool is exhausted (caught via `JPH::TempAllocatorImpl`'s OOM callback) and emit `UZ_LOG_WARN("Jolt temp allocator pool exhausted (%d MB), falling back to malloc -- increase phys.temp_alloc_mb", phys.temp_alloc_mb)`

## 3. UzJobSystem Integration

- [ ] 3.1 Create `src/engine/physics/uz_job_system.h` declaring `UzJobSystem : public JPH::JobSystem` that delegates job creation and scheduling to UzEngine's fiber/job scheduler (from `phase2-job-system-design`) rather than spawning its own threads; this prevents over-subscription when both the engine job scheduler and Jolt's `JobSystemThreadPool` would otherwise each spawn `N-2` threads
- [ ] 3.2 Implement `UzJobSystem::CreateJob()` in `src/engine/physics/uz_job_system.cpp` wrapping Jolt's job function in an engine `JobHandle` via `JobDispatcher::submit()`; implement `UzJobSystem::FreeJob()` to release the engine job handle
- [ ] 3.3 Implement `UzJobSystem::QueueJob()` forwarding to `JobDispatcher::submit()` with `PRIORITY_HIGH`; implement `UzJobSystem::QueueJobs()` (batch variant) submitting all jobs in a single `submit_parallel()` call
- [ ] 3.4 Use `UzJobSystem` instead of `JPH::JobSystemThreadPool` when the UzEngine job system is initialized (detected via `JobDispatcher::IsInitialized()`); fall back to `JPH::JobSystemThreadPool` in headless/test mode where the engine job system is not running

## 4. Multi-Threading Enable/Disable CVar

- [ ] 4.1 Add CVar `phys.mt_enabled` (int, default `1`) that when set to `0` replaces `UzJobSystem` / `JPH::JobSystemThreadPool` with `JPH::JobSystemSingleThreaded` for the next `PhysicsWorld::Reinit()` call; log the mode change with `UZ_LOG_INFO`
- [ ] 4.2 Document `phys.mt_enabled 0` in `docs/specs/physics-system.md` as the recommended setting for determinism testing and platform compatibility validation

## 5. Tracy Instrumentation

- [ ] 5.1 Subclass `JPH::JobSystem::Job` in `src/engine/physics/physics_tracy_job.h` to emit `TracyZoneScoped` on `Execute()`, naming the zone with Jolt's internal job name string; use this subclass in `UzJobSystem::CreateJob()`
- [ ] 5.2 Add a dedicated `phys` Tracy plot group in `PhysicsWorld::PostStep()` by reading `JPH::PhysicsSystem::GetStats()` and emitting: `TracyPlot("phys.broad_phase_ms", stats.mBroadPhaseTime)`, `TracyPlot("phys.narrow_phase_ms", stats.mNarrowPhaseTime)`, `TracyPlot("phys.constraint_solve_ms", stats.mConstraintSolveTime)`, `TracyPlot("phys.integrate_ms", stats.mIntegrateTime)`

## 6. IPC Handler

- [ ] 6.1 Add `engine.get_physics_stats` IPC handler in `src/ipc/ipc_handlers.cpp` serializing the response `{"sim_time_ms": 1.2, "body_count": 150, "active_body_count": 48, "sleeping_body_count": 102, "worker_threads": 6, "temp_alloc_used_bytes": 4194304, "temp_alloc_peak_bytes": 8388608}` from Jolt's `PhysicsSystem::GetStats()` and the temp allocator's `GetTotalSize()` / `GetTotalUsed()` methods

## 7. Debug Assertions

- [ ] 7.1 Enable `JPH_ENABLE_ASSERTS` in CMakeLists.txt for Debug builds of the `physics_world` target by adding `target_compile_definitions(engine_physics PRIVATE $<$<CONFIG:Debug>:JPH_ENABLE_ASSERTS>)`
- [ ] 7.2 Add a thread-safety audit comment block in `src/engine/physics/physics_world.cpp` listing all functions that must be called from the game logic thread only (e.g. `AddBody`, `RemoveBody`, `SetLinearVelocity`) and assert `std::this_thread::get_id() == g_game_logic_thread_id` in Debug builds

## 8. Testing

- [ ] 8.1 Create `tests/physics/test_physics_multithreading.cpp` instantiating `PhysicsWorld` with `phys.mt_enabled = 1` and simulating 500 rigid bodies dropped onto a static plane for 1000 fixed steps
- [ ] 8.2 Run the same 500-body simulation with `phys.mt_enabled = 0` (single-threaded) and compare all body transform outputs; assert that multi-threaded and single-threaded results match within `1e-4f` (determinism test)
- [ ] 8.3 Test `phys.worker_threads = 1` results in a job system with exactly 1 worker thread; verify via a `UzJobSystem::GetNumWorkerThreads()` accessor
- [ ] 8.4 Test temp allocator fallback: configure `phys.temp_alloc_mb = 1` and simulate a large contact-heavy scene (200 bodies stacked); verify the fallback-to-malloc warning is emitted and the simulation continues without crashing
- [ ] 8.5 Test `engine.get_physics_stats` IPC handler returns valid JSON with `body_count >= 500` after the 500-body simulation setup
- [ ] 8.6 Test `engine.get_physics_stats` returns `active_body_count + sleeping_body_count == body_count`
- [ ] 8.7 Test `phys.mt_enabled = 0` switches Jolt to `JobSystemSingleThreaded` mode and no additional threads are spawned (verify `std::thread::hardware_concurrency()` unchanged before and after)
- [ ] 8.8 Test `PhysicsWorld::Reinit()` after changing `phys.worker_threads` creates a new job system with the updated thread count without a crash or resource leak
- [ ] 8.9 Test Tracy plots are emitted: verify `broad_phase_ms`, `narrow_phase_ms`, `constraint_solve_ms`, `integrate_ms` are all non-negative after one simulation step
- [ ] 8.10 Test `JPH_ENABLE_ASSERTS` fires in Debug builds when `AddBody` is called from a non-game-logic thread (use `std::thread` to call it from a background thread and verify the assertion is triggered)
- [ ] 8.11 Run coverage -- all new files in `src/engine/physics/` must reach >= 95% line and branch coverage before marking complete
