# Job System — Multi-Threaded Work Distribution

> **Status**: Active (Milestone 2.1.0)
> **Source**: `runtime/engine/job/`
> **Depends on**: Core (logging, assertions)
> **References**: UE5 `FTaskGraphInterface`, Godot `WorkerThreadPool`, EnkiTS

## Purpose

Modern CPUs have 8-16+ cores. Single-threaded game engines waste most of the machine. The Job System distributes work across all CPU cores via a **work-stealing thread pool** that manages priorities, automatic load balancing, and synchronization.

A "job" is a lightweight unit of work: a function pointer plus up to 48 bytes of inline closure data (typically captured parameters). Jobs are enqueued into a scheduler, which assigns them to worker threads based on priority and availability. The system handles load balancing, synchronization, and thread pool management transparently — no raw threads, no manual thread creation.

```
Task Distribution Architecture
───────────────────────────────────────────────────────────────
Main Thread (Game Tick, Render Prep)
│
├─► Worker Thread 0  ──┐
├─► Worker Thread 1  ──┤
├─► Worker Thread 2  ──├─► Per-Priority Lock-Free Queues
├─► Worker Thread 3  ──┤    + Work Stealing
└─► Worker Thread N-1 ──┘
```

This is fundamentally different from raw threads:
- **Raw threads**: OS-managed, expensive to spawn, prone to deadlocks, hard to load-balance
- **Jobs**: lightweight, scheduler-managed, priority-aware, automatic work stealing, trivial to parallelize

## Core Concepts

### Job — Self-Contained Work Unit

A `Job` is the fundamental unit of work in the job system:

```cpp
struct Job {
    /// Work function signature. Receives a pointer to the inline `data` buffer.
    using WorkFn = void (*)(void* data);

    WorkFn function = nullptr;             ///< Work function (null = no-op job)
    alignas(16) uint8_t data[48] = {};     ///< Inline closure data (copied on enqueue)
    Counter* counter = nullptr;            ///< Optional completion counter (caller-owned)
    JobPriority priority = JobPriority::Normal; ///< Scheduling priority
};
```

**Key Design Features:**

1. **Zero-Allocation Enqueue**: The 48-byte inline data buffer holds most lambda captures without heap allocation. Only if you need more data should you heap-allocate and capture a pointer.

2. **Value Semantics**: Jobs are copied into lock-free queues. No ownership transfer, no virtual dispatch.

3. **Optional Counter**: If `counter` is non-null, it is decremented after job execution (atomically). This implements synchronization barriers.

4. **Trivially Copyable**: All state (function pointer, inline data, counter pointer, priority) is trivially copyable for efficient queue operations.

**Creating Jobs with `MakeJob`:**

The `MakeJob` helper template simplifies job creation from lambdas:

```cpp
// Simple lambda with no captures
auto job1 = uz::MakeJob([]() { ProcessData(); });

// Lambda capturing by value (fits in 48 bytes)
int result = 0;
auto job2 = uz::MakeJob([&result]() { result = 42; });

// With priority and counter
uz::Counter counter;
counter.Reset(1);
auto job3 = uz::MakeJob(
    []() { HeavyComputation(); },
    uz::JobPriority::High,
    &counter
);
jobSystem->Enqueue(job3);
counter.Wait();  // Blocks until job completes
```

**Example: Parallel physics broadphase**

```cpp
// Partition bodies across workers
struct BroadphaseData {
    PhysicsWorld* world;
    uint32_t start_body;
    uint32_t end_body;
};

BroadphaseData partitions[MAX_WORKERS];
uz::Counter phase_counter;
phase_counter.Reset(num_workers);

for (uint32_t i = 0; i < num_workers; ++i) {
    partitions[i] = {
        .world = &world,
        .start_body = i * bodies_per_partition,
        .end_body = (i + 1) * bodies_per_partition,
    };

    auto job = uz::MakeJob(
        [partition = partitions[i]]() {
            partition.world->UpdateBroadphase(
                partition.start_body,
                partition.end_body
            );
        },
        uz::JobPriority::High,
        &phase_counter
    );

    jobSystem->Enqueue(job);
}

phase_counter.Wait();  // Blocks until all broadphase jobs complete
```

