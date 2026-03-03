## 1. JobDispatcher API Design

- [ ] 1.1 Write `docs/specs/engine/job-system.md` defining the `JobDispatcher` C++ API: `submit(fn)` returning a `JobHandle`, `submit_parallel(fn, count)` for data-parallel batches, `wait(JobHandle)` blocking until a job completes, and `fiber_yield()` suspending the current fiber and returning it to the work-stealing queue
- [ ] 1.2 Document the thread model in the spec: Main thread (render commands), Game Logic thread (ECS + Lua + physics dispatch), and N worker threads computed as `std::max(1u, hardware_concurrency - 2)`; include a diagram showing data flow between threads
- [ ] 1.3 Define `JobHandle` as a 64-bit value encoding generation + index into the job pool, making it safe to hold across frames without dangling handle issues
- [ ] 1.4 Define job priority levels in the spec: `CRITICAL` (physics step, render submission), `HIGH` (animation blend trees, AI ticks), `NORMAL` (particle simulation, audio update), `IDLE` (asset streaming, telemetry flush)

## 2. Work-Stealing Scheduler Design

- [ ] 2.1 Document the Chase-Lev work-stealing deque data structure in `docs/specs/engine/job-system.md`: each worker thread owns a local deque, pushes to the bottom, pops from the bottom (LIFO for cache locality), and steals from the top of other threads' deques (FIFO to minimize contention)
- [ ] 2.2 Document the lock-free Chase-Lev deque operations: `push_bottom(job)`, `pop_bottom()`, and `steal_top()` using atomic compare-and-swap on the top and bottom indices; reference the Lê et al. 2013 verified implementation as the baseline
- [ ] 2.3 Document the memory layout of the job pool: fixed-size 4 KB job slots pre-allocated in a ring buffer at engine startup, eliminating per-job heap allocation during gameplay

## 3. Fiber-Based Task Suspension Design

- [ ] 3.1 Document fiber stack sizing in the spec: 64 KB default stack per fiber, with a pool of 256 fibers pre-allocated at startup using `CreateFiber` (Windows) / `makecontext` (POSIX)
- [ ] 3.2 Document the suspension contract: a job calling `fiber_yield()` saves its context to the fiber pool, releases its worker thread to run other jobs, and is rescheduled when its awaited `JobHandle` completes
- [ ] 3.3 Specify fiber use cases from the proposal: animation blend tree waiting on IK solve sub-jobs, complex AI waiting on spatial query sub-jobs, and asset streaming waiting on I/O completion

## 4. enkiTS Evaluation and Selection

- [ ] 4.1 Document the enkiTS library evaluation in `docs/specs/engine/job-system.md`: confirm zlib license compatibility, review id Tech 7 usage as production precedent, evaluate API surface against UzEngine's `JobDispatcher` spec
- [ ] 4.2 Document enkiTS integration plan: use `enkiTS` as the implementation backend for `JobDispatcher` in Phase 1, wrapping its `enki::TaskScheduler` behind the `JobDispatcher` interface so the implementation can be swapped without affecting game code
- [ ] 4.3 Add `enkiTS` to `vcpkg.json` as a dependency with the exact version tag to use; document the version choice and rationale in the spec

## 5. ECS Parallelism Groups Design

- [ ] 5.1 Define the parallelism group model in `docs/specs/engine/job-system.md`: ECS systems annotate their component read/write sets; the system scheduler builds a dependency graph at startup and groups systems with non-overlapping read/write sets into parallel batches
- [ ] 5.2 Document the read/write set declaration syntax for ECS systems: `READS(TransformComponent, MeshComponent)` and `WRITES(TransformComponent)` macros that register component type IDs into a bitset at compile time
- [ ] 5.3 Document the DAG topological sort algorithm for determining execution order when systems have write-after-read dependencies; include a worked example with 6 systems showing the resulting parallel groups

## 6. Tracy Profiling Integration Design

- [ ] 6.1 Document Tracy zone integration per job in the spec: each job submission captures a `TracyZoneN(name)` with the job name string, so the Tracy timeline shows individual jobs attributed to the correct worker thread
- [ ] 6.2 Document the `UZ_JOB_PROFILE` macro design that wraps `submit()` with a Tracy zone, compiling to zero overhead when `TRACY_ENABLE` is not defined

## 7. Validation

- [ ] 7.1 Review the completed `docs/specs/engine/job-system.md` spec against all proposal requirements: `submit()`, `submit_parallel()`, `wait()`, `fiber_yield()`, thread model, Chase-Lev deque, fiber suspension, enkiTS selection, ECS parallelism groups, and Tracy integration
- [ ] 7.2 Verify the spec addresses the `while true do end` Lua loop risk: confirm that Lua scripts run on the Game Logic thread in an isolated fiber, so an infinite loop starves only that fiber and the watchdog thread (from `phase2-script-execution-safety`) can terminate it without blocking worker threads
- [ ] 7.3 Review the spec with the threading mental model: confirm no system requires the Main thread to block on a worker result mid-frame, which would cause a deadlock if the worker pool is saturated
- [ ] 7.4 Confirm the spec includes a migration guide for existing serial ECS systems: existing systems with no `READS`/`WRITES` annotations default to a single sequential group and are never reordered, preserving current behavior with zero code changes required
- [ ] 7.5 Verify the vcpkg enkiTS dependency entry compiles in the Dev Container environment using `cmake -B build --preset ci-test` before marking the task complete
