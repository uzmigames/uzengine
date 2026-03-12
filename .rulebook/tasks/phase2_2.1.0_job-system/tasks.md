## 1. Implementation

- [ ] 1.1 Implement MPMC lock-free ring buffer (`MPMCRingBuffer<T>`) with atomic CAS push/pop
- [ ] 1.2 Implement worker thread pool (N-1 threads, per-priority queues, sleep/wake via condition vars)
- [ ] 1.3 Implement work-stealing (idle worker steals from other workers' queues, high-priority first)
- [ ] 1.4 Implement `Job` struct (function pointer, closure data copy, priority, parent counter)
- [ ] 1.5 Implement `Counter` (atomic decrement, Wait with futex/condvar, IsComplete poll)
- [ ] 1.6 Implement `Fence` (AddWork + Wait wrapper over Counter)
- [ ] 1.7 Implement `ParallelFor<T>` template with automatic partitioning by worker count
- [ ] 1.8 Implement `JobSystem::Initialize()` / `Shutdown()` / `Enqueue()` / `GetWorkerCount()` / `Yield()`
- [ ] 1.9 Implement thread-local storage (per-worker scratch allocator, profiling counters)
- [ ] 1.10 Implement debug `ReadAccess<T>` / `WriteAccess<T>` wrappers (debug-only race detection)
- [ ] 1.11 Register JobSystem as ISubsystem in EngineLoop
- [ ] 1.12 Migrate shader compilation to use job system (parallel DXC compile)

## 2. Testing

- [ ] 2.1 Test MPMCRingBuffer: single-thread push/pop, multi-thread contention, full queue behavior
- [ ] 2.2 Test worker pool: correct thread count, shutdown with pending jobs, re-initialize
- [ ] 2.3 Test Counter: Wait blocks until zero, IsComplete returns correct state, multi-waiter
- [ ] 2.4 Test Fence: sequential phase execution, all jobs complete before Wait returns
- [ ] 2.5 Test ParallelFor: correct element processing, empty span, single element, large array
- [ ] 2.6 Test work-stealing: verify jobs execute even when enqueued to a busy worker
- [ ] 2.7 Test debug wrappers: ReadAccess/WriteAccess detect concurrent write+write and write+read
- [ ] 2.8 Stress test: 100K jobs enqueued, verify all complete without deadlock or crash
- [ ] 2.9 Verify 95%+ coverage on all new code

## 3. Documentation

- [ ] 3.1 Update `docs/engine/07-platform/04-job-system.md` with final API
- [ ] 3.2 Add integration guide showing Counter/Fence/ParallelFor usage patterns
