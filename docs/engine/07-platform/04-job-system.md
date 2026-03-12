# Job System — Overview

> **Status**: Planned (Milestone 1.5)
> **Source**: Spec in `docs/ENGINE.md` §31
> **Depends on**: Core (logging, assertions, allocators, profiling)
> **References**: UE5 `FTaskGraphInterface`, Godot `WorkerThreadPool`, EnkiTS

## Purpose

Modern CPUs have 8-16+ cores. Single-threaded game engines waste most of the machine. The Job System distributes work across all CPU cores via a **task scheduler** that manages dependencies, priorities, and work stealing.

A "job" is a small unit of work (function pointer + data). Jobs are enqueued into a scheduler, which assigns them to worker threads based on priority and availability. The system handles load balancing, synchronization, and thread pool management transparently.

```
Task Graph (DAG)
───────────────────────────────────────────────────────────────
Job A (Physics Broadphase)    Job B (Animation)    Job C (Culling)
      │                              │                    │
      ├──────────────────────────────┼────────────────────┤
      ▼                              ▼                    ▼
Job D (Physics Narrowphase, depends on A, B, C)
      │
      ▼
Job E (Scene Sync, depends on D)
```

This is fundamentally different from raw threads:
- **Threads** are expensive, OS-managed, prone to deadlocks
- **Jobs** are lightweight, scheduler-managed, dependency-aware, trivial to parallelize

## Core Concepts

### Job — Fire-and-Forget Work Unit

```cpp
/// Lightweight work descriptor.
/// A job is a function pointer + closure data, enqueued into the scheduler.
/// The scheduler owns execution timing and thread assignment.
struct Job {
    /// Function to execute. Called with pointer to closure data.
    void (*work)(void* closure_data);

    /// Closure data (arbitrary size). Copied when enqueued.
    void* data;
    uint32_t data_size;

    /// Execution priority. Determines queue ordering.
    enum Priority : uint8_t {
        Critical,   // Game-critical path (game tick, render preparation)
        High,       // Rendering, physics simulation
        Normal,     // Asset loading, non-critical updates
        Low,        // Background tasks (telemetry, analytics)
        Background, // Can be deferred to idle time
    } priority;

    /// Optional parent counter. When this job finishes, decrements parent.
    Counter* parent;
};
```

**Example: Enqueue a parallel physics update**

```cpp
struct BroadphasePayload {
    PhysicsWorld* world;
    uint32_t start_body;
    uint32_t end_body;
};

void PhysicsBroadphaseWork(void* data) {
    auto* payload = (BroadphasePayload*)data;
    payload->world->UpdateBroadphase(payload->start_body, payload->end_body);
}

// In physics system:
Counter phase_counter;
const uint32_t num_workers = JobSystem::GetWorkerCount();
const uint32_t bodies_per_job = total_bodies / num_workers;

for (uint32_t i = 0; i < num_workers; ++i) {
    BroadphasePayload payload{
        .world = &world,
        .start_body = i * bodies_per_job,
        .end_body = (i + 1) * bodies_per_job,
    };

    JobSystem::Enqueue({
        .work = &PhysicsBroadphaseWork,
        .data = &payload,
        .data_size = sizeof(payload),
        .priority = Job::Priority::High,
        .parent = &phase_counter,
    });
}

// Wait for all jobs to finish
phase_counter.Wait();  // Blocks until count reaches 0
```

### Counter — Synchronization Point

```cpp
/// Atomic counter for job completion synchronization.
/// Initialized to N (number of jobs), each job decrements on completion.
/// Calling Wait() blocks until count reaches 0.
class Counter {
public:
    /// Initialize to count. Usually == number of jobs in a phase.
    void Reset(uint32_t count);

    /// Decrement counter. If reaches 0, wake all waiters.
    void Decrement();

    /// Block until counter reaches 0. Can be called by multiple threads.
    void Wait();

    /// Non-blocking poll. Returns true if count is 0.
    bool IsComplete() const;
};
```

### Fence — Dependency Tracking

Jobs can depend on prior jobs' completion via a **fence**:

```cpp
/// Lightweight synchronization primitive.
/// Used to express job dependencies (graphs, chains).
class Fence {
public:
    /// Add a job to wait for completion of prior work.
    /// job.parent must be set to this fence's counter.
    void AddWork(Job job);

    /// Block until all enqueued jobs complete.
    void Wait();
};
```

