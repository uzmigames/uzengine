## 1. Batch Query Data Structures
- [ ] 1.1 Define `BatchRaycastQuery` struct in `src/physics/physics_world.h` with fields: `origin` (float3), `direction` (float3, must be unit length), `max_distance` (float), `filter_mask` (uint32_t), `user_data` (uint64_t, caller-assigned opaque token for result correlation)
- [ ] 1.2 Define `BatchSphereOverlapQuery` struct in `src/physics/physics_world.h` with fields: `center` (float3), `radius` (float), `filter_mask` (uint32_t), `user_data` (uint64_t)
- [ ] 1.3 Define `RaycastResult` struct with fields: `hit` (bool), `distance` (float), `hit_normal` (float3), `hit_entity` (EntityID), `user_data` (uint64_t, echoed from query)
- [ ] 1.4 Define `OverlapResult` struct with fields: `hit_count` (int), `hit_entities[8]` (EntityID array, max 8 overlapping bodies), `user_data` (uint64_t, echoed from query)

## 2. PhysicsWorld Batch Raycast
- [ ] 2.1 Add `PhysicsWorld::batch_query(std::span<const BatchRaycastQuery> queries, std::span<RaycastResult> results)` declaration to `src/physics/physics_world.h`; `results` span must have the same count as `queries`
- [ ] 2.2 Implement `PhysicsWorld::batch_query()` in `src/physics/physics_world.cpp` using `JobSystem::submit_parallel()` to fan out individual Jolt `RayCast` queries across available CPU cores, one task per query; each task writes directly into the corresponding `results[i]` slot (zero allocation)
- [ ] 2.3 Block the calling thread until all parallel raycast tasks complete (synchronous at the call site) using a job counter fence before returning

## 3. PhysicsWorld Batch Sphere Overlap
- [ ] 3.1 Add `PhysicsWorld::batch_sphere_overlap(std::span<const BatchSphereOverlapQuery> queries, std::span<OverlapResult> results)` declaration to `src/physics/physics_world.h`
- [ ] 3.2 Implement `PhysicsWorld::batch_sphere_overlap()` in `src/physics/physics_world.cpp` using `JobSystem::submit_parallel()` to fan out Jolt `OverlapShape` queries; each task writes up to 8 hit entity IDs into `results[i].hit_entities` and sets `hit_count`
- [ ] 3.3 Block the calling thread until all parallel overlap tasks complete using the same job counter fence pattern as the raycast implementation

## 4. PhysicsSystem Simulation Fence
- [ ] 4.1 Update `PhysicsSystem::update()` in `src/physics/PhysicsSystem.cpp` to ensure all pending batch jobs from the previous frame's query submission are fenced before stepping the Jolt simulation; this prevents batch queries from running concurrently with the Jolt narrowphase
- [ ] 4.2 Assert in debug builds that `batch_query()` and `batch_sphere_overlap()` are not called from within the Jolt simulation step callback

## 5. Lua Bindings
- [ ] 5.1 Add `ctx.physics:batch_raycast(queries_table)` in `src/scripting/lua_physics.cpp` where `queries_table` is an array of Lua tables each with keys: `origin`, `direction`, `max_distance`, `filter_mask` (optional, default all), `user_data` (optional int); returns an array of result tables with keys: `hit`, `distance`, `hit_normal`, `hit_entity`, `user_data`
- [ ] 5.2 Add `ctx.physics:batch_sphere_overlap(queries_table)` with the same pattern; each query table has keys `center`, `radius`, `filter_mask`, `user_data`; returns array of result tables with `hit_count`, `hit_entities` (array), `user_data`
- [ ] 5.3 Validate Lua inputs: reject queries with zero-length `direction` (log error, skip that query, set `hit = false` in result); reject negative `radius`; reject empty queries table (return empty results table immediately)

## 6. Testing
- [ ] 6.1 Create `tests/physics/test_batch_raycast.cpp` with Google Test cases: 1 query with known hit returns correct distance, 100 queries all return correct hit/miss results matching serial raycast reference, zero-length query span returns immediately with no crash, `user_data` is echoed correctly in all results
- [ ] 6.2 Add batch sphere overlap tests: single query over 3 overlapping bodies returns hit_count = 3, query with no overlapping bodies returns hit_count = 0, more than 8 overlapping bodies returns hit_count = 8 (clamped)
- [ ] 6.3 Add parallel execution tests: submit 64 raycasts, assert all 64 complete before the function returns (no results left unwritten), results are deterministic across repeated calls with the same inputs
- [ ] 6.4 Add performance regression test: 500 raycasts via `batch_query()` must complete in less than 5ms on the test machine (measured with a timer, not a hard CI gate but logged as a warning if exceeded)
- [ ] 6.5 Add Lua binding tests: Lua `batch_raycast()` with 5 queries returns a table of length 5, missing `direction` field logs error and returns `hit = false` for that slot, empty table input returns empty table output
- [ ] 6.6 Verify line coverage >= 95% and branch coverage >= 95% across `physics_world.cpp` batch functions and `lua_physics.cpp` batch binding paths

## 7. Documentation
- [ ] 7.1 Update `docs/specs/physics-system.md` with `BatchRaycastQuery`, `BatchSphereOverlapQuery`, `RaycastResult`, `OverlapResult` struct field references, `batch_query()` and `batch_sphere_overlap()` function signatures, zero-allocation guarantee, and the simulation fence behavior
- [ ] 7.2 Update `docs/specs/ai-native-features.md` with a section on using batch physics queries for AI line-of-sight checks, including a pattern for 100 agents each submitting 5 raycasts per frame