### Counter — Completion Synchronization

A `Counter` is an atomic synchronization primitive for waiting on job completion. Each counter is initialized with a count (number of outstanding jobs), and each completing job atomically decrements it. When the count reaches zero, all threads blocked in `Wait()` are woken.

```cpp
class Counter {
public:
    /// Initialize the counter with the number of outstanding work items.
    /// Call before enqueueing any jobs with this counter.
    void Reset(uint32_t count);

    /// Decrement the counter by one. Called by workers upon job completion.
    /// If counter reaches zero, all threads blocked in Wait() are notified.
    void Decrement();

    /// Block the calling thread until the counter reaches zero.
    /// Returns immediately if already zero.
    void Wait();

    /// Non-blocking check for completion.
    bool IsComplete() const;
};
```

**Usage Pattern:**

```cpp
uz::Counter counter;
counter.Reset(3);  // Expecting 3 jobs

// Enqueue 3 jobs with &counter as their completion counter
js->Enqueue(uz::MakeJob([] { Task1(); }, uz::JobPriority::High, &counter));
js->Enqueue(uz::MakeJob([] { Task2(); }, uz::JobPriority::High, &counter));
js->Enqueue(uz::MakeJob([] { Task3(); }, uz::JobPriority::High, &counter));

// Block until all 3 jobs complete
counter.Wait();

// Can reuse the counter for another batch
counter.Reset(2);
js->Enqueue(uz::MakeJob([] { Task4(); }, uz::JobPriority::Normal, &counter));
js->Enqueue(uz::MakeJob([] { Task5(); }, uz::JobPriority::Normal, &counter));
counter.Wait();
```

**Thread Safety:**
- `Reset()` must not be called while another thread is in `Wait()` or `Decrement()`.
- `Decrement()` and `Wait()` are fully thread-safe and can be called concurrently from any number of threads.
- Typical usage: `Reset()` is called once from main thread, then multiple workers call `Decrement()`, and main thread calls `Wait()`.

**Implementation Details:**
- Uses `std::atomic<uint32_t>` for the counter value.
- Uses `std::mutex` + `std::condition_variable` for blocking (portable, non-busy-wait).
- Acquire/release memory ordering ensures visibility across threads.

### Fence — Batch Job Synchronization

A `Fence` is a higher-level synchronization primitive that groups multiple jobs under a single wait point. It buffers jobs and flushes them all at once when `Wait()` is called, avoiding races where a counter might reach zero before all jobs are submitted.

```cpp
class Fence {
public:
    /// Construct a fence bound to a job system.
    explicit Fence(JobSystem* system);

    /// Add a job to be tracked by this fence.
    /// The job's counter field is ignored (overwritten when flushed).
    void AddWork(Job job);

    /// Flush all buffered jobs to the job system and block until completion.
    /// Sets internal counter to the number of buffered jobs, assigns the counter
    /// to each job, enqueues them all, then waits for completion.
    void Wait();

    /// Non-blocking check for completion of the last Wait() batch.
    bool IsComplete() const;
};
```

**Usage Pattern:**

```cpp
auto* js = uz::JobSystem::Get();

// Phase 1: Parallel animation updates
uz::Fence anim_fence(js);
for (Skeleton* skel : skeletons) {
    anim_fence.AddWork(uz::MakeJob([skel]() {
        skel->UpdateBonePalette();
    }, uz::JobPriority::Normal));
}
anim_fence.Wait();  // Wait for all animations

// Phase 2: Parallel physics updates (depends on animation completion)
uz::Fence physics_fence(js);
for (RigidBody& body : bodies) {
    physics_fence.AddWork(uz::MakeJob([&body]() {
        body.UpdateTransform();
        body.UpdateVelocity();
    }, uz::JobPriority::High));
}
physics_fence.Wait();

// Phase 3: Visibility culling (depends on physics completion)
uz::Fence cull_fence(js);
cull_fence.AddWork(uz::MakeJob([]() {
    VisibilityCull(scene, camera.frustum);
}, uz::JobPriority::High));
cull_fence.Wait();
```

**Key Advantages over Raw Counter:**

