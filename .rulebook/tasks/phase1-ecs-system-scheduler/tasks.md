## 1. Prerequisites

- [ ] 1.1 Confirm `phase1-scene-system` is complete — EnTT registry and `uz_engine_update` entry point exist
- [ ] 1.2 Read `docs/specs/scene-system.md` — understand current update flow and system coupling
- [ ] 1.3 Decide on tick group names — document choice: `PreUpdate`, `PhysicsUpdate`, `Update`, `LateUpdate`, `PreRender`

## 2. IEcsSystem Interface

- [ ] 2.1 Create `src/engine/ecs/system_scheduler.h`
- [ ] 2.2 Define `TickGroup` enum: `PreUpdate = 0`, `PhysicsUpdate`, `Update`, `LateUpdate`, `PreRender` (5 groups)
- [ ] 2.3 Define `IEcsSystem` pure virtual interface:
  - `virtual void on_register(entt::registry&)` — setup phase (observers, pools)
  - `virtual void tick(entt::registry&, float dt)` — update body
  - `virtual void on_shutdown(entt::registry&)` — cleanup
  - `virtual const char* name() const` — for debug/profiler display
- [ ] 2.4 Define `SystemDesc` struct: `IEcsSystem*`, `TickGroup group`, `before[]` names, `after[]` names, `parallel_safe` bool

## 3. SystemRegistry

- [ ] 3.1 Create `src/engine/ecs/system_registry.h/cpp` — `SystemRegistry` singleton
- [ ] 3.2 Implement `SystemRegistry::register_system(SystemDesc)` — stores descriptor
- [ ] 3.3 Implement `SystemRegistry::build_scheduler() -> SystemScheduler` — called at engine init after all registrations
- [ ] 3.4 Ensure `SystemRegistry::register_system` is callable before engine init (static initializer safe)

## 4. SystemScheduler

- [ ] 4.1 Create `src/engine/ecs/system_scheduler.cpp`
- [ ] 4.2 Implement `SystemScheduler::build(vector<SystemDesc>)`:
  - Group systems by `TickGroup`
  - Within each group: build dependency graph from `before[]` and `after[]` names
  - Topological sort (Kahn's algorithm) within each group
  - Detect cycles: if cycle found, log error and abort engine startup
- [ ] 4.3 Implement `SystemScheduler::tick(entt::registry&, float dt)`:
  - Iterate groups in enum order (PreUpdate → PhysicsUpdate → Update → LateUpdate → PreRender)
  - Within each group: execute systems in topological order
  - For `parallel_safe` batches with no mutual dependencies: dispatch to job pool, await completion
- [ ] 4.4 Implement `SystemScheduler::on_shutdown(entt::registry&)` — call `on_shutdown` on all systems in reverse order
- [ ] 4.5 Implement `SystemScheduler::get_execution_order() -> vector<string>` — debug introspection

## 5. Job Pool for Parallel Groups

- [ ] 5.1 Define `SystemJobPool` — fixed thread pool using `std::jthread` (2 workers minimum)
- [ ] 5.2 Implement `SystemJobPool::dispatch_parallel(vector<IEcsSystem*>, registry, dt)` — submit each, await all
- [ ] 5.3 Parallel execution guard: `parallel_safe` systems must not access shared EnTT component pools that non-parallel systems also write
- [ ] 5.4 Add `#ifdef UZ_PARALLEL_SYSTEMS` compile guard — disable in debug for deterministic single-thread execution

## 6. Engine Integration

- [ ] 6.1 Update `src/engine/engine.cpp` — `uz_engine_update(dt)` calls `SystemScheduler::tick(registry, dt)`
- [ ] 6.2 Update engine init — call `SystemRegistry::build_scheduler()` after all subsystems registered
- [ ] 6.3 Register existing update systems (physics, audio placeholders) via `SystemRegistry::register_system`
- [ ] 6.4 Ensure `uz_engine_shutdown` calls `SystemScheduler::on_shutdown`

## 7. Profiler Integration

- [ ] 7.1 Wrap each `system->tick()` call with `UZ_PROFILE_SCOPE(system->name())` macro
- [ ] 7.2 Emit per-system timing data in `profiler.frame` IPC message
- [ ] 7.3 Display system execution order in profiler flame chart as nested zones under group label

## 8. Testing

- [ ] 8.1 Unit test: `build([A before B, B before C])` → execution order: A, B, C
- [ ] 8.2 Unit test: `build([A after B, B after C])` → execution order: C, B, A
- [ ] 8.3 Unit test: cycle detection — `[A before B, B before A]` → error logged, scheduler unusable
- [ ] 8.4 Unit test: systems in different `TickGroup` execute in group order regardless of before/after
- [ ] 8.5 Unit test: `parallel_safe` systems in same group with no mutual dependency execute concurrently
- [ ] 8.6 Integration test: register 5 systems, call `scheduler.tick(registry, 0.016)` — all `tick()` methods called exactly once
- [ ] 8.7 Integration test: `on_shutdown` called in reverse topological order

## 9. Documentation

- [ ] 9.1 Update `docs/specs/scene-system.md` — add "System Scheduler" section with `TickGroup` table and `SystemDesc` API
- [ ] 9.2 Create `docs/specs/ecs-system-scheduler.md` — full spec with execution model, dependency syntax, parallel safety rules
- [ ] 9.3 Update `docs/engine/cpp-architecture.md` — update flow diagram to show scheduler dispatch
