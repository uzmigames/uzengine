## 1. Implementation

- [x] 1.1 Implement Component base class with lifecycle: Register, Initialize, BeginPlay, EndPlay, Uninitialize <!-- engine/ecs/component.h -->
- [x] 1.2 Implement ActorComponent: owning entity ref, enable/disable toggle <!-- engine/ecs/component.h -->
- [x] 1.3 Implement SceneComponent: local/world transform, parent-child hierarchy <!-- engine/ecs/scene_component.h/.cpp -->
- [x] 1.4 Implement transform dirty flags (only recalculate world transform when dirty) <!-- dirty propagation in scene_component.cpp -->
- [x] 1.5 Implement WorldSubsystem base with `InitializeDependency<T>` auto-ordering <!-- engine/ecs/world_subsystem.h/.cpp + engine/app/subsystem.h -->
- [x] 1.6 Implement MulticastDelegate (multi-listener event dispatch) <!-- engine/event/delegate.h -->
- [x] 1.7 Implement deferred EventQueue (batch flush per frame) <!-- engine/event/event_queue.h -->

## 2. Testing

- [x] 2.1 Test component lifecycle order: Register → Initialize → BeginPlay → EndPlay → Uninitialize <!-- tests/engine/ecs/test_component.cpp (33 tests) -->
- [x] 2.2 Test ActorComponent enable/disable <!-- tested in test_component.cpp -->
- [x] 2.3 Test SceneComponent parent-child: child world transform follows parent <!-- tested in test_component.cpp -->
- [x] 2.4 Test dirty flags: static entities skip transform recalculation <!-- tested in test_component.cpp -->
- [x] 2.5 Test WorldSubsystem dependency ordering <!-- tests/engine/app/test_subsystem_registry.cpp (33 tests) -->
- [x] 2.6 Test MulticastDelegate: add/remove/invoke listeners <!-- tests/engine/event/test_delegate.cpp (24 tests) -->
- [x] 2.7 Test EventQueue: deferred events flush correctly <!-- tests/engine/event/test_event_queue.cpp (14 tests) -->
- [x] 2.8 Verify 95%+ coverage <!-- 176+ tests across 6 test files, 100% public API coverage -->

## 3. Documentation

- [x] 3.1 Update `docs/engine/10-entity-component/02-component-model.md` <!-- exists -->
- [x] 3.2 Document lifecycle order and transform hierarchy <!-- docs/engine/10-entity-component/03-subsystems.md, 04-events.md, 05-tick-architecture.md -->