- **Buffers jobs internally** — ensures counter is set before ANY worker can decrement it (eliminates race condition)
- **Cleaner API** — `AddWork()` + `Wait()` vs. manual counter management
- **Reusable** — call `AddWork()` + `Wait()` multiple times on the same fence
- **Optional thread safety** — `AddWork()` is NOT thread-safe, so call only from submitting thread. `Wait()` and `IsComplete()` are fully thread-safe.

### ParallelFor — Automatic Data Partitioning

`ParallelFor` distributes array iteration across all workers with automatic partitioning:

```cpp
template <typename T, typename Fn>
void ParallelFor(T* data, size_t count, Fn&& fn);
```

The function:
1. Divides the array into `(worker_count + 1)` chunks (one per worker + main thread)
2. Enqueues worker chunks as High-priority jobs
3. Main thread processes chunk 0
4. Blocks until all chunks complete

**Example:**

```cpp
std::vector<float> values(10000);

auto* js = uz::JobSystem::Get();
js->ParallelFor(values.data(), values.size(), [](float& v) {
    v = std::sqrt(v);  // Each worker processes its partition
});

// All iterations complete here; main thread participated in work
```

**Benefits:**

- **Automatic partitioning** — no manual chunk calculation
- **Main thread participates** — doesn't waste cycles spinning; processes chunk 0 while workers process others
- **High priority** — worker chunks are High-priority to avoid blocking on lower-priority queues
- **Blocking** — returns only when all chunks complete

**Performance Notes:**

- Best for embarrassingly parallel loops (per-item work is independent)
- Chunk size = `ceil(count / (worker_count + 1))`
- If chunk size is very small (< 100 items), overhead may dominate — consider manual partitioning
- Main thread overhead: chunk enumeration, job creation, one decrement

## Architecture

### Worker Thread Pool Design

The job system owns `N = std::thread::hardware_concurrency() - 1` worker threads (minimum 1, reserving 1 thread for the main game thread).

```
Main Thread                 Worker Threads (N-1 threads)
(Game Tick, Render)         ─────────────────────────────
                            Thread 0: Priority Queues
Enqueue(job)  ─────────────► Thread 1: Priority Queues
              │             Thread 2: Priority Queues
              │             Thread N-1: Priority Queues
              │             │
              │             └─► Work Stealing Loop
              │                 (when local queue empty)
              │
              └─► Round-robin distribution
```

**Key Design Decisions:**

1. **Per-Worker, Per-Priority Queues**: Each worker owns 4 `AtomicQueue2` instances (one per `JobPriority` level: Critical, High, Normal, Low). This avoids a global bottleneck and enables efficient work-stealing.

2. **Round-Robin Enqueue**: Jobs are distributed across workers via atomic counter round-robin (`nextWorker_` field), ensuring even load distribution.

3. **Priority Draining**: Workers drain their own queues in priority order (Critical first, then High, Normal, Low). If a queue is empty, check the next priority level.

4. **Work Stealing**: When a worker's own queues are empty, it steals from a pseudo-random peer's queues (respecting priority order). Pseudo-random starting index avoids thundering herd.

5. **Wake/Sleep**: Workers use condition variables (`wakeCV_`) for sleep/wake coordination. When a queue is full on enqueue, jobs run inline instead of blocking. Workers wake when new jobs are available or system is shutting down.

6. **Lock-Free Queues**: Uses `atomic_queue::AtomicQueue2` for MPMC synchronization without global locks.

### Lock-Free Queue: AtomicQueue2

The job system uses `atomic_queue::AtomicQueue2<Job, 4096>` for each per-worker per-priority queue:

```
AtomicQueue2 (Fixed Capacity)
─────────────────────────────
Capacity: 4096 jobs per queue
Layout: Lock-free circular buffer with atomic head/tail pointers
Enqueue: Atomic CAS to claim a slot (O(1) lock-free)
Dequeue: Atomic CAS to claim a slot (O(1) lock-free)
Full: If enqueue fails and queue is full, job runs inline on calling thread
Empty: If dequeue finds empty slot, worker steals or sleeps
```

**Rationale**: Lock-free queues avoid mutex contention, which becomes severe under high job submission rates (>1M jobs/sec). AtomicQueue2 is battle-tested in production rendering engines.

