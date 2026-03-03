# Job System / Task Graph Spec

> **Status**: Planned (Pre-Phase 1 Design)
> **Priority**: P0 — Architectural Blocker
> **Rationale**: The entire engine assumes 2 threads (Main + Game Logic). Without a job system, physics broad-phase, culling, animation, transform propagation, and particles all run single-threaded. This is exponentially harder to retrofit — every system must be designed thread-safe from the start.

---

## Overview

The **UzEngine Job System** provides a work-stealing parallel task executor that enables fine-grained parallelism across all engine subsystems. It is the foundation for all multi-threaded work in the engine.

Key capabilities:
- **Parallel-for**: Split arrays across N threads automatically
- **Fiber-based tasks**: Yield mid-task without blocking a thread
- **Dependency graph**: Chain jobs with explicit dependencies
- **Priority queues**: Critical (render), Normal (game), Background (I/O)
- **Tracy integration**: Every job appears in the profiler timeline

---

## Thread Model

```
┌─────────────────────────────────────────────────────────────┐
│  Main Thread (Render + Present)                             │
│  Game Logic Thread (ECS + Lua + Physics dispatch)           │
│  Worker Thread 0  ┐                                         │
│  Worker Thread 1  ├── Work-stealing pool                    │
│  Worker Thread 2  │   hardware_concurrency - 2 threads      │
│  Worker Thread N  ┘                                         │
│  Audio Thread (XAudio2 callbacks — never touched by jobs)   │
└─────────────────────────────────────────────────────────────┘
```

Thread count = `std::thread::hardware_concurrency() - 2` (leaves cores for Main + Audio).

---

## Job Types

| Type | Description | Max Duration | Use Cases |
|------|-------------|-------------|-----------|
| `PARALLEL` | Split work across all workers | < 1 ms per slice | Transform update, culling, skinning |
| `FIBER` | Yields mid-execution, waits for sub-jobs | < 10 ms total | Animation blend tree, complex AI |
| `BACKGROUND` | Low-priority, can run across multiple frames | Unbounded | Asset streaming, shader compilation |
| `CRITICAL` | High-priority, runs before next frame | < 2 ms | Physics callback dispatch |

---

## C++ API

### JobDispatcher

```cpp
// src/engine/core/job_system.h

namespace uz {

struct JobDesc {
    const char*     name     = "unnamed";   // Tracy label
    JobPriority     priority = JobPriority::Normal;
    JobAffinity     affinity = JobAffinity::Any;
};

struct ParallelForDesc {
    const char*     name     = "parallel_for";
    uint32_t        count    = 0;           // Total item count
    uint32_t        min_batch = 64;         // Min items per worker
    JobPriority     priority = JobPriority::Normal;
};

class JobDispatcher {
public:
    static JobDispatcher& get();

    /// Initialize thread pool (call once at engine startup)
    void init(uint32_t worker_count = 0);  // 0 = auto

    /// Shutdown thread pool (call at engine shutdown)
    void shutdown();

    /// Submit a single job
    JobHandle submit(JobDesc desc, std::function<void()> fn);

    /// Submit N jobs operating on array slices
    JobHandle submit_parallel(ParallelForDesc desc,
                              std::function<void(uint32_t begin, uint32_t end)> fn);

    /// Wait for job completion (blocks calling thread)
    void wait(JobHandle handle);

    /// Wait for multiple jobs
    void wait_all(std::span<JobHandle> handles);

    /// Check if job is done (non-blocking)
    bool is_done(JobHandle handle);

    /// Yield current fiber (only valid inside fiber jobs)
    void fiber_yield();

    /// Get worker thread count
    uint32_t worker_count() const;
};

} // namespace uz
```

### Usage Examples