**Example: Sequential phases with dependencies**

```cpp
Fence animation_fence;
Fence physics_fence;
Fence culling_fence;

// Phase 1: Animate all skeletons
for (Skeleton* skel : skeletons) {
    animation_fence.AddWork({
        .work = [](void* data) {
            auto* skel = (Skeleton*)data;
            skel->UpdateBones();
        },
        .data = skel,
        .priority = Job::Priority::Normal,
    });
}
animation_fence.Wait();  // Wait for all animations

// Phase 2: Update physics (depends on animation)
for (RigidBody& body : bodies) {
    physics_fence.AddWork({
        .work = &PhysicsWork,
        .data = &body,
        .priority = Job::Priority::High,
    });
}
physics_fence.Wait();

// Phase 3: Cull visible objects (depends on physics)
culling_fence.AddWork({
    .work = &VisibilityCull,
    .priority = Job::Priority::High,
});
culling_fence.Wait();
```

### Parallel-For — Data Parallel Loops

```cpp
/// Distribute loop iterations across all worker threads.
/// Partition automatically based on worker count.
template <typename T, typename Fn>
void ParallelFor(const std::span<T>& items, Fn lambda) {
    // Internally partitions: items.size() / worker_count items per job
    // Each job calls lambda(item) for assigned partition
}

// Example: Parallel update 10k transform matrices
std::vector<Transform> transforms;  // ... 10k items
JobSystem::ParallelFor(transforms, [](Transform& t) {
    t.Decompose();  // Cache-friendly per-transform work
});
```

## Architecture

### Worker Thread Pool

```
Main Thread (Game Tick, Render Prep)
│
├─► Worker Thread 0  ─┐
├─► Worker Thread 1  ─┤
├─► Worker Thread 2  ─├─► Job Queues (per priority, work-stealing)
├─► Worker Thread 3  ─┤
└─► Worker Thread N-1 ─┘
```

- **Worker Count**: `std::thread::hardware_concurrency() - 1` (reserve 1 for main thread)
- **Per-Thread Queues**: Each priority level has an MPMC (multi-producer, multi-consumer) lock-free ring buffer
- **Work Stealing**: When a worker's local queue is empty, it steals jobs from other threads' queues (high-priority first)
- **Thread Affinity**: Optional CPU affinity pinning per worker (improves cache locality)
- **Wake-Sleep**: Workers sleep when all queues are empty, woken by `Enqueue()` via condition variables

### Lock-Free MPMC Ring Buffer

```cpp
/// Multi-producer, multi-consumer lock-free queue.
/// Fixed capacity, never allocates after init.
template <typename T>
class MPMCRingBuffer {
private:
    struct Slot {
        std::atomic<uint64_t> sequence;  // Unique slot index
        T                     item;
    };

    std::vector<Slot>        slots;
    std::atomic<uint64_t>    head, tail;  // Monotonically increasing
    uint32_t                 mask;  // capacity - 1 (power of 2)

public:
    /// Try to enqueue without blocking. Returns true if successful.
    bool TryPush(const T& item);

    /// Try to dequeue without blocking. Returns true if successful.
    bool TryPop(T& item);
};
```

**Why MPMC instead of channels?** Multiple threads enqueue jobs simultaneously. A global lock would become the bottleneck. MPMC ring buffers use atomic compare-and-swap (CAS) for lock-free synchronization — only contending threads slow down, idle cores proceed unimpeded.

### Thread-Local Storage

Each worker thread maintains thread-local data to avoid lock contention:

```cpp
thread_local struct {
    Allocator allocator;           // Per-thread scratch allocator
    std::vector<Counter*> fences;  // Local fence registry (for Wait optimization)
    uint64_t executed_jobs = 0;    // Profiling counter
    uint64_t stolen_jobs = 0;
} worker_local;
```

This is **critical for performance**: if every `Allocator::Alloc()` on different threads locked a global mutex, the job system would be CPU-bound by synchronization instead of compute.

## Integration Points

### Physics Simulation

Jolt Physics is multi-threaded internally, but we also parallelize broadphase collision detection:

```cpp
// In PhysicsSystem::Tick()
Fence broadphase_fence;

// Partition bodies across workers
for (uint32_t i = 0; i < job_system.GetWorkerCount(); ++i) {
    broadphase_fence.AddWork({
        .work = &BroadphaseWork,
        .data = &partition[i],
        .priority = Job::Priority::High,
    });
}
broadphase_fence.Wait();

// Narrowphase (single-threaded, depends on broadphase)
NarrowphaseWork();
```