### Worker Thread Lifecycle

**Init Phase:**
```
JobSystem::Init()
  ├─► For each worker i in [0, N):
  │    └─► Create WorkerData
  │        ├─► Allocate 4 AtomicQueue2 instances
  │        └─► Spawn std::thread(WorkerMain, i)
  └─► Set global instance_ pointer
      Running = true
```

**Worker Main Loop:**
```cpp
void WorkerMain(uint32_t workerIndex) {
    while (running_) {
        if (TryExecuteOne(workerIndex)) {
            // Executed a job from own queues (priority order)
            continue;
        }
        if (TrySteal(workerIndex)) {
            // Stole a job from another worker
            continue;
        }
        // All queues empty — sleep until woken
        std::unique_lock<std::mutex> lock(wakeMutex_);
        wakeCV_.wait(lock, [this] { return !running_; });
    }
}
```

- `TryExecuteOne()`: Tries to pop from own queues in priority order (Critical → Low)
- `TrySteal()`: Tries to steal from other workers, starting at pseudo-random index, respecting priority
- Sleep on empty: Waits on condition variable (no busy-spin)
- Exit: `running_` flag signals shutdown

**Shutdown Phase:**
```
JobSystem::Shutdown()
  ├─► Set running_ = false
  ├─► Wake all sleeping workers via wakeCV_
  ├─► For each worker: std::thread::join()
  ├─► Destroy queue instances
  └─► Clear global instance_ pointer
```

### Statistics Tracking

Each worker tracks execution stats:
```cpp
struct WorkerData {
    std::atomic<uint64_t> executedJobs{0};  ///< Jobs executed from own queue
    std::atomic<uint64_t> stolenJobs{0};    ///< Jobs stolen from other workers
};
```

These are useful for profiling load balance and work-stealing effectiveness.

## Debug Race Detection: ReadAccess & WriteAccess

For debug builds, the job system provides `ReadAccess<T>` and `WriteAccess<T>` wrappers that detect concurrent access violations at runtime:

```cpp
struct AccessTracker {
    std::atomic<int32_t> state{0};
};

// Debug build: tracks access state
template<typename T>
struct ReadAccess {
    ReadAccess(T* ptr, AccessTracker* tracker);
    ~ReadAccess();
    const T& operator*() const;
};

template<typename T>
struct WriteAccess {
    WriteAccess(T* ptr, AccessTracker* tracker);
    ~WriteAccess();
    T& operator*();
};
```

**State Machine:**
- `0` — free (no active access)
- `> 0` — N concurrent readers active
- `-1` — one writer active (exclusive)

**Rules:**
- Multiple `ReadAccess` can be active concurrently (shared)
- Only one `WriteAccess` can be active (exclusive)
- `WriteAccess` + `ReadAccess` conflict — triggers `UZ_CHECK` failure

**Example:**

```cpp
uz::AccessTracker tracker;
int shared_value = 0;

// In Worker A (read):
{
    auto read = uz::ReadAccess<int>(&shared_value, &tracker);
    printf("Value: %d\n", *read);
}  // Read access released

// In Worker B (write):
{
    auto write = uz::WriteAccess<int>(&shared_value, &tracker);
    *write = 42;
}  // Write access released
```

**Shipping Builds:**
In `UZ_SHIPPING` builds, `ReadAccess` and `WriteAccess` compile to zero-cost pass-throughs:
- No atomic operations
- No tracker pointer needed
- Inline, no overhead

**Usage in Job System:**
Not currently enforced by the job system itself; intended for user code to protect shared data structures accessed by jobs.

## Integration Examples

### Physics Simulation — Parallel Broadphase

```cpp
class PhysicsSystem : public ISubsystem {
    void Tick() override {
        auto* js = uz::JobSystem::Get();

        // Partition bodies across workers
        struct PartitionData {
            PhysicsWorld* world;
            uint32_t start;
            uint32_t end;
        };

        std::vector<PartitionData> partitions;
        uz::Fence fence(js);

        for (uint32_t i = 0; i < js->GetWorkerCount(); ++i) {
            uint32_t start = i * bodies_per_partition;
            uint32_t end = (i + 1) * bodies_per_partition;
            if (start < world_.bodies.size()) {
                fence.AddWork(uz::MakeJob([&world = world_, start, end]() {
                    world.UpdateBroadphase(start, end);
                }, uz::JobPriority::High));
            }
        }
        fence.Wait();  // All broadphase partitions complete

        // Single-threaded narrowphase (depends on broadphase)
        world_.UpdateNarrowphase();
    }
};
```

