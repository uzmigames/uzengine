# Proposal: Job System (Phase 2.1)

## Why

Modern CPUs have 8-16+ cores. Single-threaded engine code wastes most of the machine. Shader compilation (already 48+ shaders) is single-threaded and slow. Asset importing, mesh culling, physics broadphase, and animation updates all need parallelism. Without a job system, every future system that needs parallel work would invent its own threading — leading to inconsistent synchronization, thread leaks, and data races.

## What Changes

### New Systems

1. **Worker Thread Pool** — `N-1` worker threads (reserve 1 for main thread) with work-stealing. Per-priority MPMC lock-free ring buffers. Workers sleep when idle, wake on enqueue.

2. **Job API** — `JobSystem::Enqueue(Job)` with function pointer + closure data + priority (Critical/High/Normal/Low/Background) + optional parent Counter.

3. **Counter** — Atomic completion counter. `Reset(N)`, `Decrement()`, `Wait()`, `IsComplete()`. Used for synchronization barriers between job phases.

4. **Fence** — Higher-level dependency primitive wrapping Counter. `AddWork(Job)` + `Wait()` for expressing sequential phases.

5. **ParallelFor** — `ParallelFor(span<T>, lambda)` template. Auto-partitions by worker count. Returns when all partitions complete.

6. **Debug Safety Wrappers** — `ReadAccess<T>` / `WriteAccess<T>` in debug builds. Detect data races at schedule time (multiple writers, or writer + reader to same data).

7. **Thread-Local Storage** — Per-worker scratch allocator, profiling counters (`executed_jobs`, `stolen_jobs`).

### Integration

- Shader compilation migrated to use job system (parallel DXC compile).
- JobSystem registered as ISubsystem in EngineLoop (Phase 2.0).

## Impact

- Affected specs: `docs/engine/07-platform/04-job-system.md`
- Affected code: `runtime/core/job/`, shader compiler
- Breaking change: NO — additive system, existing code unchanged
- User benefit: Saturate all CPU cores; shader compile 4-8x faster; foundation for parallel rendering, physics, animation
- Enables: Async resource loading (2.2), parallel culling (3.x), physics (4.2), animation (4.3)