```cpp
// Transform propagation (parallel-for)
void TransformSystem::execute(entt::registry& reg) {
    auto view = reg.view<CTransform, CNodeParent>();
    uint32_t count = (uint32_t)view.size();

    auto handle = JobDispatcher::get().submit_parallel({
        .name = "TransformPropagate",
        .count = count,
        .min_batch = 64,
    }, [&](uint32_t begin, uint32_t end) {
        for (uint32_t i = begin; i < end; i++) {
            auto entity = view[i];
            auto& [xform, parent] = view.get<CTransform, CNodeParent>(entity);
            propagate_transform(reg, entity, xform, parent);
        }
    });

    JobDispatcher::get().wait(handle);
}

// Animation blend tree (fiber-based, spawns sub-jobs)
void AnimationSystem::blend_tree(AnimNode* root) {
    auto& disp = JobDispatcher::get();

    // Submit sub-jobs for each branch
    auto left_handle  = disp.submit({.name="AnimBlend_Left"},  [&]{ blend_branch(root->left); });
    auto right_handle = disp.submit({.name="AnimBlend_Right"}, [&]{ blend_branch(root->right); });

    // Yield until both are done (fiber — doesn't block thread)
    disp.wait_all({left_handle, right_handle});

    // Both branches done, blend results
    blend_poses(root->left->pose, root->right->pose, root->weight, root->pose);
}

// Asset streaming (background job)
void AssetManager::stream_mesh(const std::string& path) {
    JobDispatcher::get().submit({
        .name     = "StreamMesh",
        .priority = JobPriority::Background,
    }, [path, this]() {
        auto data = load_file(path);
        auto mesh = parse_uzmesh(data);
        upload_to_gpu(mesh);
        mark_ready(path);
    });
    // Returns immediately — game loop continues
}
```

---

## Work-Stealing Scheduler

Each worker thread has a **double-ended queue (deque)**:
- Owner pushes/pops from the **bottom** (LIFO — cache-friendly)
- Thieves steal from the **top** (FIFO — oldest work stolen first)

```
Worker 0 deque:  [J6][J5][J4][J3]  ← push/pop (owner, LIFO)
Worker 1 steals: [J6] →               steal (thief, FIFO)
```

This minimizes contention: the owner rarely conflicts with thieves.

### Chase-Lev Deque

```cpp
template<typename T, size_t Capacity = 4096>
class WorkStealDeque {
public:
    void push(T item);          // Called by owner thread only
    bool pop(T& out);           // Called by owner thread only
    bool steal(T& out);         // Called by any other thread
    size_t approx_size() const; // Approximate (races OK)
};
```

---

## ECS System Scheduler Integration

The SystemScheduler uses the job system to run independent systems in parallel within a tick group:

```
TickGroup::PrePhysics:
  [TransformSystem]──┐
  [AnimationSystem]──┤─── parallel (no shared writes)
  [InputSystem]──────┘
         ↓ wait (JobDispatcher::wait_all)
TickGroup::Physics:
  [PhysicsSystem]  (single-threaded — Jolt owns its threads)
         ↓
TickGroup::PostPhysics:
  [CameraSystem]─────┐
  [AudioSystem]──────┤─── parallel
  [ParticleSystem]───┘
         ↓
TickGroup::Render:
  [VisibilitySystem]  (single-threaded — submits to render thread)
```

### System Dependency Declaration

```cpp
class TransformSystem : public ISystem {
public:
    void on_configure(SystemInfo& info) override {
        info.name = "TransformSystem";
        info.tick_group = TickGroup::PrePhysics;
        info.parallelizable = true;      // Can run alongside other parallel systems
        info.writes = { ComponentId::Transform };
        info.reads  = { ComponentId::NodeParent };
    }
};
```

---

## Physics Integration

Jolt Physics manages its own `JPH::JobSystemThreadPool` internally. The game-side bridge:

```cpp
class PhysicsSystem : public ISystem {
public:
    void execute() override {
        // Step Jolt (uses its own internal job system)
        physics_world_->Update(fixed_dt_, collision_steps_,
                               jolt_job_system_.get(),
                               jolt_temp_allocator_.get());

        // Process collision events on game-side (single-threaded)
        drain_collision_events();
    }
};

// Game-side parallel physics queries (uses UzEngine job system)
void batch_raycast(std::span<RaycastQuery> queries, std::span<RaycastResult> results) {
    auto handle = JobDispatcher::get().submit_parallel({
        .name = "BatchRaycast",
        .count = (uint32_t)queries.size(),
    }, [&](uint32_t begin, uint32_t end) {
        for (uint32_t i = begin; i < end; i++) {
            results[i] = physics_world_->raycast(queries[i]);
        }
    });
    JobDispatcher::get().wait(handle);
}
```

