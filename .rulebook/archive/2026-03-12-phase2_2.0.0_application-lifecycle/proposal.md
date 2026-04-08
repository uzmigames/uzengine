# Proposal: Application Lifecycle (Phase 2.0)

## Why

Every demo and tool in UzEngine currently repeats 200+ lines of init/loop/shutdown boilerplate (logging, config, paths, window, RHI, swapchain, command list, fence). Missing one shutdown step causes GPU leaks; wrong ordering causes crashes. A centralized lifecycle framework eliminates this duplication and provides the ordered init/tick/shutdown foundation that every downstream system (job system, renderer, ECS, physics, audio) depends on.

## What Changes

### New Systems

1. **EngineLoop** — 3-phase initialization (PreInit → Init → Start), main loop with fixed-timestep physics + variable-rate rendering, and reverse-order shutdown. Single entry point replaces all per-demo boilerplate.

2. **SubsystemRegistry** — `RegisterSubsystem<T>()` with explicit dependency ordering. Subsystems declare dependencies; registry topologically sorts and initializes in correct order. Shutdown is strict reverse.

3. **TimeManager** — Provides `deltaTime`, `totalTime`, `fixedDeltaTime`, `frameCount`. Fixed-timestep accumulator for deterministic physics ticks independent of display refresh rate.

4. **Module System** — `UZ_REGISTER_MODULE()` macro for static self-registration at startup. Modules register their subsystems without manual wiring.

5. **CLI Argument Parser** — `--headless`, `--screenshot N`, `--config path` parsed by EngineLoop before PreInit. Args override `.uzproject` config values.

6. **Tick Groups** — Ordered update phases per frame: PrePhysics → PostPhysics → Update → PostUpdate. Subsystems register into tick groups for deterministic execution order.

### Refactoring

- All 5 existing demos refactored to use `EngineLoop::Run()` instead of manual init/loop/shutdown.
- Existing subsystems (logging, config, RHI, shader pipeline) wrapped as `ISubsystem` implementations.

## Impact

- Affected specs: `docs/engine/09-application-lifecycle/`
- Affected code: All demos (`demos/*/`), core init code, RHI initialization
- Breaking change: YES — Demo main() functions simplified to EngineLoop configuration + Run()
- User benefit: Zero-boilerplate demo creation; correct shutdown ordering guaranteed; fixed-timestep physics ready for Phase 4
- Enables: Job System (2.1), Resource Loaders (2.2), all Phase 3+ systems