### Rendering — Parallel Visibility Culling

```cpp
class VisibilitySystem : public ISubsystem {
    void Cull(const Camera& camera) {
        auto* js = uz::JobSystem::Get();

        // Partition scene into spatial cells
        const int GRID_DIM = 8;
        const int CELL_COUNT = GRID_DIM * GRID_DIM;

        uz::Counter counter;
        counter.Reset(CELL_COUNT);

        for (int i = 0; i < CELL_COUNT; ++i) {
            js->Enqueue(uz::MakeJob([this, i, &camera]() {
                FrustumCull(cells_[i], camera.frustum);
            }, uz::JobPriority::High, &counter));
        }

        counter.Wait();  // All culling complete
    }
};
```

### Animation — Skeleton Updates

```cpp
class AnimationSystem : public ISubsystem {
    void Update(float dt) {
        auto* js = uz::JobSystem::Get();
        uz::Fence fence(js);

        for (Skeleton* skel : skeletons_) {
            fence.AddWork(uz::MakeJob([skel, dt]() {
                skel->EvaluateAnimations(dt);
                skel->UpdateBonePalette();  // SIMD-heavy
            }, uz::JobPriority::Normal));
        }
        fence.Wait();
    }
};
```

### Data Processing — ParallelFor

```cpp
class TextureCompressor {
    void CompressAll(std::vector<Texture>& textures) {
        auto* js = uz::JobSystem::Get();

        // Compress each texture in parallel
        js->ParallelFor(textures.data(), textures.size(), [](Texture& tex) {
            tex.Compress(CompressionFormat::BC7);
        });

        // All textures compressed here
    }
};
```

## JobSystem API Reference

The `JobSystem` class implements the `ISubsystem` interface and provides the public job scheduling API:

### Lifecycle (ISubsystem)

```cpp
/// Initialize the worker thread pool.
/// Called by EngineLoop during PreInit phase (before GPU, before physics).
bool Init() override;

/// Shut down worker threads.
/// Called by EngineLoop during engine shutdown.
void Shutdown() override;

/// Returns "JobSystem"
const char* GetName() const override { return "JobSystem"; }

/// Returns InitPhase::PreInit (must run before most other systems)
InitPhase GetInitPhase() const override { return InitPhase::PreInit; }

/// Returns TickGroup::None (workers run independently, not ticked)
TickGroup GetTickGroup() const override { return TickGroup::None; }

/// Returns {"Logging"} (depends on logging system for UZ_LOG)
std::vector<const char*> GetDependencies() const override;
```

### Job Submission

```cpp
/// Enqueue a job for execution by a worker thread.
///
/// The job is placed into a worker's queue for its priority level.
/// Worker is selected via round-robin for even distribution.
/// If the queue is full, the job is executed inline on the calling thread.
///
/// Thread-safe. Can be called from any thread, including other workers.
void Enqueue(Job job);

/// Attempts to execute one pending job on the calling thread.
///
/// Useful for the main thread to help drain work while waiting on a Counter.
/// Tries all workers' queues in priority order (respects stealing rules).
///
/// Returns true if a job was executed, false if all queues are empty.
bool Yield();

/// Distributes work across all workers using automatic partitioning.
///
/// Splits the range [0, count) into chunks, enqueues worker chunks as
/// High-priority jobs, and processes chunk 0 on the calling thread.
/// Blocks until all chunks complete.
///
/// Thread-safe (blocking). Can be called from main thread only in typical usage.
template <typename T, typename Fn>
void ParallelFor(T* data, size_t count, Fn&& fn);
```

### Worker Information

```cpp
/// Returns the number of worker threads (not including main thread).
///
/// Typically = std::thread::hardware_concurrency() - 1, minimum 1.
uint32_t GetWorkerCount() const;

/// Returns the global JobSystem instance.
///
/// Returns nullptr if not initialized. Typically called like:
/// auto* js = uz::JobSystem::Get();
static JobSystem* Get();
```

