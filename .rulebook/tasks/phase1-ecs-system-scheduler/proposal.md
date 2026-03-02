# Proposal: phase1-ecs-system-scheduler

## Why

The current engine update loop runs all systems in a hard-coded sequence inside `uz_engine_update`. Adding new subsystems (physics, audio, AI, particles) requires editing the update function and carefully reasoning about ordering. There is no way to express "physics runs before AI" or "rendering reads after all gameplay systems write" in a declarative way, and no mechanism to run independent systems in parallel.

A `SystemScheduler` with `TickGroups` (PreUpdate, Update, LateUpdate, PreRender) and explicit `before()/after()` dependency declarations gives each subsystem ownership of its position in the update graph. The scheduler topologically sorts groups, validates dependency cycles at startup, and runs parallel groups concurrently on the job system.

## What Changes

- **`src/engine/ecs/system_scheduler.h/cpp`** — `SystemScheduler`, `SystemGroup` enum, `IEcsSystem` interface
- **`src/engine/ecs/system_registry.h/cpp`** — `SystemRegistry` singleton for registration
- **`src/engine/engine.cpp`** — `uz_engine_update` delegates to `SystemScheduler::tick(dt)`
- **Tests** — registration, topological sort, cycle detection, execution order, parallel group execution

## Impact

- Affected specs: `docs/specs/scene-system.md` (system update model)
- Dependencies: `phase1-scene-system` (ECS/EnTT registry must exist)
- Breaking change: INTERNAL — update loop refactored; no user-facing API changes
- Downstream benefit: physics, AI, particles, audio all register via `SystemRegistry`; no more hard-coded update order