---

## Animation Integration

```cpp
// Parallel bone matrix palette computation
void AnimationSystem::update_bone_palettes(entt::registry& reg) {
    auto view = reg.view<CSkeletalMesh, CAnimationState>();
    uint32_t count = (uint32_t)view.size();

    auto handle = JobDispatcher::get().submit_parallel({
        .name = "BonePaletteUpdate",
        .count = count,
        .min_batch = 8,  // Each skeleton is ~67 bones, expensive per entity
    }, [&](uint32_t begin, uint32_t end) {
        for (uint32_t i = begin; i < end; i++) {
            auto entity = view[i];
            auto& [mesh, anim] = view.get<CSkeletalMesh, CAnimationState>(entity);
            compute_bone_palette(mesh, anim);
        }
    });

    JobDispatcher::get().wait(handle);
    // Palettes now ready for GPU upload in render thread
}
```

---

## Tracy Profiling Integration

Every job automatically creates a Tracy zone:

```cpp
// Inside job execution wrapper:
void JobWorker::execute_job(Job& job) {
    TRACY_ZONE_SCOPED_N(job.desc.name);  // Automatic zone with job name
    job.fn();
}

// Example Tracy output:
// Frame timeline shows:
//   Main Thread:     [Render][Present]
//   Worker 0:        [TransformPropagate_0-63][AnimBlend_Left]
//   Worker 1:        [TransformPropagate_64-127][AnimBlend_Right]
//   Worker 2:        [CullingSystem_0-255]
//   Background:      [StreamMesh: player.uzmesh]
```

---

## Thread Safety Guarantees

| System | Thread-Safe? | Notes |
|--------|-------------|-------|
| `entt::registry` reads | Yes (multiple readers) | Use `view.get()` only, no structural changes |
| `entt::registry` writes | No (lock required) | Structural changes in single-threaded group only |
| `ResourceManager::get()` | Yes | Returns const refs, loaded in background |
| `ResourceManager::load()` | Yes | Atomic reference counting |
| `Logger` | Yes | Lock-free ring buffer |
| `PhysicsWorld::raycast()` | Yes | Jolt read-only queries are thread-safe |
| `PhysicsWorld::add_body()` | No | Must be in Physics tick group |
| Lua VM | No | Single VM, never touch from workers |
| DX12 command lists | No | Each thread needs its own allocator |

---

## Implementation Plan

### Phase 0 (Design — 1 week)
- Finalize API surface (this document)
- Decide: custom implementation vs. enkiTS / marl / concurrentqueue

### Phase 1 (Basic — 2 weeks)
- Thread pool with Chase-Lev deques
- `submit()`, `submit_parallel()`, `wait()`
- Tracy integration
- Tests: parallel-for correctness, work stealing

### Phase 2 (Fibers — 2 weeks)
- Fiber context switching (platform assembly)
- `fiber_yield()` implementation
- Dependency graph (`submit(deps={...})`)

### Phase 3 (GPU Readback — Phase 3)
- Background jobs for async GPU readback
- `submit_after_gpu_fence()` — job starts when fence signals

---

## Recommended Third-Party Libraries

| Library | License | Notes |
|---------|---------|-------|
| **enkiTS** | zlib | Lightweight, battle-tested, used in id Tech 7 |
| **marl** | Apache 2.0 | Google, fiber-based, more complex |
| **std::execution** | C++20 | Standard but immature implementations |

**Recommendation**: enkiTS for Phase 1, extend with custom fibers for Phase 2.

---

## Dependencies

- None (foundational system)
- Tracy (optional, for profiling zones)

---

## Related Specs

- [Memory System](memory-system.md)
- [ECS and Scene System](../../engine/ecs-and-scene.md)
- [Physics System](../physics/physics-system.md)
- [Animation System](../assets/animation-system.md)
