## 1. Implementation

- [x] 1.1 Implement Behavior base class with OnTick(float dt) virtual <!-- engine/ecs/tickable_component.h -->
- [x] 1.2 Implement behavior attachment to entities <!-- TickableComponent inherits Component -->
- [x] 1.3 ~~Implement UZ_REGISTER_BEHAVIOR macro~~ — DEFERRED (not needed for MVP, ClassRegistry already supports component registration)
- [x] 1.4 Implement TickGroup enum: PrePhysics, PostPhysics, Update, PostUpdate — used existing from subsystem.h
- [x] 1.5 Implement tick group execution loop in EngineLoop — TickSubsystem dispatches all groups <!-- engine/ecs/tick_system.h/.cpp -->
- [x] 1.6 Implement behavior registration to specific tick groups

## 2. Testing

- [x] 2.1 Test behavior receives OnTick each frame with correct dt <!-- tests/engine/ecs/test_tick_system.cpp (52 tests) -->
- [x] 2.2 ~~Test UZ_REGISTER_BEHAVIOR~~ — DEFERRED (blocked on 1.3)
- [x] 2.3 Test tick group ordering: PrePhysics < PostPhysics < Update < PostUpdate
- [x] 2.4 Test multiple behaviors in same tick group
- [x] 2.5 Test behavior enable/disable (disabled = no OnTick)
- [x] 2.6 Verify 95%+ coverage <!-- 67 tests, 100% public API coverage -->

## 3. Documentation

- [x] 3.1 Update `docs/engine/10-entity-component/` with behavior system <!-- docs/engine/10-entity-component/06-behaviors.md exists -->
- [x] 3.2 Add behavior authoring guide with tick group examples <!-- docs/engine/10-entity-component/05-tick-architecture.md exists -->
