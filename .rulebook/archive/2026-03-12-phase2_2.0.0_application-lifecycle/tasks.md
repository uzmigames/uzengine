## 1. Implementation

- [x] 1.1 Define `ISubsystem` interface (`Init`, `Shutdown`, `Tick`, `GetName`, `GetDependencies`)
- [x] 1.2 Implement `SubsystemRegistry` with topological sort and reverse-order shutdown
- [x] 1.3 Implement `TimeManager` (delta time, total time, fixed-timestep accumulator, frame counter)
- [x] 1.4 Implement `EngineLoop` with 3-phase init (PreInit → Init → Start) and main loop
- [x] 1.5 Implement tick groups (PrePhysics, PostPhysics, Update, PostUpdate) with subsystem registration
- [x] 1.6 Implement `UZ_REGISTER_MODULE()` macro for static self-registration
- [x] 1.7 Implement CLI argument parser (`--headless`, `--screenshot`, `--config`)
- [x] 1.8 Implement graceful shutdown (reverse-order teardown, GPU fence wait, zero leaks)
- [x] 1.9 Wrap existing subsystems (logging, config, paths) as `ISubsystem`
- [ ] 1.10 Refactor all 5 demos to use `EngineLoop::Run()`

## 2. Testing

- [x] 2.1 Test SubsystemRegistry: dependency ordering, circular dependency detection, reverse shutdown
- [x] 2.2 Test TimeManager: fixed-timestep accumulator, delta clamping, frame counting
- [x] 2.3 Test EngineLoop: 3-phase init sequence, headless mode, graceful shutdown
- [x] 2.4 Test tick groups: correct execution order, subsystem registration to groups
- [x] 2.5 Test module system: static registration, duplicate detection
- [x] 2.6 Test CLI parser: arg parsing, config override, unknown arg handling
- [x] 2.7 Verify 95%+ coverage on all new code

## 3. Documentation

- [x] 3.1 Update `docs/engine/09-application-lifecycle/` with final API and examples
- [x] 3.2 Add lifecycle integration guide for demo authors
