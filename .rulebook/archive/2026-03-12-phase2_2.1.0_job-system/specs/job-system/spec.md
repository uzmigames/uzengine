# Job System — Spec Delta

## ADDED Requirements

### Requirement: Worker Thread Pool
The JobSystem SHALL create `hardware_concurrency - 1` worker threads on Initialize(). Each worker MUST have per-priority MPMC lock-free queues. Workers SHALL sleep when all queues are empty and wake via condition variable on Enqueue().

#### Scenario: Correct worker count
Given a machine with 16 logical cores
When JobSystem::Initialize() is called
Then 15 worker threads are created
And JobSystem::GetWorkerCount() returns 15

#### Scenario: Workers sleep when idle
Given the job system is initialized with no pending work
When all queues are empty
Then worker threads SHALL be in sleep state (not busy-waiting)

### Requirement: Lock-Free MPMC Ring Buffer
The scheduler SHALL use lock-free MPMC ring buffers (atomic CAS) for job queues. The ring buffer MUST have fixed capacity set at initialization and SHALL never allocate after init.

#### Scenario: Concurrent push from multiple producers
Given 8 threads each push 1000 jobs simultaneously
When all pushes complete
Then exactly 8000 jobs are in the queue
And no data corruption occurs

#### Scenario: Full queue rejects push
Given the ring buffer is at capacity
When a new job is pushed
Then TryPush() SHALL return false without blocking

### Requirement: Job Enqueue and Execution
The JobSystem SHALL accept jobs via `Enqueue(Job)`. Each Job MUST contain a function pointer, closure data (copied on enqueue), priority level, and optional parent Counter. The closure data MUST be copied into the queue — the caller's stack data SHALL not be referenced after Enqueue returns.

#### Scenario: Job executes with correct data
Given a job is enqueued with closure data containing value 42
When the job executes on a worker thread
Then the work function receives a pointer to copied data containing value 42

#### Scenario: Priority ordering
Given a High-priority job and a Low-priority job are enqueued
When a worker dequeues
Then the High-priority job SHALL be dequeued first

### Requirement: Work Stealing
When a worker's local queues are empty, it SHALL steal jobs from other workers' queues. Stealing MUST prefer high-priority queues first.

#### Scenario: Idle worker steals work
Given worker 0 has 100 pending jobs and worker 1 has 0
When worker 1 finishes its queue
Then worker 1 steals jobs from worker 0's queue
And both workers are utilized

### Requirement: Counter Synchronization
Counter SHALL provide `Reset(N)`, `Decrement()`, `Wait()`, and `IsComplete()`. Wait() MUST block the calling thread until the count reaches zero. Multiple threads SHALL be able to Wait() on the same Counter.

#### Scenario: Wait blocks until all jobs complete
Given a Counter is Reset(10) and 10 jobs each call Decrement()
When the main thread calls Wait()
Then it blocks until all 10 jobs have called Decrement()
And then returns immediately

#### Scenario: IsComplete is non-blocking
Given a Counter with remaining count of 5
When IsComplete() is called
Then it returns false without blocking

### Requirement: ParallelFor
The JobSystem SHALL provide `ParallelFor(span<T>, lambda)` that distributes loop iterations across all workers. Partitioning MUST be automatic based on worker count. ParallelFor SHALL block until all partitions complete.

#### Scenario: Array processed in parallel
Given an array of 10000 elements and 15 workers
When ParallelFor is called with a transform lambda
Then all 10000 elements are transformed
And execution uses multiple threads

#### Scenario: Empty span is a no-op
Given an empty span
When ParallelFor is called
Then it returns immediately without enqueuing any jobs

### Requirement: Debug Data Race Detection
In debug builds, the JobSystem SHALL provide `ReadAccess<T>` and `WriteAccess<T>` wrappers. Concurrent WriteAccess + WriteAccess or WriteAccess + ReadAccess to the same data SHALL trigger an assertion at schedule time.

#### Scenario: Concurrent writes detected
Given job A and job B both request WriteAccess to the same buffer
When both are scheduled concurrently
Then a debug assertion fires indicating a data race

### Requirement: Thread-Local Storage
Each worker thread MUST maintain thread-local data: a per-thread scratch allocator and profiling counters (executed_jobs, stolen_jobs). Thread-local allocators SHALL never contend with other threads.

#### Scenario: Per-thread allocator is independent
Given two workers allocate memory simultaneously
When both use their thread-local allocator
Then no mutex contention occurs between them

### Requirement: Subsystem Integration
JobSystem SHALL implement `ISubsystem` and register with SubsystemRegistry. It MUST initialize after core systems (logging, config) and shutdown before them (reverse order). Shutdown SHALL wait for all pending jobs to complete or cancel them.

#### Scenario: Shutdown with pending jobs
Given 50 jobs are still in queues
When JobSystem::Shutdown() is called
Then all pending jobs either complete or are cancelled
And all worker threads join cleanly
