# Proposal: phase0-job-system-design

## Why

UzEngine currently assumes exactly 2 threads: Main (render) and Game Logic (ECS + Lua + physics dispatch). Every engine system — transform propagation, animation blend trees, physics culling, particle simulation, audio update — runs serially on a single thread. This is the single biggest performance bottleneck in the architecture.

Without a job system, retrofitting parallelism later requires rewriting every ECS system to be thread-safe. Architectural parallelism cannot be added incrementally — it must be designed into the system scheduler from day one. A AAA engine targeting 60fps with 1000+ entities and dozens of complex systems requires multi-core utilization. `while true do end` in Lua is just one symptom of having no execution isolation.

## What Changes

- Design and spec the `JobDispatcher` C++ API: `submit()`, `submit_parallel()`, `wait()`, `fiber_yield()`
- Define the thread model: Main + Game Logic + N worker threads (`hardware_concurrency - 2`)
- Design the Chase-Lev work-stealing deque scheduler
- Define fiber-based task suspension (animation blend trees, complex AI)
- Tracy profiling zone integration per job
- ECS system scheduler parallelism groups (systems with non-overlapping read/write sets run simultaneously)
- Evaluate and select third-party library: **enkiTS** (used in id Tech 7, zlib license)

No code written in Phase 0 — this is a design and spec task only. Implementation is in Phase 1.

## Impact

- Affected specs: `docs/specs/engine/job-system.md`, ECS system scheduler
- Affected code: All future ECS systems (designed parallelizable from day one)
- Breaking change: NO (design only, no existing code changed)
- User benefit: 4–8× CPU utilization on multi-core hardware; enables AAA-scale entity counts at 60fps
