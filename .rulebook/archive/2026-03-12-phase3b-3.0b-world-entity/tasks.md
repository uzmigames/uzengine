## 1. Implementation

- [x] 1.1 Integrate EnTT as thirdparty dependency
- [x] 1.2 Implement World class (EnTT registry wrapper, entity ownership)
- [x] 1.3 Implement entity CRUD: Create, Destroy, IsValid
- [x] 1.4 Implement component add/remove/get/has via templated API
- [x] 1.5 Implement generational handle validation (use-after-destroy detection)
- [x] 1.6 Implement entity iteration (view/group queries via EnTT)
- [x] 1.7 Register World as ISubsystem in EngineLoop

## 2. Testing

- [x] 2.1 Test entity CRUD: create, destroy, recreate
- [x] 2.2 Test component lifecycle: add, get, remove, has
- [x] 2.3 Test generational handles: stale handle detection asserts
- [x] 2.4 Test entity iteration: view queries return correct results
- [x] 2.5 Benchmark: 100K entity create/destroy < 1ms
- [x] 2.6 Test World shutdown: clean destruction, no leaks
- [x] 2.7 Verify 95%+ coverage

## 3. Documentation

- [x] 3.1 Update `docs/engine/10-entity-component/` with World API
- [x] 3.2 Add ECS usage guide with query examples