### Rendering — Mesh Culling

Visibility culling is embarrassingly parallel: each job frustum-tests a partition of the scene.

```cpp
// In VisibilitySystem::Cull()
Counter cull_counter(num_partitions);

for (uint32_t i = 0; i < num_partitions; ++i) {
    JobSystem::Enqueue({
        .work = [](void* data) {
            auto* part = (CullPartition*)data;
            FrustumCull(part->proxies, part->view.frustum);
        },
        .data = &culling_partitions[i],
        .priority = Job::Priority::High,
        .parent = &cull_counter,
    });
}

cull_counter.Wait();
```

### Animation — Bone Transform Updates

Skeletal animation is per-skeleton parallel:

```cpp
// In AnimationSystem::Update()
Counter anim_counter(num_skeletons);

for (Skeleton* skel : skeletons) {
    JobSystem::Enqueue({
        .work = [](void* data) {
            auto* skel = (Skeleton*)data;
            skel->UpdateBonePalette();  // SIMD-heavy, scales well
        },
        .data = skel,
        .priority = Job::Priority::Normal,
        .parent = &anim_counter,
    });
}

anim_counter.Wait();
```

### Asset Loading

Background I/O is delegated to the Background priority tier:

```cpp
// User requests mesh load (game thread)
auto handle = Assets::LoadAsync<Mesh>("models/character.uzmesh");

// Internally: enqueue low-priority job
JobSystem::Enqueue({
    .work = &AssetLoaderWork,
    .data = &load_request,
    .priority = Job::Priority::Background,
});

// Game continues; when loaded, asset is ready
if (Assets::IsReady(handle)) { /* use mesh */ }
```

## Job System API

```cpp
namespace JobSystem {
    /// Initialize worker pool. Call once at engine startup.
    void Initialize();

    /// Shut down worker threads. Call once at engine shutdown.
    void Shutdown();

    /// Enqueue a job into the scheduler.
    void Enqueue(const Job& job);

    /// Get number of worker threads (hardware_concurrency - 1).
    uint32_t GetWorkerCount();

    /// Yield control back to the job scheduler (for long-running jobs).
    void Yield();
}
```

## Performance Characteristics

| Operation | Time | Notes |
|-----------|------|-------|
| Enqueue job | ~100 ns | Lock-free push to ring buffer |
| Dequeue job | ~50 ns | Work stealing, same-thread priority |
| Counter increment | ~10 ns | Atomic operation |
| Counter wait (already done) | ~5 ns | Atomic load (no blocking) |
| Context switch (thread sleep→wake) | ~1-10 μs | OS-dependent, rare |

**Bandwidth**: On a 16-core CPU, lock-free ring buffers can sustain 50M enqueues/second without contention.

## Engine Comparison

| Feature | Unreal 5 | Godot | **UzEngine** |
|---------|----------|-------|--------------|
| Task model | `FTask` / `FAsyncTask` | `WorkerThreadPool` | **`Job` + `Counter`** |
| Dependencies | Task graph, linear chains | Task ID → thread local | **Explicit `Fence` + parent counter** |
| Work stealing | Implicit (queue hierarchy) | Per-thread deque | **MPMC ring buffer with stealing** |
| Priorities | 5 levels | 3 levels (HIGH/NORMAL/LOW) | **5 levels (Critical→Background)** |
| Lock-free | Custom intrusive queues | Mutex-based | **MPMC ring buffer + atomic counters** |
| Parallel-for | `ParallelFor` template | N/A | **Template + automatic partitioning** |

## Rules

1. **NEVER use raw `std::thread`** — always use `JobSystem::Enqueue()` for parallelism
2. **NEVER busy-wait in jobs** — use `Counter::Wait()` only from main thread or at phase boundaries
3. **ALWAYS initialize `Counter` before enqueueing parent jobs** — undefined behavior otherwise
4. **ALWAYS profile parallel sections** — `UZ_PROFILE_SCOPE` around job submission and wait points
5. **NEVER enqueue jobs from job callbacks** — register all jobs before the first `Wait()`; jobs should not spawn new jobs (prevents runaway task explosion)
6. **ALWAYS use thread-local allocators** — never share heap allocators between threads without locking