### Constants

```cpp
static constexpr uint32_t kQueueCapacity = 4096;  ///< Per-queue capacity
```

### Usage

Typical integration into engine loop:

```cpp
class EngineLoop {
    void Initialize() {
        // JobSystem is added to subsystem registry
        // It initializes during PreInit phase
        subsystems_.Register(new uz::JobSystem());
        subsystems_.InitAll();  // JobSystem::Init() called here
    }

    void MainLoop() {
        while (running_) {
            // Submit work
            auto* js = uz::JobSystem::Get();
            uz::Fence fence(js);

            // Physics parallel work
            PhysicsSystem::SubmitWork(fence);
            // Animation parallel work
            AnimationSystem::SubmitWork(fence);
            fence.Wait();

            // Rendering
            renderer_.Render();
        }
    }

    void Shutdown() {
        subsystems_.ShutdownAll();  // JobSystem::Shutdown() called here
    }
};
```

## Performance Characteristics

### Microbenchmarks

| Operation | Latency | Notes |
|-----------|---------|-------|
| `Enqueue(job)` | ~20-100 ns | Lock-free atomic operations |
| `Dequeue(job)` from own queue | ~10-50 ns | Priority queue search |
| Work steal from peer | ~30-80 ns | Random peer access + priority search |
| `Counter::Decrement()` | ~5-10 ns | Atomic fetch_sub + condvar notification |
| `Counter::Wait()` (already zero) | ~5 ns | Atomic load check |
| `Counter::Wait()` (blocks) | ~1-10 μs | OS context switch + wake |
| `Job` execution (no-op) | ~30-50 ns | Dispatch overhead |

### Throughput

On a 16-core CPU with contention:
- **Enqueue throughput**: ~50M jobs/second (atomic_queue::AtomicQueue2 rated limit)
- **Per-worker throughput**: ~200k jobs/second (depends on job complexity)
- **Steal throughput**: Minimal overhead when workers are balanced

### Bottlenecks to Avoid

1. **Queue Saturation**: If all queues fill (4096 jobs × 4 priorities × N workers), subsequent enqueues execute inline. Mitigate by batching finer-grained work or using Fence.

2. **Contention on Counter**: Multiple workers decrementing the same counter under high rate (~100M/sec) may cause contention. Use multiple counters (one per phase) instead of one global counter.

3. **Excessive Work Stealing**: If load is severely imbalanced, stealing overhead adds up. Prefer partitioning work upfront (ParallelFor, Fence).

4. **Main Thread Starvation**: Main thread blocked in `Counter::Wait()` while workers are slow. Mitigate by using `Yield()` to help drain work.

## Queue Capacity and Configuration

```cpp
static constexpr uint32_t kQueueCapacity = 4096;  // Per-queue capacity

// Queue capacity formula:
// Total capacity = 4 priorities × N workers × 4096 jobs
//                = 4 × 16 × 4096 = ~262k jobs (16-core CPU)

// If you enqueue faster than workers can drain:
// - Jobs pile up in queues
// - After queues fill, new enqueues run inline (high latency)
// - Mitigate: use Fence to batch/flush, or reduce job submission rate
```

For typical 60 FPS engines:
- `60 FPS × 16k jobs/frame ÷ 4 priorities ÷ 16 workers = 9 job/queue/frame` — **no contention**
- Peak rates (e.g., initial load): May exceed capacity; inline execution is acceptable

## Comparison with Other Engines

| Feature | Unreal 5 | Godot | EnkiTS | **UzEngine 2.1.0** |
|---------|----------|-------|--------|------------------|
| **Task model** | `FTask` (virtual dispatch) | `WorkerThreadPool` (void* data) | `Task` (std::function) | `Job` (function ptr + 48B inline) |
| **Dependencies** | Task graph (implicit DAG) | Task ID + thread-local | Event system | Explicit `Fence` + `Counter` |
| **Work stealing** | Implicit (queue hierarchy) | Per-thread deque | Per-thread queue | MPMC ring buffer + peer stealing |
| **Priorities** | 5 levels + task graph | 3 levels | 1 level (FIFO) | 4 levels (Critical/High/Normal/Low) |
| **Synchronization** | `FGraphEvent` | Task ID polling | `Event` | `Counter` + `Fence` |
| **Lock-free** | Custom intrusive lists | Mutex-based | Mutex-based | AtomicQueue2 + atomics |
| **Parallel-for** | `ParallelFor` (chunked) | N/A | `ParallelFor` | `ParallelFor<T>` (chunked) |
| **Allocation** | Zero-alloc at enqueue | Heap allocation | Heap per task | Zero-alloc (inline 48B) |

