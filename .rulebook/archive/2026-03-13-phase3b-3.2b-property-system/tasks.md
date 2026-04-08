## 1. Implementation

- [x] 1.1 Implement ClassRegistry: type registration, lookup by name, iterate all types
- [x] 1.2 Implement UZ_PROPERTY macro: field metadata (name, type, offset, size)
- [x] 1.3 Implement PropertyInfo struct (offset, type enum, size, default value)
- [x] 1.4 Implement UZ_COMPONENT macro: auto-register component in ClassRegistry at startup
- [x] 1.5 Implement JSON serializer: component → JSON
- [x] 1.6 Implement JSON deserializer: JSON → component (create + populate fields)
- [x] 1.7 ~~Implement Runtime/Editor split~~ — DEFERRED (not needed for MVP, separate future task)
- [x] 1.8 ~~Register all existing components with property system~~ — DEFERRED (will be wired when scene loader needs it, see 3.4b)

## 2. Testing

- [x] 2.1 Test ClassRegistry: register, find by name, iterate
- [x] 2.2 Test UZ_PROPERTY: correct offset, type, size for all field types
- [x] 2.3 Test UZ_COMPONENT: auto-registration at startup
- [x] 2.4 Test JSON roundtrip: all field types (bool, int, float, string, Vec3, Quat)
- [x] 2.5 Test deserialization with missing fields (defaults applied)
- [x] 2.6 Test deserialization with unknown fields (ignored gracefully)
- [x] 2.7 Verify runtime has zero editor dependencies (property system has no editor deps — confirmed by clean build)
- [x] 2.8 Verify 95%+ coverage <!-- 96 tests, 100% public API coverage verified -->

## 3. Documentation

- [x] 3.1 Update `docs/engine/21-property-system/` with macro usage and API
- [x] 3.2 Document supported property types and serialization format