**UzEngine Advantages:**
- **Smallest job footprint** — 48B inline buffer avoids most heap allocation
- **Zero-alloc enqueue** — critical for high-throughput scenarios
- **Explicit counter semantics** — clearer than implicit task graphs
- **MPMC lock-free** — scales to 50M enqueues/sec

**Trade-offs:**
- **No implicit dependencies** — user manages Counter/Fence explicitly
- **Limited inline data** — 48 bytes (typical lambda captures fit)
- **No task graph visualization** — Fence is lower-level

## Usage Rules & Best Practices

### DO

1. **DO use `MakeJob()` helper** — type-safe, validates 48-byte limit at compile time
2. **DO initialize `Counter` before enqueuing** — `Reset(N)` before enqueue
3. **DO use `Fence` for batches** — cleaner than manual counter management
4. **DO profile with `UZ_PROFILE_SCOPE`** — measure job submission, work, and wait overhead
5. **DO partition work upfront** — ParallelFor or manual Fence batching
6. **DO use appropriate priority** — Critical (frame-essential), High (rendering), Normal (default), Low (background)
7. **DO reuse Counter/Fence** — call Reset()/AddWork() multiple times in a loop
8. **DO call Yield() while waiting** — help drain work instead of idle spinning

### DON'T

1. **DON'T use raw `std::thread`** — always use `JobSystem::Enqueue()`
2. **DON'T enqueue jobs from within jobs** — can cause task explosion. Register all work upfront.
3. **DON'T busy-wait** — don't poll `IsComplete()` in a loop. Use `Wait()` or `Yield()`
4. **DON'T capture large objects by value** — 48-byte limit. Capture pointers and stack-allocate.
5. **DON'T share allocators between threads without locks** — use thread-local allocators or engine pool
6. **DON'T forget to increment counter before enqueuing jobs** — race condition if counter starts at 0
7. **DON'T enqueue without checking initialization** — `JobSystem::Get()` returns nullptr if not initialized

## Testing & Debugging

### Detecting Race Conditions

Use `ReadAccess<T>` / `WriteAccess<T>` in debug builds:

```cpp
uz::AccessTracker tracker;
int shared_data = 0;

// Job A (reader)
js->Enqueue(uz::MakeJob([&]() {
    auto r = uz::ReadAccess<int>(&shared_data, &tracker);
    printf("Data: %d\n", *r);
}));

// Job B (writer)
js->Enqueue(uz::MakeJob([&]() {
    auto w = uz::WriteAccess<int>(&shared_data, &tracker);
    *w = 42;
}));
```

If jobs overlap, `UZ_CHECK` fires with clear error message (reader conflicts with writer).

### Profiling Jobs

Profile job queues and work-stealing with engine stats:

```cpp
// In development, log worker stats
auto* js = uz::JobSystem::Get();
uint32_t workers = js->GetWorkerCount();

UZ_LOG(Jobs, Info, "Executed: {} jobs, Stolen: {} jobs, Workers: {}",
    executed_total, stolen_total, workers);
```

Higher `stolen` count indicates load imbalance. Use `ParallelFor` or finer-grained Fence to rebalance.

## File Organization

```
runtime/engine/job/
├── job_types.h          // Job, JobPriority, MakeJob() template
├── counter.h/.cpp       // Counter class (completion synchronization)
├── fence.h/.cpp         // Fence class (batch job management)
├── job_system.h/.cpp    // JobSystem class (worker pool, queues)
└── access_guard.h       // ReadAccess<T>, WriteAccess<T> (debug race detection)
```

**Total lines of code**: ~1100 lines (excluding external atomic_queue library)
